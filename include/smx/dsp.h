#ifndef SMX_DSP_H_
#define SMX_DSP_H_

#include <SDL3/SDL.h>

/** Stereo frames the output ring holds before the oldest are dropped. */
#define SMX_DSP_OUT_FRAMES 4096u

/** Decoded samples a voice keeps for the interpolation the listener hears. */
#define SMX_DSP_HEARD_SAMPLES 32u

/**
 * How the samples the listener hears are interpolated. The hardware's
 * Gaussian filter softens the treble; cubic and sinc keep more of it.
 */
typedef enum SmxDspInterpolation {
  SMX_DSP_GAUSSIAN,
  SMX_DSP_CUBIC,
  SMX_DSP_SINC
} SmxDspInterpolation;

/** One of the eight voices. `buf` holds 12 decoded samples twice over. */
typedef struct SmxDspVoice {
  Sint16 buf[24];
  /** The same samples in decode order, further back, for `heard`. */
  Sint16 heard[SMX_DSP_HEARD_SAMPLES];
  Uint8 heard_head;
  Uint8 buf_pos;
  Sint32 interp_pos;
  Uint16 brr_addr;
  Uint8 brr_offset;
  Uint8 kon_delay;
  Uint8 env_mode;
  Sint32 env;
  Sint32 hidden_env;
  Uint8 envx_out;
} SmxDspVoice;

/**
 * S-DSP, run one SPC700 cycle at a time.
 * A sample takes 32 cycles. Each cycle performs the steps the hardware does
 * on that cycle: voices are staggered through the sample, echo reads and
 * writes go through SPC RAM, and envelopes, noise, and the KON/KOFF latches
 * advance on the shared rate counter. `ram` is the SPC700's 64 KiB, borrowed.
 * Fields named `t_` are values latched on one cycle for use on a later one.
 * Each finished sample is appended to `out` as an interleaved stereo frame.
 *
 * With `heard` other than SMX_DSP_GAUSSIAN, `out` is mixed from the same
 * voices interpolated that way, through the same volumes, and the echo as
 * the hardware computes it. Everything the SPC700 can see (OUTX, ENVX, pitch
 * modulation, and the echo buffer in its RAM) still comes from the Gaussian
 * filter, so the program runs as it would on the console.
 */
typedef struct SmxDsp {
  Uint8 *ram;
  Uint8 regs[128];
  SmxDspVoice voices[8];
  Uint8 phase;
  Sint32 counter;
  bool every_other;
  Uint8 kon;
  Uint8 new_kon;
  Uint8 endx_buf;
  Uint8 envx_buf;
  Uint8 outx_buf;
  Uint16 noise;
  Sint16 hist[16][2];
  Uint8 hist_pos;
  Uint16 echo_offset;
  Uint16 echo_length;
  Uint8 t_koff;
  Uint8 t_pmon;
  Uint8 t_non;
  Uint8 t_eon;
  Uint8 t_dir;
  Uint8 t_esa;
  Uint8 t_echo_flags;
  Uint8 t_srcn;
  Uint16 t_dir_addr;
  Uint16 t_brr_next_addr;
  Uint8 t_adsr0;
  Uint8 t_brr_header;
  Uint8 t_brr_byte;
  Uint8 t_looped;
  Sint32 t_pitch;
  Sint32 t_output;
  Uint16 t_echo_ptr;
  Sint32 t_main_out[2];
  Sint32 t_echo_out[2];
  Sint32 t_echo_in[2];
  SmxDspInterpolation heard;
  Sint32 t_heard_output;
  Sint32 t_heard_main[2];
  Sint16 out[SMX_DSP_OUT_FRAMES * 2u];
  Uint32 out_head;
  Uint32 out_count;
} SmxDsp;

/** Power-on state: registers clear except FLG at $E0 (reset, mute, no echo writes); noise is $4000.
 */
void smx_dsp_init(SmxDsp *dsp, Uint8 *ram);

Uint8 smx_dsp_read(const SmxDsp *dsp, Uint8 addr);

/** Writing ENDX clears it. KON is latched until the DSP next samples it. */
void smx_dsp_write(SmxDsp *dsp, Uint8 addr, Uint8 value);

/** Advance `cycles` SPC700 cycles. */
void smx_dsp_run(SmxDsp *dsp, Uint32 cycles);

/** Move up to `max` stereo frames, oldest first, into `frames`. Returns the count. */
Uint32 smx_dsp_drain(SmxDsp *dsp, Sint16 *frames, Uint32 max);

#endif /* SMX_DSP_H_ */
