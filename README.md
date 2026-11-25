# ЛР1. Среда разработки

Выполнила: Ли Анастасия Сангуновна

Основная система — Windows, в VirtualBox стоит Ubuntu 20.04. На обеих системах установлены
gcc, CMake, git и gdb (в Windows через MSYS2 UCRT64), репозиторий склонирован на обе.

## Что в папке

```
os-lab1/
├── CMakeLists.txt
├── src/main.cpp        Hello world
├── build.sh            сборка и запуск в Linux
├── build.cmd           сборка и запуск в Windows
├── .vscode/            tasks.json (Ctrl+Shift+B), launch.json (F5)
├── .gitignore          build/
└── .gitattributes      концы строк: *.sh - LF, *.cmd - CRLF
```

## Сборка

Linux:

```
./build.sh
```

Windows (cmd):

```
build.cmd
```

Скрипт делает `git pull --ff-only`, собирает проект в `build/` и запускает `hello`.
Если `build.sh` не запускается (Permission denied), то один раз в Linux:

```
chmod +x build.sh
git update-index --chmod=+x build.sh
```

## Отладка

В VS Code открыть папку проекта, поставить точку останова в `main.cpp`, нажать F5.
Путь к gdb в Windows: `C:/msys64/ucrt64/bin/gdb.exe`.

## Контрольные вопросы

1. `cmake -S . -B build` — конфигурация: CMake читает CMakeLists.txt и генерирует в `build`
   файлы для make (Makefile). `cmake --build build` — сама сборка, вызывает make в этой папке.
2. В `build` лежат сгенерированные файлы и бинарники. Они свои на каждой системе и
   пересоздаются при сборке, хранить их в git незачем, к тому же будут конфликты.
3. `git pull --ff-only` скачивает изменения и только перематывает ветку вперёд, без merge-коммита.
   Если локально есть свои коммиты, которых нет на сервере (ветки разошлись), он откажется.
4. Чтобы компилятор добавил отладочную информацию (`-g`) и не оптимизировал код —
   иначе gdb не покажет строки исходника и точки останова не сработают.
5. MinGW — порт gcc под Windows, собирает обычные exe, которые работают на WinAPI.
   Cygwin даёт слой совместимости POSIX (cygwin1.dll), программы без этой dll не запустятся.
6. Переходит в папку, где лежит сам скрипт, чтобы он работал при запуске из любой текущей папки.
