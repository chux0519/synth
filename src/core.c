#include "core.h"

#include <stdio.h>

int start_soundio(struct SynthContext *ctx) {
  int err;
  struct SoundIoOutStream *outstream = ctx->out;

  outstream->format = SoundIoFormatFloat32NE;
  outstream->write_callback = ctx->cb;
  outstream->software_latency = 0;

  struct SoundIo *soundio = ctx->io;
  if ((err = soundio_outstream_open(outstream))) {
    fprintf(stderr, "unable to open device: %s", soundio_strerror(err));
    return 1;
  }

  if (outstream->layout_error)
    fprintf(stderr, "unable to set channel layout: %s\n",
            soundio_strerror(outstream->layout_error));

  if ((err = soundio_outstream_start(outstream))) {
    fprintf(stderr, "unable to start device: %s", soundio_strerror(err));
    return 1;
  }

  while (1) soundio_wait_events(soundio);

  return 0;
}
