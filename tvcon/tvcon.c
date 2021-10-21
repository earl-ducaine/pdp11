#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>

#include <unistd.h>
#include <sys/types.h>

#include <SDL.h>
#include <SDL_net.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <locale.h>

// Note, this is a special Linux Kernal file documenting the event
// codes of several user input devices, notably the general Linux user
// keyboard. Typically it can be found with other linux header files
// at: /usr/include/linux/input-event-codes.h depending of course on
// how your specific distribution is set up.
// 
// The .h file consists of defines (i.e. #define) and comments only,
// therefor it has no dependancies. C defines are removed very seldom,
// or ever. So, it's not vital that it be in sync with the version of
// Linux that this application runs on, or indeed even that it's
// running on linux.
#include "input-event-codes.h"

#include "../args.h"
#include "interactive-sdl.h"

typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;
typedef enum xkb_state_component state_component_enum;

#define nil NULL


#define WIDTH 576
#define HEIGHT 454
#define EVDEV_OFFSET 8

char *argv0;

int scale = 1;
int ctrlslock = 0;
int modmap = 0;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *screentex;
const uint8 *keystate;
uint32 fb[WIDTH*HEIGHT];
uint32 *finalfb;
uint32 fg = 0x4AFF0000; // Phosphor P39, peak at 525nm.
uint32 bg = 0x00000000;
TCPsocket sock;
int backspace = 017; /* Knight key code for BS. */
uint32 userevent;
int updatebuf = 1;
int updatescreen = 1;

uint8 largebuf[64*1024];

void
log_to_file(char *log_string);

enum {
	/* TV to 11 */
	MSG_KEYDN = 0,
	MSG_GETFB,

	/* 11 to TV */
	MSG_FB,
	MSG_WD,
	MSG_CLOSE,
};

void
panic(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
	SDL_Quit();
}

uint16
b2w(uint8 *b)
{
	return b[0] | b[1]<<8;
}

void
w2b(uint8 *b, uint16 w)
{
	// Log key presses
	if (b == (largebuf + 3)) {
		char message_buffer[1024];
		memset(message_buffer, '\0', 1024);
		sprintf(message_buffer, "w2b: %i\n", w);
		log_to_file(message_buffer);
	}

	b[0] = w;
	b[1] = w>>8;
}

void
msgheader(uint8 *b, uint8 type, uint16 length)
{
	w2b(b, length);
	b[2] = type;
}

TCPsocket
dial(char *host, int port)
{
	IPaddress address;
	TCPsocket s;
	if (SDLNet_ResolveHost (&address, host, port) == -1)
		panic("Error resolving host name %s: %s\n",
		      host, SDLNet_GetError());
	s = SDLNet_TCP_Open (&address);
	if (s == 0)
		panic("Error connecting to %s: %s\n",
		      host, SDLNet_GetError());
	return s;
}

void
updatefb(void)
{
	int x, y;
	int i;
	uint32 c;
	int stride;
	uint32 *src, *dst;

	if(scale == 1){
		memcpy(finalfb, fb, WIDTH*HEIGHT*sizeof(uint32));
		return;
	}
	stride = WIDTH*scale;
	src = fb;
	dst = finalfb;
	for(y = 0; y < HEIGHT; y++){
		for(x = 0; x < WIDTH; x++)
			for(i = 0; i < scale; i++)
				dst[x*scale + i] = src[x];
		for(i = 1; i < scale; i++){
			memcpy(dst+stride, dst, stride*sizeof(uint32));
			dst += stride;
		}
		src += WIDTH;
		dst += stride;
	}
}

SDL_Rect texrect;
uint32 screenmodes[2] = { 0, SDL_WINDOW_FULLSCREEN_DESKTOP };
int fullscreen;

void
resize(void)
{
	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	//	printf("resize %d %d\n", w, h);
	texrect.x = (w-1024)/2;
	texrect.y = (h-1024)/2;

	SDL_Event ev;
	SDL_memset(&ev, 0, sizeof(SDL_Event));
	ev.type = userevent;
	SDL_PushEvent(&ev);
}

void
stretch(SDL_Rect *r)
{
	int w, h;
	SDL_GetRendererOutputSize(renderer, &w, &h);
	if ((double)h / texrect.h < (double)w / texrect.w){
		r->w = texrect.w * h / texrect.h;
		r->h = h;
		r->x = (w - r->w) / 2;
		r->y = 0;
	}else{
		r->w = w;
		r->h = texrect.h * w / texrect.w;
		r->x = 0;
		r->y = (h - r->h) / 2;
	}
}

void
draw(void)
{
	if(updatebuf){
		updatebuf = 0;
		updatefb();
		SDL_UpdateTexture(screentex, nil,
				  finalfb, WIDTH*scale*sizeof(uint32));
		updatescreen = 1;
	}
	if(updatescreen){
		SDL_Rect screenrect;
		stretch(&screenrect);
		updatescreen = 0;
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, screentex, nil, &screenrect);
		SDL_RenderPresent(renderer);
	}
}

int
writen(TCPsocket s, void *data, int n)
{
	if (SDLNet_TCP_Send (s, data, n) < n)
		return -1;
	return 0;
}

int
readn(TCPsocket s, void *data, int n)
{
	int m;

	/* The documentation claims a successful call to SDLNet_TCP_Recv
	 * should always return n, and that anything less is an error.
	 * This doesn't appear to be true, so this loop is necessary
	 * to collect the full buffer. */

	while(n > 0){
		m = SDLNet_TCP_Recv (s, data, n);
		if(m <= 0)
			return -1;
		data += m;
		n -= m;
	}

	return 0;
}

/* Map SDL scancodes to Knight keyboard codes as best we can */
unsigned int scancodemap[SDL_NUM_SCANCODES];

