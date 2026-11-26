#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Монотонные часы в миллисекундах (для расписания таймера).
int64_t mono_ms();

// Пауза в миллисекундах.
void sleep_ms(int64_t ms);

// Текущие дата и время с миллисекундами: "2026-11-25 21:30:05.123".
void time_str(char *buf, size_t size);

// PID текущего процесса.
long my_pid();

// Полный путь к своему исполняемому файлу.
std::string self_path();
