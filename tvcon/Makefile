CFLAGS=-g -O3 `sdl2-config --cflags`
LDLIBS=`sdl2-config --libs` -lSDL2_net -lxkbcommon

all: tvcon interactive-sdl-main

tvcon.o:
tools-common.o:
interactive-sdl.o:
interactive-sdl-main.o:

tvcon: tvcon.o tools-common.o interactive-sdl.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

interactive-sdl-main: interactive-sdl-main.o tools-common.o interactive-sdl.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# tvcon: tvcon.c tools-common.c interactive-sdl.c 
#	cc $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f *.o tvcon interactive-sdl-main
