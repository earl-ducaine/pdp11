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

#include "xkbcommon/xkbcommon.h"

#include "tools-common.h"
#include "interactive-sdl.h"

struct keyboard {
    struct xkb_state *state;
    struct xkb_compose_state *compose_state;
};

// static bool terminate;
static int evdev_offset = 8;
static bool report_state_changes;
static bool with_compose;
static enum xkb_consumed_mode consumed_mode = XKB_CONSUMED_MODE_XKB;

static int
keyboard_new(struct xkb_keymap *keymap,
             struct xkb_compose_table *compose_table, struct keyboard *out)
{
    int ret;
    struct xkb_state *state;
    struct xkb_compose_state *compose_state = NULL;

    state = xkb_state_new(keymap);
    if (!state) {
      fprintf(stderr, "Couldn't create xkb state\n");
        ret = -EFAULT;
        goto err_fd;
    }

    if (with_compose) {
        compose_state = xkb_compose_state_new(compose_table,
                                              XKB_COMPOSE_STATE_NO_FLAGS);
        if (!compose_state) {
	  fprintf(stderr, "Couldn't create compose state\n");
            ret = -EFAULT;
            goto err_state;
        }
    }

    out->state = state;
    out->compose_state = compose_state;
    return 0;

    xkb_compose_state_unref(compose_state);
err_state:
    xkb_state_unref(state);
err_fd:
    return ret;
}

static void get_keyboards(struct xkb_keymap *keymap,
		   struct xkb_compose_table *compose_table,
		   struct keyboard *kbd)
{
  int ret;

        ret = keyboard_new(keymap, compose_table, kbd);
        if (ret) {
	  fprintf(stderr, "Couldn't open Skipping.\n");
        }
}

/* The meaning of the input_event 'value' field. */
enum {
    KEY_STATE_RELEASE = 0,
    KEY_STATE_PRESS = 1,
    KEY_STATE_REPEAT = 2,
};

static void
process_event(struct keyboard *kbd, uint16_t type, uint16_t code, int32_t value)
{
    xkb_keycode_t keycode;
    struct xkb_keymap *keymap;
    enum xkb_state_component changed;
    enum xkb_compose_status status;

    if (type != EV_KEY)
        return;

    keycode = evdev_offset + code;
    keymap = xkb_state_get_keymap(kbd->state);

    if (value == KEY_STATE_REPEAT && !xkb_keymap_key_repeats(keymap, keycode))
        return;

    if (with_compose && value != KEY_STATE_RELEASE) {
        xkb_keysym_t keysym = xkb_state_key_get_one_sym(kbd->state, keycode);
        xkb_compose_state_feed(kbd->compose_state, keysym);
    }

    if (value != KEY_STATE_RELEASE)
        tools_print_keycode_state(kbd->state, kbd->compose_state, keycode,
                                  consumed_mode);

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

static int
read_keyboard(struct keyboard *kbd)
{
    struct input_event evs;

    /* No fancy error checking here. */
    evs.type = EV_KEY;
    evs.code = 10;
    evs.value = KEY_STATE_PRESS;
    process_event(kbd, evs.type, evs.code, evs.value);

    return 0;
}


static int
loop(struct keyboard *kbd)
{
    int ret = -1;

    ret = read_keyboard(kbd);
    ret = 0;
    return ret;
}


int
interactive_sdl(int argc, char *argv[])
{
    int ret = EXIT_FAILURE;
    // struct keyboard *kbds;
    struct xkb_context *ctx = NULL;
    struct xkb_keymap *keymap = NULL;
    struct xkb_compose_table *compose_table = NULL;
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

    ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
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
        keymap = xkb_keymap_new_from_file(ctx, file,
                                          XKB_KEYMAP_FORMAT_TEXT_V1,
                                          XKB_KEYMAP_COMPILE_NO_FLAGS);
        fclose(file);
    }
    else {
        struct xkb_rule_names rmlvo = {
            .rules = (rules == NULL || rules[0] == '\0') ? NULL : rules,
            .model = (model == NULL || model[0] == '\0') ? NULL : model,
            .layout = (layout == NULL || layout[0] == '\0') ? NULL : layout,
            .variant = (variant == NULL || variant[0] == '\0') ? NULL : variant,
            .options = (options == NULL || options[0] == '\0') ? NULL : options
        };

        if (!rules && !model && !layout && !variant && !options)
            keymap = xkb_keymap_new_from_names(ctx, NULL, 0);
        else
            keymap = xkb_keymap_new_from_names(ctx, &rmlvo, 0);

        if (!keymap) {
            fprintf(stderr,
                    "Failed to compile RMLVO: '%s', '%s', '%s', '%s', '%s'\n",
                    rules, model, layout, variant, options);
            goto out;
        }
    }

    if (!keymap) {
        fprintf(stderr, "Couldn't create xkb keymap\n");
        goto out;
    }

    if (with_compose) {
        locale = setlocale(LC_CTYPE, NULL);
        compose_table =
            xkb_compose_table_new_from_locale(ctx, locale,
                                              XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (!compose_table) {
            fprintf(stderr, "Couldn't create compose from locale\n");
            goto out;
        }
    }

    struct keyboard kbd;
    get_keyboards(keymap, compose_table, &kbd);
    // if (!kbds) {
    //     goto out;
    // }

    // act.sa_handler = sigintr_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    /* sigaction(SIGINT, &act, NULL); */
    /* sigaction(SIGTERM, &act, NULL); */

    tools_disable_stdin_echo();
    ret = loop(&kbd);
    tools_enable_stdin_echo();

    xkb_state_unref(kbd.state);
    xkb_compose_state_unref(kbd.compose_state);

    // keyboard_free(kbd);
out:
    xkb_compose_table_unref(compose_table);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);

    return ret;
}
