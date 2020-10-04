#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

#include "core.h"

int main(int argc, char *argv[]) {
  synth_ctx_t *ctx = synth_ctx_create(NULL, 1, 0.3, 440);

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

  int key_a = 0x2a;
  // soundio_outstream_pause(outstream, true);
  while (1) {
    XNextEvent(display, &event);

    /* keyboard events */
    if (event.type == KeyPress) {
      printf("KeyPress: %x\n", event.xkey.keycode);
      set_syth_ctx_freq(ctx, 440);
      /* exit on ESC key press */
      if (event.xkey.keycode == 0x09) break;
    } else if (event.type == KeyRelease) {
      printf("KeyRelease: %x\n", event.xkey.keycode);
      set_syth_ctx_freq(ctx, 0);
    }
  }

  XCloseDisplay(display);

  int ret;
  pthread_join(sound_thread, (void **)&ret);

  synth_ctx_destroy(ctx);
  return 0;
}
