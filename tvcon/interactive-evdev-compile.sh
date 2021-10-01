#!/usr/bin/env bash

rm -f tools_interactive-sdl

XKB_TOOLS=/home/rett/dev/common-lisp/its/libxkbcommon
TV_CON_DIR=/home/rett/dev/common-lisp/its/its-skull-dev-remote/its/tools/tv11/tvcon

# cd $XKB_TOOLS/build

rm -f *.o
rm -f tools_interactive-sdl

gcc -Ixkbcli-interactive-sdl.p \
    -I$XKB_TOOLS/build \
    -I$XKB_TOOLS \
    -I$XKB_TOOLS/tools \
    -I$XKB_TOOLS/include \
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
   -MQ tools_interactive-sdl.c.o \
   -MF tools_interactive-sdl.c.o.d \
   -o tools_interactive-sdl.c.o \
   -c interactive-sdl.c

gcc -Ixkbcli-interactive-sdl.p \
    -I$XKB_TOOLS/build \
    -I$XKB_TOOLS \
    -I$XKB_TOOLS/tools \
    -I$XKB_TOOLS/include \
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

gcc -L/usr/lib/x86_64-linux-gnu \
    tools-common.c.o  \
    tools_interactive-sdl.c.o \
    -lxcb \
    -lxkbcommon  \
    -o tools_interactive-sdl

# cp tools_interactive-sdl \
#    /home/rett/dev/common-lisp/its/its-skull-dev-remote/its/tools/tv11/tvcon

# cd -
