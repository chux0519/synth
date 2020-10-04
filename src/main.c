#include <alsa/asoundlib.h>

#include "core.h"

int main(int argc, char *argv[]) {
  synth_ctx_t *ctx = synth_ctx_create(NULL, 1);
  synth_loop_start(ctx);
  synth_ctx_destroy(ctx);
  return 0;
}
