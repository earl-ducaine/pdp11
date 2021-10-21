

#ifndef INTERACTIVE_SDL_H
#define INTERACTIVE_SDL_H

typedef struct {
	struct xkb_keysym_t *keysym;
	struct xkb_state *state;
	struct xkb_compose_state *compose_state;
} keyboard;


/* The meaning of the input_event 'value' field. */
enum {
    KEY_STATE_RELEASE = 0,
    KEY_STATE_PRESS = 1,
    KEY_STATE_REPEAT = 2,
};

// 16 bytes long. Last unicode characters retrieved. (How do we know
// no overflow?)
extern uint8_t g_s[];

int
interactive_sdl_init(int argc, char *argv[]);

int
init_keyboard(struct xkb_keymap *xkb_keymap_ptr,
	      struct xkb_compose_table *compose_table,
	      struct xkb_state **xkb_state_out_ptr_ptr,
	      struct xkb_compose_state **compose_state_out_ptr_ptr);

int
interactive_sdl_translate(uint16_t code, int32_t value);

void
interactive_sdl_translate_destroy();

void
get_unicode_char(struct xkb_state *state,
		 struct xkb_compose_state *compose_state,
		 xkb_keycode_t keycode,
		 char *s,
		 const xkb_keysym_t **xkb_keysyms_ptr_ptr,
		 xkb_keysym_t *xkb_keysym_ptr,
		 int *nsyms_ptr);

#endif