void
initkeymap(void)
{
	int i;
	for(i = 0; i < SDL_NUM_SCANCODES; i++)
		scancodemap[i] = -1;

	scancodemap[SDL_SCANCODE_F12] = 000;	/* BREAK */
	scancodemap[SDL_SCANCODE_F2] = 001;	/* ESC */
	scancodemap[SDL_SCANCODE_1] = 002;
	scancodemap[SDL_SCANCODE_2] = 003;
	scancodemap[SDL_SCANCODE_3] = 004;
	scancodemap[SDL_SCANCODE_4] = 005;
	scancodemap[SDL_SCANCODE_5] = 006;
	scancodemap[SDL_SCANCODE_6] = 007;
	scancodemap[SDL_SCANCODE_7] = 010;
	scancodemap[SDL_SCANCODE_8] = 011;
	scancodemap[SDL_SCANCODE_9] = 012;
	scancodemap[SDL_SCANCODE_0] = 013;
	scancodemap[SDL_SCANCODE_MINUS] = 014;	/* - = */
	scancodemap[SDL_SCANCODE_EQUALS] = 015;	/* @ ` */
	scancodemap[SDL_SCANCODE_GRAVE] = 016;	/* ^ ~ */
	scancodemap[SDL_SCANCODE_BACKSPACE] = backspace;
	scancodemap[SDL_SCANCODE_F1] = 0020;	/* CALL */

	scancodemap[SDL_SCANCODE_F4] = 0021;	/* CLEAR */
	scancodemap[SDL_SCANCODE_TAB] = 022;
	scancodemap[SDL_SCANCODE_ESCAPE] = 023;	/* ALT MODE */
	scancodemap[SDL_SCANCODE_Q] = 024;
	scancodemap[SDL_SCANCODE_W] = 025;
	scancodemap[SDL_SCANCODE_E] = 026;
	scancodemap[SDL_SCANCODE_R] = 027;
	scancodemap[SDL_SCANCODE_T] = 030;
	scancodemap[SDL_SCANCODE_Y] = 031;
	scancodemap[SDL_SCANCODE_U] = 032;
	scancodemap[SDL_SCANCODE_I] = 033;
	scancodemap[SDL_SCANCODE_O] = 034;
	scancodemap[SDL_SCANCODE_P] = 035;
	scancodemap[SDL_SCANCODE_LEFTBRACKET] = 036;
	scancodemap[SDL_SCANCODE_RIGHTBRACKET] = 037;
	scancodemap[SDL_SCANCODE_BACKSLASH] = 040;
	// / inf
	// +- delta
	// O+ gamma

	// FORM
	// VTAB
	scancodemap[SDL_SCANCODE_DELETE] = 046;	/* RUBOUT */
	scancodemap[SDL_SCANCODE_A] = 047;
	scancodemap[SDL_SCANCODE_S] = 050;
	scancodemap[SDL_SCANCODE_D] = 051;
	scancodemap[SDL_SCANCODE_F] = 052;
	scancodemap[SDL_SCANCODE_G] = 053;
	scancodemap[SDL_SCANCODE_H] = 054;
	scancodemap[SDL_SCANCODE_J] = 055;
	scancodemap[SDL_SCANCODE_K] = 056;
	scancodemap[SDL_SCANCODE_L] = 057;
	scancodemap[SDL_SCANCODE_SEMICOLON] = 060;	/* ; + */
	scancodemap[SDL_SCANCODE_APOSTROPHE] = 061;	/* : * */
	scancodemap[SDL_SCANCODE_RETURN] = 062;
	// LINE FEED
	scancodemap[SDL_SCANCODE_F3] = 064;		/* next, back */

	scancodemap[SDL_SCANCODE_Z] = 065;
	scancodemap[SDL_SCANCODE_X] = 066;
	scancodemap[SDL_SCANCODE_C] = 067;
	scancodemap[SDL_SCANCODE_V] = 070;
	scancodemap[SDL_SCANCODE_B] = 071;
	scancodemap[SDL_SCANCODE_N] = 072;
	scancodemap[SDL_SCANCODE_M] = 073;
	scancodemap[SDL_SCANCODE_COMMA] = 074;
	scancodemap[SDL_SCANCODE_PERIOD] = 075;
	scancodemap[SDL_SCANCODE_SLASH] = 076;
	scancodemap[SDL_SCANCODE_SPACE] = 077;
}


int16_t sdl_to_xkb_scancodemap[KEY_CNT];


void
init_sdl_to_xkb_scancodemap(void)
{
	int i;
	for(i = 0; i <= KEY_MAX; i++)
		sdl_to_xkb_scancodemap[i] = -1;

	sdl_to_xkb_scancodemap[SDL_SCANCODE_F12] = KEY_F12;	/* BREAK */
	sdl_to_xkb_scancodemap[SDL_SCANCODE_F2] = KEY_F2;	/* ESC */
	sdl_to_xkb_scancodemap[SDL_SCANCODE_1] = KEY_1;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_2] = KEY_2;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_3] = KEY_3;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_4] = KEY_4;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_5] = KEY_5;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_6] = KEY_6;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_7] = KEY_7;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_8] = KEY_8;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_9] = KEY_9;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_0] = KEY_0;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_MINUS] = KEY_MINUS;	/* - = */
	sdl_to_xkb_scancodemap[SDL_SCANCODE_EQUALS] = KEY_EQUAL;	/* @ ` */
	sdl_to_xkb_scancodemap[SDL_SCANCODE_GRAVE] = KEY_GRAVE;	/* ^ ~ */
	sdl_to_xkb_scancodemap[SDL_SCANCODE_BACKSPACE] = KEY_BACKSPACE;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_F1] = KEY_F1;	/* CALL */

	sdl_to_xkb_scancodemap[SDL_SCANCODE_F4] = KEY_F4;	/* CLEAR */
	sdl_to_xkb_scancodemap[SDL_SCANCODE_TAB] = KEY_TAB;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_ESCAPE] = KEY_ESC; /* ALT MODE */
	sdl_to_xkb_scancodemap[SDL_SCANCODE_Q] = KEY_Q;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_W] = KEY_W;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_E] = KEY_E;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_R] = KEY_R;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_T] = KEY_T;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_Y] = KEY_Y;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_U] = KEY_U;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_I] = KEY_I;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_O] = KEY_O;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_P] = KEY_P;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_LEFTBRACKET] = KEY_LEFTBRACE;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_RIGHTBRACKET] = KEY_RIGHTBRACE;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_BACKSLASH] = KEY_BACKSLASH;
	// / inf
	// +- delta
	// O+ gamma

	// FORM
	// VTAB
	sdl_to_xkb_scancodemap[SDL_SCANCODE_DELETE] = KEY_DELETE;	/* RUBOUT */
	sdl_to_xkb_scancodemap[SDL_SCANCODE_A] = KEY_A;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_S] = KEY_S;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_D] = KEY_D;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_F] = KEY_F;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_G] = KEY_G;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_H] = KEY_H;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_J] = KEY_J;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_K] = KEY_K;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_L] = KEY_L;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_SEMICOLON] = KEY_SEMICOLON;	/* ; + */
	sdl_to_xkb_scancodemap[SDL_SCANCODE_APOSTROPHE] = KEY_APOSTROPHE;	/* : * */
	sdl_to_xkb_scancodemap[SDL_SCANCODE_RETURN] = KEY_ENTER;

	sdl_to_xkb_scancodemap[SDL_SCANCODE_Z] = KEY_Z;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_X] = KEY_X;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_C] = KEY_C;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_V] = KEY_V;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_B] = KEY_B;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_N] = KEY_N;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_M] = KEY_M;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_COMMA] = KEY_COMMA;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_PERIOD] = KEY_DOT;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_SLASH] = KEY_SLASH;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_SPACE] = KEY_SPACE;

	sdl_to_xkb_scancodemap[SDL_SCANCODE_SPACE] = KEY_LEFTALT;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_SPACE] = KEY_SPACE;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_SPACE] = KEY_CAPSLOCK;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_SPACE] = KEY_RIGHTCTRL;
	sdl_to_xkb_scancodemap[SDL_SCANCODE_SPACE] = KEY_LEFTCTRL;

	sdl_to_xkb_scancodemap[SDL_SCANCODE_LGUI] = KEY_LEFTMETA;	
	sdl_to_xkb_scancodemap[SDL_SCANCODE_RGUI] = KEY_RIGHTMETA;	
	sdl_to_xkb_scancodemap[SDL_SCANCODE_APPLICATION] = KEY_COMPOSE;	
}


