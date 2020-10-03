#pragma once
#include <soundio/soundio.h>

typedef void (*synth_cb)(struct SoundIoOutStream *, int frame_count_min,
                         int frame_count_max);

struct SynthContext {
  synth_cb cb;
  struct SoundIo *io;
  struct SoundIoOutStream *out;
};

int start_soundio(struct SynthContext *ctx);
