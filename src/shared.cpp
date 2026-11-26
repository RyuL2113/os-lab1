#include "shared.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static const uint32_t MAGIC = 0x4C414233;   // "LAB3"

static SharedData *data = nullptr;

#ifdef _WIN32

static HANDLE mutex = NULL;
static HANDLE mapping = NULL;

void shared_lock()
{
    // WAIT_ABANDONED - прежний владелец умер, не отпустив мьютекс; замок всё равно наш
    WaitForSingleObject(mutex, INFINITE);
}

void shared_unlock()
{
    ReleaseMutex(mutex);
}

bool shared_open()
{
    mutex = CreateMutexA(NULL, FALSE, "Local\\lab3_mutex");   // создать или открыть
    if (!mutex)
        return false;

    shared_lock();
    // INVALID_HANDLE_VALUE - память в файле подкачки, удалится вместе с последним описателем
    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                 0, sizeof(SharedData), "Local\\lab3_shm");
    if (mapping)
        data = (SharedData *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));
    if (!data) {
        if (mapping)
            CloseHandle(mapping);
        mapping = NULL;
        shared_unlock();
        CloseHandle(mutex);
        mutex = NULL;
        return false;
    }

    if (data->magic != MAGIC) {   // мы первые
        memset(data, 0, sizeof *data);
        data->magic = MAGIC;
    }
    data->attached++;
    shared_unlock();
    return true;
}

void shared_close()
{
    if (!data)
        return;
    shared_lock();
    data->attached--;
    shared_unlock();

    UnmapViewOfFile(data);
    data = nullptr;
    CloseHandle(mapping);
    CloseHandle(mutex);
    mapping = mutex = NULL;
}

#else /* POSIX */

static sem_t *sem = SEM_FAILED;

void shared_lock()
{
    while (sem_wait(sem) != 0 && errno == EINTR)
        ;
}

void shared_unlock()
{
    sem_post(sem);
}

bool shared_open()
{
    sem = sem_open("/lab3_sem", O_CREAT, 0600, 1);   // 1 = свободен
    if (sem == SEM_FAILED)
        return false;

    // общую память открываем и инициализируем под замком, иначе два процесса,
    // запущенные одновременно, оба решат, что они первые
    shared_lock();
    int fd = shm_open("/lab3_shm", O_CREAT | O_RDWR, 0600);
    if (fd >= 0 && ftruncate(fd, sizeof(SharedData)) == 0) {   // без ftruncate размер 0 -> SIGBUS
        void *p = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p != MAP_FAILED)
            data = (SharedData *)p;
    }
    if (fd >= 0)
        close(fd);   // отображение остаётся

    if (!data) {
        shared_unlock();
        sem_close(sem);
        sem = SEM_FAILED;
        return false;
    }

    if (data->magic != MAGIC) {
        memset(data, 0, sizeof *data);
        data->magic = MAGIC;
    }
    data->attached++;
    shared_unlock();
    return true;
}

void shared_close()
{
    if (!data)
        return;
    shared_lock();
    data->attached--;
    if (data->attached <= 0)
        shm_unlink("/lab3_shm");   // последний - удаляем имя
    shared_unlock();

    munmap(data, sizeof(SharedData));
    data = nullptr;
    // sem_unlink не делаем: следующий процесс создал бы новый семафор с тем же именем,
    // и у работающих процессов замки оказались бы разные
    sem_close(sem);
    sem = SEM_FAILED;
}

#endif

SharedData *shared_data()
{
    return data;
}