/* These bits are directly sent to the 11 */
enum {
	MOD_RSHIFT = 0100,
	MOD_LSHIFT = 0200,
	MOD_RTOP = 00400,
	MOD_LTOP = 01000,
	MOD_RCTRL = 02000,
	MOD_LCTRL = 04000,
	MOD_RMETA = 010000,
	MOD_LMETA = 020000,
	MOD_SLOCK = 040000,
};

#define MOD_SHIFT (MOD_LSHIFT | MOD_RSHIFT)
#define MOD_CTRL (MOD_LCTRL | MOD_RCTRL)

/* Map key symbols to Knight keyboard codes as best we can */
unsigned int symbolmap[128];

void
initsymbolmap(void)
{
	int i;
	for(i = 0; i < 128; i++)
		symbolmap[i] = -1;

	symbolmap[' '] = 077;
	symbolmap['!'] = 002 | MOD_LSHIFT;
	symbolmap['"'] = 003 | MOD_LSHIFT;
	symbolmap['#'] = 004 | MOD_LSHIFT;
	symbolmap['%'] = 006 | MOD_LSHIFT;
	symbolmap['$'] = 005 | MOD_LSHIFT;
	symbolmap['&'] = 007 | MOD_LSHIFT;
	symbolmap['\''] = 010 | MOD_LSHIFT;
	symbolmap['('] = 011 | MOD_LSHIFT;
	symbolmap[')'] = 012 | MOD_LSHIFT;
	symbolmap['*'] = 061 | MOD_LSHIFT;
	symbolmap['+'] = 060 | MOD_LSHIFT;
	symbolmap[','] = 074;
	symbolmap['-'] = 014;
	symbolmap['.'] = 075;
	symbolmap['/'] = 076;
	symbolmap['0'] = 013;
	symbolmap['1'] = 002;
	symbolmap['2'] = 003;
	symbolmap['3'] = 004;
	symbolmap['4'] = 005;
	symbolmap['5'] = 006;
	symbolmap['6'] = 007;
	symbolmap['7'] = 010;
	symbolmap['8'] = 011;
	symbolmap['9'] = 012;
	symbolmap[':'] = 061;
	symbolmap[';'] = 060;
	symbolmap['<'] = 074 | MOD_LSHIFT;
	symbolmap['='] = 014 | MOD_LSHIFT;
	symbolmap['>'] = 075 | MOD_LSHIFT;
	symbolmap['?'] = 076 | MOD_LSHIFT;
	symbolmap['@'] = 015;
	symbolmap['A'] = 047 | MOD_LSHIFT;
	symbolmap['B'] = 071 | MOD_LSHIFT;
	symbolmap['C'] = 067 | MOD_LSHIFT;
	symbolmap['D'] = 051 | MOD_LSHIFT;
	symbolmap['E'] = 026 | MOD_LSHIFT;
	symbolmap['F'] = 052 | MOD_LSHIFT;
	symbolmap['G'] = 053 | MOD_LSHIFT;
	symbolmap['H'] = 054 | MOD_LSHIFT;
	symbolmap['I'] = 033 | MOD_LSHIFT;
	symbolmap['J'] = 055 | MOD_LSHIFT;
	symbolmap['K'] = 056 | MOD_LSHIFT;
	symbolmap['L'] = 057 | MOD_LSHIFT;
	symbolmap['M'] = 073 | MOD_LSHIFT;
	symbolmap['N'] = 072 | MOD_LSHIFT;
	symbolmap['O'] = 034 | MOD_LSHIFT;
	symbolmap['P'] = 035 | MOD_LSHIFT;
	symbolmap['Q'] = 024 | MOD_LSHIFT;
	symbolmap['R'] = 027 | MOD_LSHIFT;
	symbolmap['S'] = 050 | MOD_LSHIFT;
	symbolmap['T'] = 030 | MOD_LSHIFT;
	symbolmap['U'] = 032 | MOD_LSHIFT;
	symbolmap['V'] = 070 | MOD_LSHIFT;
	symbolmap['W'] = 025 | MOD_LSHIFT;
	symbolmap['X'] = 066 | MOD_LSHIFT;
	symbolmap['Y'] = 031 | MOD_LSHIFT;
	symbolmap['Z'] = 065 | MOD_LSHIFT;
	symbolmap['['] = 036;
	symbolmap['\\'] = 040;
	symbolmap[']'] = 037;
	symbolmap['^'] = 016;
	symbolmap['_'] = 013 | MOD_LSHIFT;
	symbolmap['`'] = 015 | MOD_LSHIFT;
	symbolmap['a'] = 047;
	symbolmap['b'] = 071;
	symbolmap['c'] = 067;
	symbolmap['d'] = 051;
	symbolmap['e'] = 026;
	symbolmap['f'] = 052;
	symbolmap['g'] = 053;
	symbolmap['h'] = 054;
	symbolmap['i'] = 033;
	symbolmap['j'] = 055;
	symbolmap['k'] = 056;
	symbolmap['l'] = 057;
	symbolmap['m'] = 073;
	symbolmap['n'] = 072;
	symbolmap['o'] = 034;
	symbolmap['p'] = 035;
	symbolmap['q'] = 024;
	symbolmap['r'] = 027;
	symbolmap['s'] = 050;
	symbolmap['t'] = 030;
	symbolmap['u'] = 032;
	symbolmap['v'] = 070;
	symbolmap['w'] = 025;
	symbolmap['x'] = 066;
	symbolmap['y'] = 031;
	symbolmap['z'] = 065;
	symbolmap['{'] = 036 | MOD_LSHIFT;
	symbolmap['|'] = 040 | MOD_LSHIFT;
	symbolmap['}'] = 037 | MOD_LSHIFT;
	symbolmap['~'] = 016 | MOD_LSHIFT;
}

