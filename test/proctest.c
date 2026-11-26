#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "proclaunch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

/* Аргументы, которые неудобно передавать на Windows. Ребёнок сравнивает
   полученные argv с этим же массивом. */
static const char *const tricky_args[] = {
    "hello \"big\" world\\",
    "hello world",
    "say \"hi\"",
    "C:\\my dir\\",
    "a\\\\b c",
    "",
    "plain",
};
#define TRICKY_COUNT ((int)(sizeof tricky_args / sizeof tricky_args[0]))

static int failed = 0;

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0)
        ;
#endif
}

/* Путь к своему exe. argv[0] не всегда подходит (например, если запустили через PATH). */
static const char *self_path(const char *argv0)
{
    static char buf[4096];
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
    if (n > 0 && n < sizeof buf)
        return buf;
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = '\0';
        return buf;
    }
#endif
    return argv0;
}

static void report(int ok, const char *name)
{
    printf("[%s] %s\n", ok ? " OK " : "FAIL", name);
    if (!ok)
        failed++;
}

/* ---------- режимы ребёнка ---------- */

/* Возвращает 1, если программа запущена как ребёнок; код выхода - в *code. */
static int child_main(int argc, char **argv, int *code)
{
    /* --child КОД МС */
    if (argc >= 4 && strcmp(argv[1], "--child") == 0) {
        sleep_ms(atoi(argv[3]));
        *code = atoi(argv[2]);
        return 1;
    }

    /* --args a1 a2 ... - проверить, что аргументы пришли без искажений */
    if (argc >= 2 && strcmp(argv[1], "--args") == 0) {
        int got = argc - 2;
        int bad = (got != TRICKY_COUNT);
        printf("    ребёнок получил %d аргументов:\n", got);
        for (int i = 0; i < got; i++) {
            int same = (i < TRICKY_COUNT && strcmp(argv[i + 2], tricky_args[i]) == 0);
            if (!same)
                bad = 1;
            printf("      [%s]%s\n", argv[i + 2], same ? "" : "  <- не совпадает");
        }
        fflush(stdout);
        *code = bad ? 1 : 0;
        return 1;
    }

    return 0;
}

/* ---------- тесты ---------- */

static void test_background(const char *self)
{
    const char *args[] = { self, "--child", "42", "1000", NULL };
    pl_process p;
    if (pl_start(args, &p) != 0) {
        report(0, "1. фоновый запуск: pl_start не сработала");
        return;
    }

    int iterations = 0, code = -1, r;
    while ((r = pl_poll(&p, &code)) == 0) {
        iterations++;
        sleep_ms(100);
    }

    printf("    pid %ld, родитель успел сделать %d итераций, код %d\n", p.pid, iterations, code);
    report(r == 1 && iterations > 3 && code == 42, "1. родитель работает, пока работает ребёнок; код 42");
}

static void test_wait(const char *self)
{
    const char *args[] = { self, "--child", "7", "300", NULL };
    pl_process p;
    int code = -1;
    int ok = pl_start(args, &p) == 0 && pl_wait(&p, &code) == 0;
    printf("    код %d\n", code);
    report(ok && code == 7, "2. pl_wait дожидается и возвращает код 7");
}

static void test_args(const char *self)
{
    const char *args[TRICKY_COUNT + 3];
    args[0] = self;
    args[1] = "--args";
    for (int i = 0; i < TRICKY_COUNT; i++)
        args[i + 2] = tricky_args[i];
    args[TRICKY_COUNT + 2] = NULL;

    fflush(stdout); /* чтобы вывод ребёнка не перемешался с нашим */
    pl_process p;
    int code = -1;
    int ok = pl_start(args, &p) == 0 && pl_wait(&p, &code) == 0;
    report(ok && code == 0, "3. аргументы с пробелами, кавычками и слешами переданы без искажений");
}

static void test_missing(void)
{
    const char *args[] = { "no_such_program_12345", NULL };
    pl_process p;
    int r = pl_start(args, &p);
    printf("    pl_start вернула %d\n", r);
    report(r == -1, "4. запуск несуществующей программы - ошибка");
}

static void test_big_code(const char *self)
{
    const char *args[] = { self, "--child", "300", "0", NULL };
    pl_process p;
    int code = -1;
    int ok = pl_start(args, &p) == 0 && pl_wait(&p, &code) == 0;
#ifdef _WIN32
    int expected = 300;
#else
    int expected = 300 & 0xFF; /* в POSIX код 8-битный: 300 % 256 = 44 */
#endif
    printf("    код %d (ожидается %d)\n", code, expected);
    report(ok && code == expected, "5. код возврата 300");
}

/* proctest --code N : запустить ребёнка с заданным кодом и показать результат */
static int run_with_code(const char *self, const char *code_str)
{
    const char *args[] = { self, "--child", code_str, "500", NULL };
    pl_process p;
    int code;
    if (pl_start(args, &p) != 0 || pl_wait(&p, &code) != 0) {
        printf("не удалось запустить ребёнка\n");
        return 1;
    }
    printf("ребёнок (pid %ld) завершился с кодом %d\n", p.pid, code);
    return 0;
}

/* proctest --run ПРОГРАММА [АРГУМЕНТЫ...] : запустить любую программу и дождаться */
static int run_program(char **argv)
{
    pl_process p;
    int code;
    if (pl_start((const char *const *)argv, &p) != 0) {
        printf("pl_start: не удалось запустить \"%s\"\n", argv[0]);
        return 1;
    }
    printf("запущен pid %ld\n", p.pid);
    fflush(stdout);
    if (pl_wait(&p, &code) != 0) {
        printf("pl_wait: ошибка\n");
        return 1;
    }
    printf("код возврата: %d\n", code);
    return 0;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    int child_code;
    if (child_main(argc, argv, &child_code))
        return child_code;

    const char *self = self_path(argv[0]);

    if (argc >= 3 && strcmp(argv[1], "--code") == 0)
        return run_with_code(self, argv[2]);
    if (argc >= 3 && strcmp(argv[1], "--run") == 0)
        return run_program(argv + 2);

    printf("proctest: %s\n\n", self);
    test_background(self);
    test_wait(self);
    test_args(self);
    test_missing();
    test_big_code(self);

    printf("\n%s: провалено тестов - %d\n", failed ? "ЕСТЬ ОШИБКИ" : "ВСЕ ТЕСТЫ ПРОШЛИ", failed);
    return failed ? 1 : 0;
}
