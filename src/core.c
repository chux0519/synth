#include "core.h"

#include <errno.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char default_device[] = "default";
static snd_pcm_format_t format = SND_PCM_FORMAT_S16;
static int resample = 1; /* enable alsa-lib resampling */
static int verbose = 1;
static int period_event = 0;      /* produce poll event after each period */
static unsigned int rate = 44100; /* stream rate */
static unsigned int buffer_time = 50000; /* ring buffer length in us */
static unsigned int period_time = 50000; /* period time in us */

double osc_sin(double phase) { return sin(phase); }
double osc_square(double phase) { return sin(phase) > 0 ? 1.0 : -1.0; }
double osc_triangle(double phase) { return asin(sin(phase)) * (2.0 / M_PI); }
double osc_saw_analogue(double phase) {
  double ret = 0.0;
  for (double n = 1.0; n < 40.0; n++) ret += (sin(n * phase)) / n;
  return ret * (2.0 / M_PI);
}

static int set_hwparams(synth_ctx_t *ctx) {
  unsigned int rrate;
  snd_pcm_uframes_t size;
  int err, dir;
  snd_pcm_t *handle = ctx->alsa.handle;
  snd_pcm_hw_params_t *params = ctx->alsa.hwparams;

  /* choose all parameters */
  err = snd_pcm_hw_params_any(handle, params);
  if (err < 0) {
    printf(
        "Broken configuration for playback: no configurations available: %s\n",
        snd_strerror(err));
    return err;
  }

  err = snd_pcm_hw_params_set_rate_resample(handle, params, resample);
  if (err < 0) {
    printf("Resampling setup failed for playback: %s\n", snd_strerror(err));
    return err;
  }

  err = snd_pcm_hw_params_set_access(handle, params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err < 0) {
    printf("Access type not available for playback: %s\n", snd_strerror(err));
    return err;
  }
  /* set the sample format */
  err = snd_pcm_hw_params_set_format(handle, params, format);
  if (err < 0) {
    printf("Sample format not available for playback: %s\n", snd_strerror(err));
    return err;
  }
  /* set the count of channels */
  err = snd_pcm_hw_params_set_channels(handle, params, ctx->channels);
  if (err < 0) {
    printf("Channels count (%u) not available for playbacks: %s\n",
           ctx->channels, snd_strerror(err));
    return err;
  }
  /* set the stream rate */
  rrate = rate;
  err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
  if (err < 0) {
    printf("Rate %uHz not available for playback: %s\n", rate,
           snd_strerror(err));
    return err;
  }
  if (rrate != rate) {
    printf("Rate doesn't match (requested %uHz, get %iHz)\n", rate, err);
    return -EINVAL;
  }
  /* set the buffer time */
  err = snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time,
                                               &dir);
  if (err < 0) {
    printf("Unable to set buffer time %u for playback: %s\n", buffer_time,
           snd_strerror(err));
    return err;
  }
  err = snd_pcm_hw_params_get_buffer_size(params, &size);
  if (err < 0) {
    printf("Unable to get buffer size for playback: %s\n", snd_strerror(err));
    return err;
  }
  ctx->alsa.buffer_size = size;
  /* set the period time */
  err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time,
                                               &dir);
  if (err < 0) {
    printf("Unable to set period time %u for playback: %s\n", period_time,
           snd_strerror(err));
    return err;
  }
  err = snd_pcm_hw_params_get_period_size(params, &size, &dir);
  if (err < 0) {
    printf("Unable to get period size for playback: %s\n", snd_strerror(err));
    return err;
  }
  ctx->alsa.period_size = size;
  /* write the parameters to device */
  err = snd_pcm_hw_params(handle, params);
  if (err < 0) {
    printf("Unable to set hw params for playback: %s\n", snd_strerror(err));
    return err;
  }
  return 0;
}

