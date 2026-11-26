#ifndef PROCLAUNCH_H
#define PROCLAUNCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    intptr_t handle;   /* Windows: HANDLE процесса, в POSIX не используется */
    long pid;          /* PID процесса */
} pl_process;

/* Запустить программу в фоне. argv[0] - имя программы, массив завершается NULL.
   Возвращает 0 при успехе, -1 при ошибке (в том числе если программы нет). */
int pl_start(const char *const argv[], pl_process *proc);

/* Дождаться завершения, код возврата кладётся в *exit_code.
   0 - успех, -1 - ошибка. */
int pl_wait(pl_process *proc, int *exit_code);

/* Проверить без ожидания: 1 - завершился (код в *exit_code), 0 - ещё работает, -1 - ошибка. */
int pl_poll(pl_process *proc, int *exit_code);

#ifdef __cplusplus
}
#endif

#endif
