#include "platform.h"
#include "proclaunch.h"
#include "shared.h"

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <pthread.h>
#endif

static const char *LOG_NAME = "lab3.log";

static const int64_t TICK_MS = 300;    // +1 к счётчику
static const int64_t LOG_MS = 1000;    // строка в лог
static const int64_t SPAWN_MS = 3000;  // запуск копий
static const int64_t LEADER_TIMEOUT_MS = 1000;

static std::atomic<bool> stop{false};
static FILE *log_file = nullptr;
static std::string self;

static pl_process child[2];
static bool running[2];

// Пишет строку в лог. Вызывать только под shared_lock(), сама замок не берёт.
static void log_line(const char *fmt, ...)
{
    if (!log_file)
        return;
    char ts[64];
    time_str(ts, sizeof ts);
    fprintf(log_file, "%s  pid %-6ld  ", ts, my_pid());

    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);

    fputc('\n', log_file);
    fflush(log_file);
}

/* ---------- копии ---------- */

static void run_copy1()
{
    shared_lock();
    log_line("start (copy 1)");
    shared_data()->counter += 10;
    log_line("exit (copy 1)");
    shared_unlock();
}

static void run_copy2()
{
    shared_lock();
    log_line("start (copy 2)");
    shared_data()->counter *= 2;
    shared_unlock();

    sleep_ms(2000);   // замок на время паузы не держим

    shared_lock();
    shared_data()->counter /= 2;
    log_line("exit (copy 2)");
    shared_unlock();
}

// Забрать завершившиеся копии, чтобы в Linux не висели зомби.
static void reap_copies()
{
    for (int i = 0; i < 2; i++) {
        int code;
        if (running[i] && pl_poll(&child[i], &code) != 0)
            running[i] = false;
    }
}

static void spawn_copies()
{
    for (int i = 0; i < 2; i++) {
        int code;
        if (running[i] && pl_poll(&child[i], &code) == 0) {   // ещё работает
            shared_lock();
            log_line("copy %d (pid %ld) is still running, not started", i + 1, child[i].pid);
            shared_unlock();
            continue;
        }

        const char *args[] = { self.c_str(), i == 0 ? "--copy1" : "--copy2", NULL };
        running[i] = (pl_start(args, &child[i]) == 0);
        if (!running[i]) {
            shared_lock();
            log_line("failed to start copy %d", i + 1);
            shared_unlock();
        }
    }
}

/* ---------- таймер ---------- */

// Сроки считаются от расписания, а не от текущего времени, иначе интервалы поплывут.
// Если процесс долго стоял (например, его приостановили), не догоняем пропущенное, а начинаем заново.
static bool due(int64_t &next, int64_t period, int64_t now)
{
    if (now < next)
        return false;
    next += period;
    if (now - next > period)
        next = now + period;
    return true;
}

static void timer_thread()
{
    int64_t start = mono_ms();
    int64_t next_tick = start + TICK_MS;
    int64_t next_log = start + LOG_MS;
    int64_t next_spawn = start + SPAWN_MS;
    bool leader = false;

    while (!stop) {
        int64_t now = mono_ms();
        int64_t wake = next_tick;
        if (next_log < wake) wake = next_log;
        if (next_spawn < wake) wake = next_spawn;
        if (now < wake) {
            int64_t left = wake - now;
            sleep_ms(left < 50 ? left : 50);   // короткими кусками, чтобы быстро заметить stop
            continue;
        }

        if (due(next_tick, TICK_MS, now)) {
            shared_lock();
            SharedData *d = shared_data();
            d->counter += 1;

            int64_t t = mono_ms();
            if (d->leader_pid == my_pid()) {
                d->leader_beat = t;
            } else if (d->leader_pid == 0 || t - d->leader_beat > LEADER_TIMEOUT_MS) {
                d->leader_pid = my_pid();
                d->leader_beat = t;
                log_line("became the leader");
            }
            leader = (d->leader_pid == my_pid());
            shared_unlock();

            reap_copies();
        }

        if (due(next_log, LOG_MS, now) && leader) {
            shared_lock();
            log_line("counter = %lld", (long long)shared_data()->counter);
            shared_unlock();
        }

        if (due(next_spawn, SPAWN_MS, now) && leader)
            spawn_copies();
    }
}

