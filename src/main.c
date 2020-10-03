#include <X11/Xlib.h>
#include <math.h>
#include <pthread.h>
#include <soundio/soundio.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "core.h"

#define KEY_DELAY 30

atomic_int pitch = 0;

static long diff_time_ms(struct timeval a, struct timeval b) {
  long seconds = a.tv_sec - b.tv_sec;
  long micros = ((seconds * 1000000) + a.tv_usec - b.tv_usec);
  long millis = micros / 1000;
  return millis;
}

static const float PI = 3.1415926535f;
static float seconds_offset = 0.0f;
static void write_callback(struct SoundIoOutStream *outstream,
                           int frame_count_min, int frame_count_max) {
  const struct SoundIoChannelLayout *layout = &outstream->layout;
  float float_sample_rate = outstream->sample_rate;
  float seconds_per_frame = 1.0f / float_sample_rate;
  struct SoundIoChannelArea *areas;
  // int frames_left = frame_count_max;
  int frames_left = frame_count_max;
  int err;

  while (frames_left > 0) {
    int frame_count = frames_left;

    if ((err =
             soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
      fprintf(stderr, "%s\n", soundio_strerror(err));
      exit(1);
    }

    if (!frame_count) break;

    float radians_per_second = pitch * 2.0f * PI;
    for (int frame = 0; frame < frame_count; frame += 1) {
      float sample = 0.3 * sinf((seconds_offset + frame * seconds_per_frame) *
                                radians_per_second);
      for (int channel = 0; channel < layout->channel_count; channel += 1) {
        float *ptr =
            (float *)(areas[channel].ptr + areas[channel].step * frame);
        *ptr = sample;
      }
    }
    seconds_offset =
        fmodf(seconds_offset + seconds_per_frame * frame_count, 1.0f);

    if ((err = soundio_outstream_end_write(outstream))) {
      fprintf(stderr, "%s\n", soundio_strerror(err));
      exit(1);
    }

    frames_left -= frame_count;
  }
}

int main(int argc, char **argv) {
  // init sound
  int err;
  struct SoundIo *soundio = soundio_create();
  soundio->app_name = "synth";
  if (!soundio) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  if ((err = soundio_connect(soundio))) {
    fprintf(stderr, "error connecting: %s", soundio_strerror(err));
    return 1;
  }
  soundio_flush_events(soundio);
  int default_out_device_index = soundio_default_output_device_index(soundio);
  if (default_out_device_index < 0) {
    fprintf(stderr, "no output device found");
    return 1;
  }
  struct SoundIoDevice *device =
      soundio_get_output_device(soundio, default_out_device_index);
  if (!device) {
    fprintf(stderr, "out of memory");
    return 1;
  }

  fprintf(stderr, "Output device: %s\n", device->name);

  struct SoundIoOutStream *outstream = soundio_outstream_create(device);

  struct SynthContext ctx = {write_callback, soundio, outstream};

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_t sound_thread;
  pthread_create(&sound_thread, &attr, (void *(*)(void *))start_soundio,
                 (void *)&ctx);
  pthread_attr_destroy(&attr);

  // listen to keypress

  Display *display;
  Window window;
  XEvent event;
  int s;

  /* open connection with the server */
  display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "Cannot open display\n");
    exit(1);
  }

  s = DefaultScreen(display);

  /* create window */
  window =
      XCreateSimpleWindow(display, RootWindow(display, s), 10, 10, 200, 200, 1,
                          BlackPixel(display, s), BlackPixel(display, s));

  /* select kind of events we are interested in */
  XSelectInput(display, window, KeyPressMask | KeyReleaseMask);

  /* map (show) the window */
  XMapWindow(display, window);

  unsigned int k;
  struct timeval last_press, last_release;
  gettimeofday(&last_press, NULL);
  gettimeofday(&last_release, NULL);

  /* event loop */
  while (1) {
    XNextEvent(display, &event);

    /* keyboard events */
    if (event.type == KeyPress) {
      gettimeofday(&last_press, NULL);
      printf("KeyPress: %x\n", event.xkey.keycode);
      long delta = diff_time_ms(last_press, last_release);

      if (k != event.xkey.keycode || delta > KEY_DELAY) {
        printf("delta: %ld\n", delta);
        pitch = 440;
        soundio_outstream_pause(outstream, false);
      }
      /* exit on ESC key press */
      if (event.xkey.keycode == 0x09) break;
    } else if (event.type == KeyRelease) {
      k = event.xkey.keycode;
      gettimeofday(&last_release, NULL);
      printf("KeyRelease: %x\n", event.xkey.keycode);
      long delta = diff_time_ms(last_release, last_press);
      if (delta > KEY_DELAY) {
        printf("delta: %ld\n", delta);
        pitch = 0;
        soundio_outstream_pause(outstream, true);
      }
    }
  }

  /* close connection to server */
  XCloseDisplay(display);

  int ret;
  pthread_join(sound_thread, (void *)&ret);

  soundio_outstream_destroy(outstream);
  soundio_device_unref(device);
  soundio_destroy(soundio);
  return 0;
}