int
texty_ignore(int key)
{
	return 0;
}

int (*texty)(int) = texty_ignore;

int curmod;

void
textinput(char *text)
{
	int key;

	if (text[0] >= 128)
		return;

	key = symbolmap[text[0]];
	if(key < 0)
		return;

	// Add in modifiers except shift, which comes from the table.
	key |= curmod & ~MOD_SHIFT;

	msgheader(largebuf, MSG_KEYDN, 3);
	w2b(largebuf+3, key);
	writen(sock, largebuf, 5);
}

/* Return true if this key will come as a TextInput event.*/
int
texty_symbol(int key)
{
	// Control characters don't generate TextInput.
	/* if(curmod & MOD_CTRL) */
        /* 	return 0; */

	// Nor do these function keys.
	switch(key){
	case SDL_SCANCODE_F1:
	case SDL_SCANCODE_F2:
	case SDL_SCANCODE_F3:
	case SDL_SCANCODE_F4:
	case SDL_SCANCODE_F5:
	case SDL_SCANCODE_F6:
	case SDL_SCANCODE_F7:
	case SDL_SCANCODE_F8:
	case SDL_SCANCODE_F9:
	case SDL_SCANCODE_F10:
	case SDL_SCANCODE_F11:
	case SDL_SCANCODE_F12:
	case SDL_SCANCODE_TAB:
	case SDL_SCANCODE_ESCAPE:
	case SDL_SCANCODE_DELETE:
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_BACKSPACE:
        	return 0;
	}

	// Plain letters, numbers, and symbols do.
	return 1;
}

int use_scancode = 1;

char *logfile_name = "/home/rett/dev/common-lisp/its/its/tvcon-keylog.log";
FILE *fd = 0;

void
log_to_file(char *log_string)
{
	if (!fd) {
		fd = fopen(logfile_name, "w+");
	}
	int results = fputs(log_string, fd);
	// Since we're an error handler, not much we can do except ignore.
	if (results != EOF) {
		fflush(fd);
	}
}

SDL_Scancode
provide_scancode(SDL_Keysym keysym)
{
	SDL_Scancode computed_scancode = SDL_GetScancodeFromKey(keysym.sym);
	char char_buffer[1024];
	memset(char_buffer, 0, 1024);
	// Danger! buffer overflow.
	sprintf(char_buffer, "Computed scancode: %i, keysym.scancode: %i\n",
		computed_scancode,
		keysym.scancode);
	// log_to_file(char_buffer);
	return use_scancode ? keysym.scancode : computed_scancode;
}

