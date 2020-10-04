#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <alsa/asoundlib.h>
#include <math.h>
#include <pthread.h>

#include "core.h"

int get_freq(int k) {
  double base = 110.0;  // A2
  double delta = pow(2.0, 1.0 / 12.0);
  double ret = base * pow(delta, (double)k);
  return (int)ret;
}

int main(int argc, char *argv[]) {
  synth_ctx_t *ctx = synth_ctx_create(NULL, 1, 1.0, 440, osc_square);

  pthread_t sound_thread;
  pthread_create(&sound_thread, NULL, (void *(*)(void *))synth_loop_start, ctx);

  Display *display;
  Window window;
  XEvent event;
  int s;

  display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "Cannot open display\n");
    exit(1);
  }
  XkbSetDetectableAutoRepeat(display, 1, NULL);
  s = DefaultScreen(display);
  window =
      XCreateSimpleWindow(display, RootWindow(display, s), 10, 10, 200, 200, 1,
                          BlackPixel(display, s), BlackPixel(display, s));

  XSelectInput(display, window, KeyPressMask | KeyReleaseMask);

  XMapWindow(display, window);

  printf(
      "|   |   |   |   |   | |   |   |   |   | |   | |   |   |   |\n"
      "|   | S |   |   | F | | G |   |   | J | | K | | L |   |   |\n"
      "|   |___|   |   |___| |___|   |   |___| |___| |___|   |   |__\n"
      "|     |     |     |     |     |     |     |     |     |     |\n"
      "|  Z  |  X  |  C  |  V  |  B  |  N  |  M  |  ,  |  .  |  /  |\n"
      "|_____|_____|_____|_____|_____|_____|_____|_____|_____|_____|\n\n");
  // z, s, x, c, f, v, g, b, n, j, m, k, \,, l, ., /
  const int sounds[16] = {52, 39, 53, 54, 41, 55, 42, 56,
                          57, 44, 58, 45, 59, 46, 60, 61};
  int cur_key = -1;
  int freq = 0;

  // soundio_outstream_pause(outstream, true);
  while (1) {
    XNextEvent(display, &event);

    if (event.xkey.keycode == 0x09) break;
    for (int i = 0; i < 16; i++) {
      if (sounds[i] == event.xkey.keycode) {
        if (event.type == KeyPress) {
          printf("KeyPress: %x\n", event.xkey.keycode);
          freq = get_freq(i);
        } else if (event.type == KeyRelease) {
          printf("KeyRelease: %x\n", event.xkey.keycode);
          freq = 0;
        }
      }
    }
    set_syth_ctx_freq(ctx, freq);
  }

  XCloseDisplay(display);

  int ret;
  pthread_join(sound_thread, (void **)&ret);

  synth_ctx_destroy(ctx);
  return 0;
}