static int set_swparams(synth_ctx_t *ctx) {
  int err;
  snd_pcm_t *handle = ctx->alsa.handle;
  snd_pcm_sw_params_t *swparams = ctx->alsa.swparams;

  snd_pcm_sframes_t buffer_size = ctx->alsa.buffer_size;
  snd_pcm_sframes_t period_size = ctx->alsa.period_size;

  /* get the current swparams */
  err = snd_pcm_sw_params_current(handle, swparams);
  if (err < 0) {
    printf("Unable to determine current swparams for playback: %s\n",
           snd_strerror(err));
    return err;
  }
  /* start the transfer when the buffer is almost full: */
  /* (buffer_size / avail_min) * avail_min */
  err = snd_pcm_sw_params_set_start_threshold(
      handle, swparams, (buffer_size / period_size) * period_size);
  if (err < 0) {
    printf("Unable to set start threshold mode for playback: %s\n",
           snd_strerror(err));
    return err;
  }
  /* allow the transfer when at least period_size samples can be processed */
  /* or disable this mechanism when period event is enabled (aka interrupt like
   * style processing) */
  err = snd_pcm_sw_params_set_avail_min(
      handle, swparams, period_event ? buffer_size : period_size);
  if (err < 0) {
    printf("Unable to set avail min for playback: %s\n", snd_strerror(err));
    return err;
  }
  /* enable period events when requested */
  if (period_event) {
    err = snd_pcm_sw_params_set_period_event(handle, swparams, 1);
    if (err < 0) {
      printf("Unable to set period event: %s\n", snd_strerror(err));
      return err;
    }
  }
  /* write the parameters to the playback device */
  err = snd_pcm_sw_params(handle, swparams);
  if (err < 0) {
    printf("Unable to set sw params for playback: %s\n", snd_strerror(err));
    return err;
  }
  return 0;
}

static void generate_sine(synth_ctx_t *ctx, double *_phase, double freq) {
  snd_pcm_channel_area_t *areas = ctx->alsa.areas;
  int count = ctx->alsa.period_size;
  int offset = 0;
  int channels = ctx->channels;
  synth_osc_fn process = ctx->process;
  if (process == NULL) process = osc_sin;

  static double max_phase = 2. * M_PI;
  double phase = *_phase;
  double step = max_phase * freq / (double)rate;
  unsigned char *samples[channels];
  int steps[channels];
  unsigned int chn;
  int format_bits = snd_pcm_format_width(format);
  unsigned int maxval = ctx->volume * ((1 << (format_bits - 1)) - 1);
  int bps = format_bits / 8; /* bytes per sample */
  int phys_bps = snd_pcm_format_physical_width(format) / 8;
  int big_endian = snd_pcm_format_big_endian(format) == 1;
  int to_unsigned = snd_pcm_format_unsigned(format) == 1;
  /* verify and prepare the contents of areas */
  for (chn = 0; chn < channels; chn++) {
    if ((areas[chn].first % 8) != 0) {
      printf("areas[%u].first == %u, aborting...\n", chn, areas[chn].first);
      exit(EXIT_FAILURE);
    }
    samples[chn] = /*(signed short *)*/ (((unsigned char *)areas[chn].addr) +
                                         (areas[chn].first / 8));
    if ((areas[chn].step % 16) != 0) {
      printf("areas[%u].step == %u, aborting...\n", chn, areas[chn].step);
      exit(EXIT_FAILURE);
    }
    steps[chn] = areas[chn].step / 8;
    samples[chn] += offset * steps[chn];
  }
  /* fill the channel areas */
  while (count-- > 0) {
    union {
      float f;
      int i;
    } fval;
    int res, i;
    res = process(phase) * maxval;
    if (to_unsigned) res ^= 1U << (format_bits - 1);
    for (chn = 0; chn < channels; chn++) {
      /* Generate data in native endian format */
      if (big_endian) {
        for (i = 0; i < bps; i++)
          *(samples[chn] + phys_bps - 1 - i) = (res >> i * 8) & 0xff;
      } else {
        for (i = 0; i < bps; i++) *(samples[chn] + i) = (res >> i * 8) & 0xff;
      }
      samples[chn] += steps[chn];
    }
    phase += step;
    if (phase >= max_phase) phase -= max_phase;
  }
  *_phase = phase;
}