void
keydown(SDL_Keysym keysym, Uint8 repeat, char c)
{
	int key;
	SDL_Scancode scancode = provide_scancode(keysym);

	if(ctrlslock && scancode == SDL_SCANCODE_CAPSLOCK)
		scancode = SDL_SCANCODE_LCTRL;

	if(scancode == SDL_SCANCODE_F8){
		fullscreen = !fullscreen;
		SDL_SetWindowFullscreen(window, screenmodes[fullscreen]);
		resize();
	}

	if(modmap){
		/* Map RALT to TOP and ignore windows key */
		switch(scancode){
		case SDL_SCANCODE_LSHIFT: curmod |= MOD_LSHIFT; break;
		case SDL_SCANCODE_RSHIFT: curmod |= MOD_RSHIFT; break;
		case SDL_SCANCODE_LCTRL: curmod |= MOD_LCTRL; break;
		case SDL_SCANCODE_RCTRL: curmod |= MOD_RCTRL; break;
		case SDL_SCANCODE_LALT: curmod |= MOD_LMETA; break;
		case SDL_SCANCODE_RALT: curmod |= MOD_RTOP; break;
		}
		if(keystate[SDL_SCANCODE_LGUI] || keystate[SDL_SCANCODE_RGUI])
			return;
	}else
		switch(scancode){
		case SDL_SCANCODE_LSHIFT: curmod |= MOD_LSHIFT; break;
		case SDL_SCANCODE_RSHIFT: curmod |= MOD_RSHIFT; break;
		case SDL_SCANCODE_LGUI: curmod |= MOD_LTOP; break;
		case SDL_SCANCODE_RGUI: curmod |= MOD_RTOP; break;
		case SDL_SCANCODE_LCTRL: curmod |= MOD_LCTRL; break;
		case SDL_SCANCODE_RCTRL: curmod |= MOD_RCTRL; break;
		case SDL_SCANCODE_LALT: curmod |= MOD_LMETA; break;
		case SDL_SCANCODE_RALT: curmod |= MOD_RMETA; break;
		}

	if(scancode == SDL_SCANCODE_F11 && !repeat){
		uint32 f = SDL_GetWindowFlags(window) &
			SDL_WINDOW_FULLSCREEN_DESKTOP;
		SDL_SetWindowFullscreen(window,
					f ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
	}

	// Some, but not all, keys come as both KeyboardEvent and
	// TextInput. Ignore the latter kind here.
	if(texty(scancode) && !(curmod & MOD_CTRL)) {
		printf("unmodded text\n");
		return;
	} else if (texty(scancode) && (curmod & MOD_CTRL)) {
		// symbolmap characters are only ever modified with
		// MOD_LSHIFT, this strips that out, giving us an
		// effective mapping of character to PDP-11
		// keycode. Note that, for example, 'a' and 'A' will
		// both have the keycode 047.
		key = (symbolmap[c] & ~((unsigned int) MOD_LSHIFT));
		printf("Control modified c %d, symbolmap[c] %d; ~MOD_LSHIFT %d\n",
		       c,
		       symbolmap[c],
		       ~((unsigned int) MOD_LSHIFT));
	} else {
		// key = scancodemap[scancode];
		key = scancodemap[SDL_GetScancodeFromKey(keysym.sym)];
		printf("misc. modifier key %d\n", key);
	}

	if(key < 0)
		return;

	key |= curmod;

	msgheader(largebuf, MSG_KEYDN, 3);
	w2b(largebuf+3, key);
	writen(sock, largebuf, 5);
}

void
keyup(SDL_Keysym keysym)
{
	SDL_Scancode scancode = use_scancode ?
		SDL_GetScancodeFromKey(keysym.sym) :
		keysym.scancode;

	if(ctrlslock && scancode == SDL_SCANCODE_CAPSLOCK)
		scancode = SDL_SCANCODE_LCTRL;

	if(modmap)
		/* Map RALT to TOP and ignore windows key */
		switch(scancode){
		case SDL_SCANCODE_LSHIFT: curmod &= ~MOD_LSHIFT; break;
		case SDL_SCANCODE_RSHIFT: curmod &= ~MOD_RSHIFT; break;
		case SDL_SCANCODE_LCTRL: curmod &= ~MOD_LCTRL; break;
		case SDL_SCANCODE_RCTRL: curmod &= ~MOD_RCTRL; break;
		case SDL_SCANCODE_LALT: curmod &= ~MOD_LMETA; break;
		case SDL_SCANCODE_RALT: curmod &= ~MOD_RTOP; break;
		case SDL_SCANCODE_CAPSLOCK: curmod ^= MOD_SLOCK; break;
		}
	else
		switch(scancode){
		case SDL_SCANCODE_LSHIFT: curmod &= ~MOD_LSHIFT; break;
		case SDL_SCANCODE_RSHIFT: curmod &= ~MOD_RSHIFT; break;
		case SDL_SCANCODE_LGUI: curmod &= ~MOD_LTOP; break;
		case SDL_SCANCODE_RGUI: curmod &= ~MOD_RTOP; break;
		case SDL_SCANCODE_LCTRL: curmod &= ~MOD_LCTRL; break;
		case SDL_SCANCODE_RCTRL: curmod &= ~MOD_RCTRL; break;
		case SDL_SCANCODE_LALT: curmod &= ~MOD_LMETA; break;
		case SDL_SCANCODE_RALT: curmod &= ~MOD_RMETA; break;
		case SDL_SCANCODE_CAPSLOCK: curmod ^= MOD_SLOCK; break;
		}
	//	printf("up: %d %o %o\n", scancode, scancodemap[scancode], curmod);
}

void
dumpbuf(uint8 *b, int n)
{
	while(n--)
		printf("%o ", *b++);
	printf("\n");
}

void
unpackfb(uint8 *src, int x, int y, int w, int h)
{
	int i, j;
	uint32 *dst;
	uint16 wd;

	dst = &fb[y*WIDTH + x];
	for(i = 0; i < h; i++){
		for(j = 0; j < w; j++){
			if(j%16 == 0){
				wd = b2w(src);
				if(keystate[SDL_SCANCODE_F5] && wd != 0)
					printf("%d,%d: %o\n", i, j, wd);
				src += 2;
			}
			dst[j] = wd&0100000 ? fg : bg;
			wd <<= 1;
		}
		dst += WIDTH;
	}
	updatebuf = 1;
}

void
getupdate(uint16 addr, uint16 wd)
{
	int j;
	uint32 *dst;
	dst = &fb[addr*16];
	for(j = 0; j < 16; j++){
		dst[j] = wd&0100000 ? fg : bg;
		wd <<= 1;
	}
	updatebuf = 1;
}

void
getfb(void)
{
	uint8 *b;
	int x, y, w, h;

	x = 0;
	y = 0;
	w = WIDTH;
	h = HEIGHT;

	b = largebuf;
	msgheader(b, MSG_GETFB, 9);
	b += 3;
	w2b(b, x);
	w2b(b+2, y);
	w2b(b+4, w);
	w2b(b+6, h);
	writen(sock, largebuf, 11);
}

void
getdpykbd(void)
{
	uint8 buf[2];
	if(readn(sock, buf, 2) == -1){
		fprintf(stderr, "protocol botch\n");
		return;
	}
	printf("%o %o\n", buf[0], buf[1]);
}

int
readthread(void *arg)
{
	uint16 len;
	uint8 *b;
	uint8 type;
	int x, y, w, h;
	SDL_Event ev;

	SDL_memset(&ev, 0, sizeof(SDL_Event));
	ev.type = userevent;

	while(readn(sock, &len, 2) != -1){
		len = b2w((uint8*)&len);
		b = largebuf;
		readn(sock, b, len);
		type = *b++;
		switch(type){
		case MSG_FB:
			x = b2w(b);
			y = b2w(b+2);
			w = b2w(b+4);
			h = b2w(b+6);
			b += 8;
			unpackfb(b, x*16, y, w*16, h);
			SDL_PushEvent(&ev);
			break;

		case MSG_WD:
			getupdate(b2w(b), b2w(b+2));
			SDL_PushEvent(&ev);
			break;

		case MSG_CLOSE:
			SDLNet_TCP_Close(sock);
			exit(0);

		default:
			fprintf(stderr, "unknown msg type %d\n", type);
		}
	}
	printf("connection hung up\n");
	exit(0);
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-c bg,fg] [-2] [-B] [-C] [-p port] server\n", argv0);
	fprintf(stderr, "\t-c: set background and foreground color (in hex)\n");
	fprintf(stderr, "\t-2: scale 2x\n");
	fprintf(stderr, "\t-B: map backspace to rubout\n");
	fprintf(stderr, "\t-C: map shift lock to control\n");
	fprintf(stderr, "\t-S: map keys by according to symbols\n");
	fprintf(stderr, "\t-M: map RALT to TOP and ignore windows keys\n");
	fprintf(stderr, "\t-p: tv11 port; default 11100\n");
	fprintf(stderr, "\tserver: host running tv11\n");
	exit(0);
}