/* ---------- Ctrl+C ---------- */

#ifdef _WIN32

static std::atomic<bool> cleanup_done{false};

static BOOL WINAPI on_console_event(DWORD)
{
    stop = true;
    // fgets в основном потоке вернёт NULL
    CloseHandle(GetStdHandle(STD_INPUT_HANDLE));
    // ждём, пока основной поток уберёт за собой (не дольше 5 с)
    for (int i = 0; i < 100 && !cleanup_done; i++)
        Sleep(50);
    return TRUE;
}

#else

static void on_signal(int)
{
    stop = true;
}

#endif

/* ---------- main ---------- */

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    int copy = 0;
    if (argc >= 2 && strcmp(argv[1], "--copy1") == 0)
        copy = 1;
    else if (argc >= 2 && strcmp(argv[1], "--copy2") == 0)
        copy = 2;

    if (copy) {
        // Ctrl+C в консоли получают все процессы; копия должна спокойно доработать
#ifdef _WIN32
        SetConsoleCtrlHandler(NULL, TRUE);
#else
        // копию запускает поток таймера, у которого SIGINT/SIGTERM заблокированы,
        // а маска сигналов переживает fork и exec - снимаем блокировку
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        signal(SIGINT, SIG_IGN);
#endif
    }

    if (!shared_open()) {
        fprintf(stderr, "не удалось открыть общую память\n");
        return 1;
    }
    log_file = fopen(LOG_NAME, "a");
    if (!log_file)
        fprintf(stderr, "не удалось открыть %s, лог писаться не будет\n", LOG_NAME);

    if (copy) {
        if (copy == 1)
            run_copy1();
        else
            run_copy2();
        if (log_file)
            fclose(log_file);
        shared_close();
        return 0;
    }

    self = self_path();
    if (self.empty())
        self = argv[0];

    shared_lock();
    log_line("start");
    shared_unlock();

    printf("PID %ld, лог: %s\n", my_pid(), LOG_NAME);
    printf("Введите число, чтобы задать счётчик, или q для выхода.\n");
    fflush(stdout);

#ifdef _WIN32
    std::thread timer(timer_thread);
    SetConsoleCtrlHandler(on_console_event, TRUE);
#else
    // сигнал должен приходить в основной поток: на время создания таймера блокируем его,
    // маска наследуется потоком
    sigset_t set, old;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, &old);
    std::thread timer(timer_thread);
    pthread_sigmask(SIG_SETMASK, &old, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;   // без SA_RESTART: fgets прервётся
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
#endif

    char line[128];
    while (!stop) {
        if (!fgets(line, sizeof line, stdin))
            break;   // Ctrl+C, конец ввода или ошибка
        if (stop)
            break;
        trim(line);
        if (line[0] == '\0')
            continue;
        if (strcmp(line, "q") == 0)
            break;

        char *end;
        errno = 0;
        long long value = strtoll(line, &end, 10);
        if (*end != '\0' || errno != 0) {
            printf("нужно целое число или q\n");
            fflush(stdout);
            continue;
        }

        shared_lock();
        shared_data()->counter = value;
        log_line("counter set to %lld by user", value);
        shared_unlock();
    }

    // выход
    stop = true;
    timer.join();

    shared_lock();
    if (shared_data()->leader_pid == my_pid())
        shared_data()->leader_pid = 0;   // место ведущего сразу займёт другой
    shared_unlock();

    for (int i = 0; i < 2; i++) {
        int code;
        if (running[i])
            pl_wait(&child[i], &code);
    }

    shared_lock();
    log_line("exit");
    shared_unlock();

    if (log_file)
        fclose(log_file);
    shared_close();

#ifdef _WIN32
    cleanup_done = true;
#endif
    printf("выход\n");
    return 0;
}
