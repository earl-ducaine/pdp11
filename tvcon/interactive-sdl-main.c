

#include <stdio.h>
#include <stdint.h>
#include <linux/input-event-codes.h>

#include <xkbcommon/xkbcommon-compose.h>

#include "interactive-sdl.h"


int
main(int argc, char *argv[])
{
  if (interactive_sdl_init(argc, argv)) {
    printf("Unable to initialize xkb\n");
    return 1;
  }
  else {
    interactive_sdl_translate(KEY_9, KEY_STATE_PRESS);
    interactive_sdl_translate(KEY_LEFTSHIFT, KEY_STATE_PRESS);
    interactive_sdl_translate(KEY_Q, KEY_STATE_PRESS);
    interactive_sdl_translate(KEY_LEFTSHIFT, KEY_STATE_RELEASE);
    interactive_sdl_translate(KEY_Q, KEY_STATE_PRESS);
    interactive_sdl_translate(KEY_Z, KEY_STATE_PRESS);
    interactive_sdl_translate(KEY_X, KEY_STATE_PRESS);
  }

  interactive_sdl_translate_destroy();
  
  return 0;
}