// Keyboard Events
//
// To map physical keyboard key presses to a simulated Knight
// keyboard, its sufficient to use SDL's SDL_KEYDOWN and SDL_KEYUP
// events. SDL provides other events about what's being typed on the
// keyboard, e.g. SDL_TextEditingEvent and SDL_TextInputEvent, but for
// our purpes these aren't needed, and combining those events can
// introduce subtle timing errors unless great care is taken.
//
// SDL's key SDL_KEYUP and SDL_KEYDOWN provide both information on the
// physical key that was pressed (SDL_Scancode) and on how the
// operating system has interpreted that key based on the current
// keymap (SDL_Keycode also refered to as 'key symbol'). But it should
// be noted that neither of them correspond exactly to the various
// levels of key mapping in X11's xkb system (e.g. geometry, keycodes,
// rules, symbols, etc.)
//
// Conceptually what we want to do is to translate SDL key symbols
// into physical operations on a standard keyboard and then apply a
// mapping of a modern day PC keyboard to a virtual Knight keyboard
// and emulate the bits that keyboard would have produced. Both of
// these tasks are relatively straight-forward and can be accomplished
// using just the information produced by SDL_KEYUP and SDL_KEYDOWN
// event and the fields sym (SDL_Keycode) and mod (SDL_Keymod) from
// the SDL_Keysym structure attached to those events.
//
// Translating SDL Key Symbols into Physical Keyboard Opperations
//
// SDL's notion of a key is somewhat different than xkb. In xkb an
// symbol can be mapped to any an key plus modifiers combination. For
// example, 'b' could be mapped to CTRL+ALT+AC01 where AC01 is the
// physical location of the key to the immediate right of the 'Caps'
// key, labeled 'A' on the standard PC keyboard. SDL doesn't allow
// that level of flexibility, either with scancodes or with key
// symbols. In particular SDL considers all alphebetic keys to be
// either lowercase when not modified by KMOD_SHIFT, or uppercase when
// KMOD_SHIFT is set in the mod field. However, all non-alphabetic
// symbols (.e.g. '&', '1' or even the modifier KMOD_LCTRL) are
// considered their own phisical key. So, as an example '!' is not
// KMOD_SHIFT+1, but rather KMOD_SHIFT+!. The practical result of this
// is that we ignore KMOD_LSHIFT and KMOD_RSHIFT key for all
// non-alphabetic key events, and synthesize the appropriate shift for
// all other keys as appropriate for the Knight keyboard, e.g.
//
// KMOD_LSHIFT+! => ! => SHIFT+1
//
// Hopefully this is only a pathological corner case, and would not
// effect real keyboard mappings, either international or alternate
// layouts like the Dvorak.

void
log_event(SDL_Event event) {
	switch (event.type) {
	case SDL_TEXTINPUT:  {
	        // Unsafe string handling! 
	        char buffer[1024];
		sprintf(buffer, "textinput: %s\n", event.text.text);
		log_to_file(buffer);
	}
		break;
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		{
			char message_buffer[1024];
			memset(message_buffer, '\0', 1024);
#define FORMAT_STRING "key symbol: %i, mod: %o\n"
			// Should we ignore on (event.key.repeat != 0) or
			// perhaps only acknowlege every 5th or 10th one, so
			// as to not overwhelm our PDP-11?
			sprintf(message_buffer,
				(event.type == SDL_KEYDOWN) ?
				"SDL_KEYDOWN, " FORMAT_STRING :
				"SDL_KEYUP, " FORMAT_STRING,
				event.key.keysym.sym,
				event.key.keysym.mod);
			log_to_file(message_buffer);
		}
		break;
	}
}


void
tools_print_state_changes_alt(state_component_enum changed)
{
	if (changed == 0)
		return;

	printf("changed [ ");
	if (changed & XKB_STATE_LAYOUT_EFFECTIVE)
		printf("effective-layout ");

	if (changed & XKB_STATE_LAYOUT_DEPRESSED)
		printf("depressed-layout ");

	if (changed & XKB_STATE_LAYOUT_LATCHED)
		printf("latched-layout ");

	if (changed & XKB_STATE_LAYOUT_LOCKED)
		printf("locked-layout ");

	if (changed & XKB_STATE_MODS_EFFECTIVE)
		printf("effective-mods ");

	if (changed & XKB_STATE_MODS_DEPRESSED)
		printf("depressed-mods ");

	if (changed & XKB_STATE_MODS_LATCHED)
		printf("latched-mods ");

	if (changed & XKB_STATE_MODS_LOCKED)
		printf("locked-mods ");

	if (changed & XKB_STATE_LEDS)
		printf("leds ");
	printf("]\n");
}




// text_input_text is char[32]
void
process_sdl_text_input(SDL_Event sdl_event,
		       char *sdl_text_input_text_ptr)
{
	memcpy(sdl_text_input_text_ptr, sdl_event.text.text, 32);
}


// Note, this should only be called when not composing, e.g.
// if (status != XKB_COMPOSE_COMPOSING && status != XKB_COMPOSE_CANCELLED)

