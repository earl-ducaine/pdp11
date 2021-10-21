
/*
 * Copyright © 2012 Ran Benita <ran234@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input.h>

#include <xkbcommon/xkbcommon-compose.h>

#include "tools-common.h"
#include "interactive-sdl.h"

// static bool terminate;
static int evdev_offset = 8;
static bool report_state_changes;
static bool with_compose = true;
static enum xkb_consumed_mode consumed_mode = XKB_CONSUMED_MODE_XKB;

static int
keyboard_new(struct xkb_keymap *keymap,
             struct xkb_compose_table *compose_table,
	     keyboard *keyboard_out_ptr)
{
	struct xkb_state *xkb_state_out_ptr;
	struct xkb_compose_state *xkb_compose_state_ptr;
	init_keyboard(keymap,
		      compose_table,
		      &xkb_state_out_ptr,
		      &xkb_compose_state_ptr);
	keyboard_out_ptr->state = xkb_state_out_ptr;
	keyboard_out_ptr->compose_state = xkb_compose_state_ptr;
}


int
init_keyboard(struct xkb_keymap *xkb_keymap_ptr,
	      struct xkb_compose_table *compose_table,
	      struct xkb_state **xkb_state_out_ptr_ptr,
	      struct xkb_compose_state **compose_state_out_ptr_ptr)
{
	int ret = 0;
	struct xkb_compose_state *compose_state_tmp_ptr = NULL;

	struct xkb_state *xkb_state_tmp_ptr = xkb_state_new(xkb_keymap_ptr);
	if (!xkb_state_tmp_ptr) {
		fprintf(stderr, "Couldn't create xkb state\n");
		ret = -EFAULT;
		goto err_fd;
	}

	if (with_compose) {
		compose_state_tmp_ptr = xkb_compose_state_new(compose_table,
							      XKB_COMPOSE_STATE_NO_FLAGS);
		if (!compose_state_tmp_ptr) {
			fprintf(stderr, "Couldn't create compose state\n");
			ret = -EFAULT;
			goto err_state;
		}
	}

	*xkb_state_out_ptr_ptr = xkb_state_tmp_ptr;
	*compose_state_out_ptr_ptr = compose_state_tmp_ptr;
    
	return ret;
    
 err_state:
	xkb_state_unref(xkb_state_tmp_ptr);

 err_fd:
	return ret;
}

static void get_keyboards(struct xkb_keymap *keymap,
			  struct xkb_compose_table *compose_table,
			  keyboard *keyboard_ptr)
{
	int ret;

        ret = keyboard_new(keymap, compose_table, keyboard_ptr);
        if (ret) {
		fprintf(stderr, "Couldn't open Skipping.\n");
        }
}


// Note, this should only be called when not composing, e.g.
// if (status != XKB_COMPOSE_COMPOSING && status != XKB_COMPOSE_CANCELLED)

void
get_unicode_char(struct xkb_state *state,
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
        xkb_state_key_get_utf8(state, keycode, s, 32);
    }

    return;
}

// Last unicode characters retrieved. (How do we know no overflow?)
uint8_t g_s[16];


void
process_event(keyboard *kbd, uint16_t code, int32_t value)
{
	xkb_keycode_t keycode = evdev_offset + code;
	struct xkb_keymap *keymap;
	enum xkb_state_component changed;
	enum xkb_compose_status status;

	keymap = xkb_state_get_keymap(kbd->state);

	if (value == KEY_STATE_REPEAT && !xkb_keymap_key_repeats(keymap, keycode))
		return;

	if (value != KEY_STATE_RELEASE) {
		xkb_keysym_t keysym;
		if (with_compose) {
			keysym = xkb_state_key_get_one_sym(kbd->state, keycode);
			xkb_compose_state_feed(kbd->compose_state, keysym);
		}

		char s[32];
		xkb_keysym_t *keysym_ptr = &keysym;
		const xkb_keysym_t **keysyms = (const xkb_keysym_t **) &keysym_ptr;
		int nsyms;
		get_unicode_char(kbd->state,
				 kbd->compose_state,
				 keycode,
				 s,
				 keysyms,
				 &keysym,
				 &nsyms);
		
		tools_print_keycode_state(kbd->state, kbd->compose_state,
					  keycode, XKB_CONSUMED_MODE_XKB);
	}

	if (with_compose) {
		status = xkb_compose_state_get_status(kbd->compose_state);
		if (status == XKB_COMPOSE_CANCELLED || status == XKB_COMPOSE_COMPOSED)
			xkb_compose_state_reset(kbd->compose_state);
	}

	if (value == KEY_STATE_RELEASE)
		changed = xkb_state_update_key(kbd->state, keycode, XKB_KEY_UP);
	else
		changed = xkb_state_update_key(kbd->state, keycode, XKB_KEY_DOWN);

	if (report_state_changes)
		tools_print_state_changes(changed);
}


keyboard g_kbd;
struct xkb_context *g_ctx = NULL;
struct xkb_compose_table *g_compose_table = NULL;
struct xkb_keymap *g_keymap = NULL;

int
interactive_sdl_init(int argc, char *argv[])
{
    int ret = EXIT_FAILURE;
    // struct keyboard *kbds;
    const char *rules = NULL;
    const char *model = NULL;
    const char *layout = NULL;
    const char *variant = NULL;
    const char *options = NULL;
    const char *keymap_path = NULL;
    const char *locale;
    struct sigaction act;
    enum options {
        OPT_RULES,
        OPT_MODEL,
        OPT_LAYOUT,
        OPT_VARIANT,
        OPT_OPTION,
        OPT_KEYMAP,
        OPT_WITHOUT_X11_OFFSET,
        OPT_CONSUMED_MODE,
        OPT_COMPOSE,
        OPT_REPORT_STATE,
    };
    static struct option opts[] = {
        {"help",                 no_argument,            0, 'h'},
        {"rules",                required_argument,      0, OPT_RULES},
        {"model",                required_argument,      0, OPT_MODEL},
        {"layout",               required_argument,      0, OPT_LAYOUT},
        {"variant",              required_argument,      0, OPT_VARIANT},
        {"options",              required_argument,      0, OPT_OPTION},
        {"keymap",               required_argument,      0, OPT_KEYMAP},
        {"consumed-mode",        required_argument,      0, OPT_CONSUMED_MODE},
        {"enable-compose",       no_argument,            0, OPT_COMPOSE},
        {"report-state-changes", no_argument,            0, OPT_REPORT_STATE},
        {"without-x11-offset",   no_argument,            0, OPT_WITHOUT_X11_OFFSET},
        {0, 0, 0, 0},
    };

    setlocale(LC_ALL, "");

    while (1) {
        int opt;
        int option_index = 0;

        opt = getopt_long(argc, argv, "h", opts, &option_index);
        if (opt == -1)
            break;

        switch (opt) {
        case OPT_RULES:
            rules = optarg;
            break;
        case OPT_MODEL:
            model = optarg;
            break;
        case OPT_LAYOUT:
            layout = optarg;
            break;
        case OPT_VARIANT:
            variant = optarg;
            break;
        case OPT_OPTION:
            options = optarg;
            break;
        case OPT_KEYMAP:
            keymap_path = optarg;
            break;
        case OPT_WITHOUT_X11_OFFSET:
            evdev_offset = 0;
            break;
        case OPT_REPORT_STATE:
            report_state_changes = true;
            break;
        case OPT_COMPOSE:
            with_compose = true;
            break;
        case OPT_CONSUMED_MODE:
            if (strcmp(optarg, "gtk") == 0) {
                consumed_mode = XKB_CONSUMED_MODE_GTK;
            } else if (strcmp(optarg, "xkb") == 0) {
                consumed_mode = XKB_CONSUMED_MODE_XKB;
            } 
            break;
        }
    }

    g_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!g_ctx) {
        fprintf(stderr, "Couldn't create xkb context\n");
        goto out;
    }

    if (keymap_path) {
        FILE *file = fopen(keymap_path, "rb");
        if (!file) {
            fprintf(stderr, "Couldn't open '%s': %s\n",
                    keymap_path, strerror(errno));
            goto out;
        }
        g_keymap = xkb_keymap_new_from_file(g_ctx, file,
                                          XKB_KEYMAP_FORMAT_TEXT_V1,
                                          XKB_KEYMAP_COMPILE_NO_FLAGS);
        fclose(file);
    } else {
        struct xkb_rule_names rmlvo = {
            .rules = "evdev",
            .model = "pc105",
            .layout = "us",
            .variant = "dvorak",
            .options = NULL,
        };

        if (!rules && !model && !layout && !variant && !options)
            g_keymap = xkb_keymap_new_from_names(g_ctx, NULL, 0);
        else
            g_keymap = xkb_keymap_new_from_names(g_ctx, &rmlvo, 0);

        if (!g_keymap) {
            fprintf(stderr,
                    "Failed to compile RMLVO: '%s', '%s', '%s', '%s', '%s'\n",
                    rules, model, layout, variant, options);
            goto out;
        }
    }

    if (!g_keymap) {
        fprintf(stderr, "Couldn't create xkb keymap\n");
        goto out;
    }

    if (with_compose) {
        locale = setlocale(LC_CTYPE, NULL);
        g_compose_table =
            xkb_compose_table_new_from_locale(g_ctx, locale,
                                              XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (!g_compose_table) {
            fprintf(stderr, "Couldn't create compose from locale\n");
            goto out;
        }
    }

    get_keyboards(g_keymap, g_compose_table, &g_kbd);

    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    return 0;
    
 out:
    xkb_compose_table_unref(g_compose_table);
    xkb_keymap_unref(g_keymap);
    xkb_context_unref(g_ctx);
    return 1;
}

int
interactive_sdl_translate(uint16_t code, int32_t value)
{
	tools_disable_stdin_echo();
	process_event(&g_kbd, code, value);
	tools_enable_stdin_echo();

	return 0;
}

void
interactive_sdl_translate_destroy()
{
	xkb_state_unref(g_kbd.state);
	xkb_compose_state_unref(g_kbd.compose_state);
	xkb_compose_table_unref(g_compose_table);
	xkb_keymap_unref(g_keymap);
	xkb_context_unref(g_ctx);

	return;
}