static int xrun_recovery(snd_pcm_t *handle, int err) {
  if (verbose) printf("stream recovery\n");
  if (err == -EPIPE) { /* under-run */
    err = snd_pcm_prepare(handle);
    if (err < 0)
      printf("Can't recovery from underrun, prepare failed: %s\n",
             snd_strerror(err));
    return 0;
  } else if (err == -ESTRPIPE) {
    while ((err = snd_pcm_resume(handle)) == -EAGAIN)
      sleep(1); /* wait until the suspend flag is released */
    if (err < 0) {
      err = snd_pcm_prepare(handle);
      if (err < 0)
        printf("Can't recovery from suspend, prepare failed: %s\n",
               snd_strerror(err));
    }
    return 0;
  }
  return err;
}
static int write_loop(synth_ctx_t *ctx) {
  double phase = 0;
  signed short *ptr;
  int err, cptr;

  snd_pcm_t *handle = ctx->alsa.handle;
  signed short *samples = (signed short *)ctx->samples;
  snd_pcm_sframes_t period_size = ctx->alsa.period_size;
  int channels = ctx->channels;
  int freq, last_freq;
  while (1) {
    freq = atomic_fetch_add_explicit(&ctx->freq, 0, memory_order_acquire);
    if (last_freq != freq) {
      printf("changed: %d -> %d\n", last_freq, freq);
    }
    generate_sine(ctx, &phase, (double)freq);
    ptr = samples;
    cptr = period_size;

    last_freq = freq;
    while (cptr > 0) {
      err = snd_pcm_writei(handle, ptr, cptr);
      if (err == -EAGAIN) continue;
      if (err < 0) {
        if (xrun_recovery(handle, err) < 0) {
          printf("Write error: %s\n", snd_strerror(err));
          exit(EXIT_FAILURE);
        }
        break; /* skip one period */
      }
      ptr += err * channels;
      cptr -= err;
    }
    last_freq = freq;
  }
}

synth_ctx_t *synth_ctx_create(const char *device_, int channels, double volume,
                              int freq, synth_osc_fn process) {
  int err;
  synth_ctx_t *ptr = malloc(sizeof(synth_ctx_t));
  ptr->channels = channels;
  ptr->freq = ATOMIC_VAR_INIT(freq);
  ptr->volume = volume;
  ptr->process = process;
  snd_pcm_hw_params_alloca(&ptr->alsa.hwparams);
  snd_pcm_sw_params_alloca(&ptr->alsa.swparams);

  const char *device = device_ == NULL ? default_device : device_;
  if ((err = snd_pcm_open(&ptr->alsa.handle, device, SND_PCM_STREAM_PLAYBACK,
                          0)) < 0) {
    printf("Playback open error: %s\n", snd_strerror(err));
    return NULL;
  }

  if ((err = set_hwparams(ptr)) < 0) {
    printf("Setting of hwparams failed: %s\n", snd_strerror(err));
    exit(EXIT_FAILURE);
  }

  if ((err = set_swparams(ptr)) < 0) {
    printf("Setting of swparams failed: %s\n", snd_strerror(err));
    exit(EXIT_FAILURE);
  }

  ptr->alsa.areas = calloc(channels, sizeof(snd_pcm_channel_area_t));
  ptr->samples = malloc((ptr->alsa.period_size * channels *
                         snd_pcm_format_physical_width(format)) /
                        8);
  for (int chn = 0; chn < channels; chn++) {
    ptr->alsa.areas[chn].addr = ptr->samples;
    ptr->alsa.areas[chn].first = chn * snd_pcm_format_physical_width(format);
    ptr->alsa.areas[chn].step =
        channels * snd_pcm_format_physical_width(format);
  }

  return ptr;
}

void synth_loop_start(synth_ctx_t *ctx) {
  int err = write_loop(ctx);
  if (err < 0) printf("Transfer failed: %s\n", snd_strerror(err));
}

void synth_ctx_destroy(synth_ctx_t *ctx) {
  if (ctx->alsa.areas) free(ctx->alsa.areas);
  if (ctx->samples) free(ctx->samples);
  if (ctx->alsa.handle) snd_pcm_close(ctx->alsa.handle);
  if (ctx) free(ctx);
}

void set_syth_ctx_freq(synth_ctx_t *ctx, int freq) {
  freq = freq < 0 ? 0 : freq;
  freq = freq > 5000 ? 5000 : freq;
  atomic_exchange_explicit(&ctx->freq, freq, memory_order_release);
}