void
get_sdl_keycode_from_unicode(struct xkb_state *state,
			     struct xkb_compose_state *compose_state,
			     xkb_keycode_t keycode,
			     char *s,
			     const xkb_keysym_t **xkb_keysyms_ptr_ptr,
			     xkb_keysym_t *xkb_keysym_ptr,
			     int *nsyms_ptr)
{
    struct xkb_keymap *keymap;

    xkb_keysym_t sym;
    xkb_layout_index_t layout;

    keymap = xkb_state_get_keymap(state);

    int nsyms_tmp = xkb_state_key_get_syms(state, keycode, xkb_keysyms_ptr_ptr);

    if (nsyms_tmp <= 0) {
	    *nsyms_ptr = 0;
	    return;
    }

    enum xkb_compose_status status = XKB_COMPOSE_NOTHING;
    if (compose_state)
        status = xkb_compose_state_get_status(compose_state);

    if (status == XKB_COMPOSE_COMPOSED) {
        sym = xkb_compose_state_get_one_sym(compose_state);
        nsyms_tmp = 1;
        xkb_compose_state_get_utf8(compose_state, s, 32);
    } else if (nsyms_tmp == 1) {
        sym = xkb_state_key_get_one_sym(state, keycode);
        *xkb_keysym_ptr = sym;
        xkb_state_key_get_utf8(state, keycode, s, sizeof(s));
    }

    return;
}


// Note the unicode buffer, chars, should be 32(?) bytes
void
process_sdl_keydown_event(struct xkb_state *xkb_state_ptr,
			  struct xkb_compose_state *xkb_compose_state_ptr,
			  uint8_t *chars,
			  state_component_enum *changed_out_ptr,
			  enum xkb_compose_status xkb_compose_status_in,
			  enum xkb_compose_status *xkb_compose_status_out_ptr,
			  SDL_Scancode sdl_scancode_in,
			  SDL_Scancode *sdl_scancode_out_ptr)
{
	xkb_keycode_t xkb_scancode =
		EVDEV_OFFSET + sdl_to_xkb_scancodemap[sdl_scancode_in];

	
	struct xkb_keymap *keymap = xkb_state_get_keymap(xkb_state_ptr);

	xkb_keysym_t xkb_keysym =
		xkb_state_key_get_one_sym(xkb_state_ptr, xkb_scancode);

	printf("xkb_scancode [%d], xkb_keysym [%d]\n",
	       xkb_scancode,
	       xkb_keysym);
	
	xkb_compose_state_feed(xkb_compose_state_ptr, xkb_keysym);

	enum xkb_compose_status xkb_compose_status_temp =
		xkb_compose_state_get_status(xkb_compose_state_ptr);
		
	if (xkb_compose_status_temp != XKB_COMPOSE_COMPOSING &&
	    xkb_compose_status_temp != XKB_COMPOSE_CANCELLED) {
		unsigned char s[32];
		int nsyms = 0;
		const xkb_keysym_t *xkb_keysyms_ptr_ptr;
		get_unicode_char(xkb_state_ptr,
				 xkb_compose_state_ptr,
				 xkb_scancode,
				 chars,
				 (const xkb_keysym_t **)&xkb_keysyms_ptr_ptr,
				 &xkb_keysym,
				 &nsyms);
		printf("Unicode s [%d]\n", s[0]);
	}

	*xkb_compose_status_out_ptr =
		xkb_compose_state_get_status(xkb_compose_state_ptr);

	// Not 100% clear (to me) whether whether
	// XKB_COMPOSE_COMPOSED, is really the end of the composition,
	// or if aditional keys of a chord could still be pressed. If
	// it is the end of a composition, how are multi chord keys
	// handled?
	if (*xkb_compose_status_out_ptr == XKB_COMPOSE_CANCELLED ||
	    *xkb_compose_status_out_ptr == XKB_COMPOSE_COMPOSED) {
		xkb_compose_state_reset(xkb_compose_state_ptr);
		printf("resetting xkb compose state.\n");
	}

	*changed_out_ptr = xkb_state_update_key(xkb_state_ptr,
						xkb_scancode,
						XKB_KEY_DOWN);
}

// text_input_text is char[32]
void
process_sdl_keyup(SDL_Scancode sdl_scancode,
		  struct xkb_state *xkb_state_ptr,
		  struct xkb_compose_state *xkb_compose_state_ptr,
		  state_component_enum *xkb_state_component_enum_out_ptr,
		  enum xkb_compose_status *xkb_compose_status_ptr)
{
	xkb_keysym_t *sym_ptr;
	uint16_t code;
	int32_t value;
	xkb_keycode_t xkb_scancode =
		EVDEV_OFFSET + sdl_to_xkb_scancodemap[sdl_scancode];
	
	*xkb_compose_status_ptr =
		xkb_compose_state_get_status(xkb_compose_state_ptr);

	// Not 100% clear (to me) whether whether
	// XKB_COMPOSE_COMPOSED, is really the end of the composition,
	// or if aditional keys of a chord could still be pressed. If
	// it is the end of a composition, how are multi chord keys
	// handled?
	if (*xkb_compose_status_ptr == XKB_COMPOSE_CANCELLED ||
	    *xkb_compose_status_ptr == XKB_COMPOSE_COMPOSED)
		xkb_compose_state_reset(xkb_compose_state_ptr);

	*xkb_state_component_enum_out_ptr =
		xkb_state_update_key(xkb_state_ptr,
				     xkb_scancode,
				     XKB_KEY_UP);
}


// return value 0 success -1 failure
struct xkb_keymap*
keymap_new(struct xkb_context *ctx_ptr)
{
	struct xkb_keymap *keymap_ptr = NULL;
	struct xkb_rule_names rmlvo = {
		.rules = NULL,
		.model = "pc105",
		.layout = "is",
		.variant = "dvorak",
		.options = "terminate:ctrl_alt_bksp"
	};	
	keymap_ptr = xkb_keymap_new_from_names(ctx_ptr, &rmlvo, 0);

	if (!keymap_ptr) {
		fprintf(stderr, "Failed to compile RMLVO");
		return NULL;
	} else {
		return keymap_ptr;
	}
}

