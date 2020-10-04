#pragma once

#include <alsa/asoundlib.h>
#include <stdatomic.h>

typedef struct _alsa_ctx {
  snd_pcm_t *handle;
  snd_pcm_hw_params_t *hwparams;
  snd_pcm_sw_params_t *swparams;
  snd_pcm_channel_area_t *areas;
  snd_pcm_sframes_t buffer_size;
  snd_pcm_sframes_t period_size;
} alsa_ctx_t;

typedef struct _synth_ctx {
  alsa_ctx_t alsa;
  unsigned short *samples;
  int channels;
  _Atomic int freq;
  double volume;
} synth_ctx_t;

synth_ctx_t *synth_ctx_create(const char *device_, int channels, double volume,
                              int freq);

void synth_loop_start(synth_ctx_t *ctx);

void synth_ctx_destroy(synth_ctx_t *ctx);
