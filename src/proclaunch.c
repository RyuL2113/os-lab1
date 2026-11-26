#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "proclaunch.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#include <windows.h>

/* Дописывает один аргумент в командную строку по правилам разбора
   CommandLineToArgvW / CRT. Если out == NULL, только считает длину. */
static size_t append_arg(char *out, const char *arg)
{
    size_t n = 0;
    int need_quotes = (arg[0] == '\0') || strpbrk(arg, " \t\n\v\"") != NULL;

    if (!need_quotes) {
        size_t len = strlen(arg);
        if (out) memcpy(out, arg, len);
        return len;
    }

    if (out) out[n] = '"';
    n++;

    for (const char *p = arg; ; p++) {
        size_t slashes = 0;
        while (*p == '\\') {
            slashes++;
            p++;
        }

        if (*p == '\0') {
            /* слеши перед закрывающей кавычкой удваиваем */
            for (size_t i = 0; i < slashes * 2; i++) {
                if (out) out[n] = '\\';
                n++;
            }
            break;
        }

        if (*p == '"') {
            /* удвоенные слеши + \" */
            for (size_t i = 0; i < slashes * 2 + 1; i++) {
                if (out) out[n] = '\\';
                n++;
            }
        } else {
            /* обычные слеши в середине оставляем как есть */
            for (size_t i = 0; i < slashes; i++) {
                if (out) out[n] = '\\';
                n++;
            }
        }
        if (out) out[n] = *p;
        n++;
    }

    if (out) out[n] = '"';
    n++;
    return n;
}

static char *build_cmdline(const char *const argv[])
{
    size_t total = 0;
    for (int i = 0; argv[i]; i++)
        total += append_arg(NULL, argv[i]) + 1;   /* +1 на пробел или '\0' */

    char *cmd = (char *)malloc(total);
    if (!cmd)
        return NULL;

    size_t pos = 0;
    for (int i = 0; argv[i]; i++) {
        if (i > 0)
            cmd[pos++] = ' ';
        pos += append_arg(cmd + pos, argv[i]);
    }
    cmd[pos] = '\0';
    return cmd;
}

int pl_start(const char *const argv[], pl_process *proc)
{
    if (!argv || !argv[0] || !proc)
        return -1;

    /* CreateProcess может менять строку, поэтому нужен свой буфер */
    char *cmd = build_cmdline(argv);
    if (!cmd)
        return -1;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);

    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmd);
    if (!ok)
        return -1;   /* GetLastError() == ERROR_FILE_NOT_FOUND, если программы нет */

    CloseHandle(pi.hThread);
    proc->handle = (intptr_t)pi.hProcess;
    proc->pid = (long)pi.dwProcessId;
    return 0;
}

static int finish(pl_process *proc, int *exit_code)
{
    HANDLE h = (HANDLE)proc->handle;
    DWORD code = 0;
    BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    proc->handle = 0;
    if (!ok)
        return -1;
    if (exit_code)
        *exit_code = (int)code;
    return 0;
}

int pl_wait(pl_process *proc, int *exit_code)
{
    if (!proc || !proc->handle)
        return -1;
    if (WaitForSingleObject((HANDLE)proc->handle, INFINITE) != WAIT_OBJECT_0)
        return -1;
    return finish(proc, exit_code);
}

int pl_poll(pl_process *proc, int *exit_code)
{
    if (!proc || !proc->handle)
        return -1;
    DWORD r = WaitForSingleObject((HANDLE)proc->handle, 0);
    if (r == WAIT_TIMEOUT)
        return 0;
    if (r != WAIT_OBJECT_0)
        return -1;
    return finish(proc, exit_code) == 0 ? 1 : -1;
}

#else /* POSIX */

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Как узнать, что exec в ребёнке не удался: создаём канал с FD_CLOEXEC.
   Если exec прошёл, канал закроется сам и родитель прочитает 0 байт.
   Если нет - ребёнок запишет туда errno. */
int pl_start(const char *const argv[], pl_process *proc)
{
    if (!argv || !argv[0] || !proc)
        return -1;

    int fds[2];
    if (pipe(fds) < 0)
        return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    if (pid == 0) {
        /* ребёнок */
        close(fds[0]);
        execvp(argv[0], (char *const *)argv);
        int err = errno;
        ssize_t w = write(fds[1], &err, sizeof err);
        (void)w;
        _exit(127);
    }

    /* родитель */
    close(fds[1]);
    int child_err = 0;
    ssize_t n;
    do {
        n = read(fds[0], &child_err, sizeof child_err);
    } while (n < 0 && errno == EINTR);
    close(fds[0]);

    if (n == (ssize_t)sizeof child_err) {
        /* exec не удался, ребёнок уже вызвал _exit - забираем его, чтобы не было зомби */
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
            ;
        errno = child_err;
        return -1;
    }

    proc->handle = 0;
    proc->pid = (long)pid;
    return 0;
}

static int decode_status(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return -1;
}

int pl_wait(pl_process *proc, int *exit_code)
{
    if (!proc || proc->pid <= 0)
        return -1;

    int status;
    pid_t r;
    do {
        r = waitpid((pid_t)proc->pid, &status, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0)
        return -1;

    if (exit_code)
        *exit_code = decode_status(status);
    return 0;
}

int pl_poll(pl_process *proc, int *exit_code)
{
    if (!proc || proc->pid <= 0)
        return -1;

    int status;
    pid_t r;
    do {
        r = waitpid((pid_t)proc->pid, &status, WNOHANG);
    } while (r < 0 && errno == EINTR);

    if (r == 0)
        return 0;
    if (r < 0)
        return -1;

    if (exit_code)
        *exit_code = decode_status(status);
    return 1;
}

#endif