int
main(int argc, char *argv[])
{
	SDL_Thread *th1;
	SDL_Event event;
	int running;
	char *p;
	int port;
	char *host;

	SDL_Init(SDL_INIT_EVERYTHING);
	SDLNet_Init();
	SDL_StopTextInput();

	if (interactive_sdl_init(0, NULL)) {
		printf("Unable to initialize xkb\n");
		exit(1);
	}

	struct xkb_context *ctx_ptr =
		xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!ctx_ptr) {
		fprintf(stderr, "Couldn't create xkb context\n");
		exit(1);
	}

	struct xkb_keymap *keymap_ptr = keymap_new(ctx_ptr);
	struct xkb_compose_table *xkb_compose_table_ptr = NULL;
	char *locale_ptr = setlocale(LC_CTYPE, NULL);
	
        xkb_compose_table_ptr =
		xkb_compose_table_new_from_locale(ctx_ptr,
						  locale_ptr,
						  XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (!xkb_compose_table_ptr) {
		fprintf(stderr, "Couldn't create compose from locale\n");
		exit(1);
        }

	struct xkb_state *xkb_state_ptr = NULL;
	struct xkb_compose_state *xkb_compose_state_ptr = NULL;

	if (init_keyboard(keymap_ptr,
			  xkb_compose_table_ptr,
			  &xkb_state_ptr,
			  &xkb_compose_state_ptr)) {
		printf("unable to initialize keyboard");
		exit(1);
	}	
	port = 11100;
	ARGBEGIN{
		case 'p':
			port = atoi(EARGF(usage()));
			break;
		case 'c':
			p = EARGF(usage());
			bg = strtol(p, &p, 16)<<8;
			if(*p++ != ',') usage();
			fg = strtol(p, &p, 16)<<8;
			if(*p++ != '\0') usage();
			break;
		case 'B':
			/* Backspace is Rubout. */
			backspace = 046;
			break;
		case 'C':
			ctrlslock++;
			break;
		case 'S':
			initsymbolmap();
			texty = texty_symbol;
			SDL_StartTextInput();
			break;
		case 'M':
			modmap++;
			break;
		case '2':
			scale++;
			break;
	}ARGEND;

	if(argc < 1)
		usage();

	host = argv[0];

	initkeymap();
	init_sdl_to_xkb_scancodemap();

	sock = dial(host, port);

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

	if(SDL_CreateWindowAndRenderer(WIDTH*scale, HEIGHT*scale, 0, &window, &renderer) < 0)
		panic("SDL_CreateWindowAndRenderer() failed: %s\n", SDL_GetError());
	SDL_SetWindowTitle(window, "Knight TV");

	screentex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
				      SDL_TEXTUREACCESS_STREAMING, WIDTH*scale, HEIGHT*scale);
	texrect.x = 0;
	texrect.y = 0;
	texrect.w = WIDTH*scale;
	texrect.h = HEIGHT*scale;

	keystate = SDL_GetKeyboardState(nil);

	userevent = SDL_RegisterEvents(1);

	int i;
	for(i = 0; i < WIDTH*HEIGHT; i++)
		fb[i] = bg;

	finalfb = malloc(WIDTH*scale*HEIGHT*scale*sizeof(uint32));

	getdpykbd();
	getfb();

	th1 = SDL_CreateThread(readthread, "Read thread", nil);

	running = 1;
	enum xkb_compose_status xkb_compose_status = 0;
	char sdl_text_input_text_ptr[32];
	unsigned char chars[32];
	memset(chars, '\0', sizeof(chars));

	while(running){
		if(SDL_WaitEvent(&event) < 0)
			panic("SDL_PullEvent() error: %s\n", SDL_GetError());

		switch(event.type) {
		case SDL_MOUSEBUTTONDOWN:
			break;
		case SDL_TEXTINPUT:
			memset((void*)sdl_text_input_text_ptr,
			       0,
			       sizeof(sdl_text_input_text_ptr));
			process_sdl_text_input(event, sdl_text_input_text_ptr);
			textinput(event.text.text);
			break;
		case SDL_KEYDOWN:
			memset(chars, '\0', sizeof(chars));
			if (!event.key.repeat) {
				state_component_enum state_component_enum;
				enum xkb_compose_status xkb_compose_status_out = 0;

				struct xkb_compose_state *
					xkb_compose_state_out_ptr;
				SDL_Keycode sdl_keycode = event.key.keysym.sym;
				SDL_Scancode sdl_scancode_in =
					event.key.keysym.scancode;
				SDL_Scancode sdl_scancode_out;
				/* enum xkb_state_component sdl_mod = */
				/* 	event.key.keysym.mod; */

				process_sdl_keydown_event(xkb_state_ptr,
							  xkb_compose_state_ptr,
							  chars,
							  &state_component_enum,
							  xkb_compose_status,
							  &xkb_compose_status_out,
							  sdl_scancode_in,
							  &sdl_scancode_out);

				printf("Post process_sdl_keydown_event chars %d\n",
					chars[0]);
				xkb_compose_status = xkb_compose_status_out;
			}
			keydown(event.key.keysym, event.key.repeat, chars[0]);
			break;

		case SDL_KEYUP:
			if (!event.key.repeat) {
				state_component_enum xkb_state_component_enum = 0;
				process_sdl_keyup(event.key.keysym.scancode,
						  xkb_state_ptr,
						  xkb_compose_state_ptr,
						  &xkb_state_component_enum,
						  &xkb_compose_status);
			}
			keyup(event.key.keysym);
			break;

		case SDL_QUIT:
			running = 0;
			break;

		case SDL_USEREVENT:
			/* framebuffer changed */
			draw();
			break;
		case SDL_WINDOWEVENT:
			switch(event.window.event){
			case SDL_WINDOWEVENT_MOVED:
			case SDL_WINDOWEVENT_ENTER:
			case SDL_WINDOWEVENT_LEAVE:
			case SDL_WINDOWEVENT_FOCUS_GAINED:
			case SDL_WINDOWEVENT_FOCUS_LOST:
#ifdef SDL_WINDOWEVENT_TAKE_FOCUS
			case SDL_WINDOWEVENT_TAKE_FOCUS:
#endif
				break;
			case SDL_WINDOWEVENT_RESIZED:
				texrect.x = (event.window.data1-WIDTH*scale)/2;
				texrect.y = (event.window.data2-HEIGHT*scale)/2;
				// fall through
			default:
				/* redraw */
				updatescreen = 1;
				draw();
				break;
			}
			break;
		}

	}
	// Close log file if we opened one.
	if (fd) {
		fclose(fd);
	}
	interactive_sdl_translate_destroy();
	return 0;
}


