#include "platform.h"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <ctime>
#include <unistd.h>
#endif

int64_t mono_ms()
{
#ifdef _WIN32
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

void sleep_ms(int64_t ms)
{
    if (ms <= 0)
        return;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    // nanosleep прерывается сигналом - досыпаем остаток
    while (nanosleep(&ts, &ts) != 0)
        ;
#endif
}

void time_str(char *buf, size_t size)
{
#ifdef _WIN32
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(buf, size, "%04u-%02u-%02u %02u:%02u:%02u.%03u",
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);   // localtime() не потокобезопасна
    char date[32];
    strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &tm);
    snprintf(buf, size, "%s.%03ld", date, ts.tv_nsec / 1000000);
#endif
}

long my_pid()
{
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}

std::string self_path()
{
    char buf[4096];
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
    if (n == 0 || n >= sizeof buf)
        return std::string();
    return std::string(buf, n);
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0)
        return std::string();
    return std::string(buf, (size_t)n);
#endif
}
