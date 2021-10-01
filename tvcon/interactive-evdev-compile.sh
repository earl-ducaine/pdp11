#!/usr/bin/env bash


XKB_TOOLS=/home/rett/dev/common-lisp/its/libxkbcommon
TV_CON_DIR=/home/rett/dev/common-lisp/its/its-skull-dev-remote/its/tools/tv11/tvcon

rm -f *.o
rm -f interactive-sdl

gcc -I. \
    -fdiagnostics-color=always \
    -pipe \
    -D_FILE_OFFSET_BITS=64 \
    -Wall -Winvalid-pch -Wextra \
    -std=c11 \
    -g \
    -fsanitize-undefined-trap-on-error \
    -Wpointer-arith \
    -Wmissing-declarations \
    -Wformat=2 \
    -Wstrict-prototypes \
    -Wmissing-prototypes \
    -Wnested-externs \
    -Wbad-function-cast \
    -Wshadow \
    -Wlogical-op \
    -Wdate-time \
    -Wwrite-strings \
    -MD \
    -MQ interactive-sdl.c.o \
    -MF interactive-sdl.c.o.d \
    -o interactive-sdl.c.o \
    -c interactive-sdl.c

gcc -I. \
    -fdiagnostics-color=always \
    -pipe \
    -D_FILE_OFFSET_BITS=64 \
    -Wall -Winvalid-pch -Wextra \
    -std=c11 \
    -g \
    -fsanitize-undefined-trap-on-error \
    -Wpointer-arith \
    -Wmissing-declarations \
    -Wformat=2 \
    -Wstrict-prototypes \
    -Wmissing-prototypes \
    -Wnested-externs \
    -Wbad-function-cast \
    -Wshadow \
    -Wlogical-op \
    -Wdate-time \
    -Wwrite-strings \
    -MD \
    -MQ tools-common.c.o \
    -MF tools-common.c.o.d \
    -o tools-common.c.o \
    -c tools-common.c

gcc -I. \
    -fdiagnostics-color=always \
    -pipe \
    -D_FILE_OFFSET_BITS=64 \
    -Wall -Winvalid-pch -Wextra \
    -std=c11 \
    -g \
    -fsanitize-undefined-trap-on-error \
    -Wpointer-arith \
    -Wmissing-declarations \
    -Wformat=2 \
    -Wstrict-prototypes \
    -Wmissing-prototypes \
    -Wnested-externs \
    -Wbad-function-cast \
    -Wshadow \
    -Wlogical-op \
    -Wdate-time \
    -Wwrite-strings \
    -MD \
    -MQ interactive-sdl-main.c.o \
    -MF interactive-sdl-main.c.o.d \
    -o interactive-sdl-main.c.o \
    -c interactive-sdl-main.c







gcc -L/usr/lib/x86_64-linux-gnu \
    tools-common.c.o  \
    interactive-sdl.c.o \
    interactive-sdl-main.c.o \
    -lxkbcommon  \
    -o interactive-sdl
