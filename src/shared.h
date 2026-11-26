#pragma once

#include <cstdint>

// То, что лежит в общей памяти. Менять только под shared_lock().
struct SharedData {
    uint32_t magic;       // признак, что память уже заполнена
    int32_t attached;     // сколько процессов подключено
    int64_t counter;      // счётчик
    int64_t leader_pid;   // PID ведущего, 0 - места нет
    int64_t leader_beat;  // когда ведущий последний раз отмечался, мс (mono_ms)
};

bool shared_open();   // создать или подключиться
void shared_close();
void shared_lock();
void shared_unlock();
SharedData *shared_data();
