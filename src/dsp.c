#include "smx/dsp.h"

enum {
  SMX_DSP_VOLL = 0x00,
  SMX_DSP_PITCHL = 0x02,
  SMX_DSP_PITCHH = 0x03,
  SMX_DSP_SRCN = 0x04,
  SMX_DSP_ADSR0 = 0x05,
  SMX_DSP_ADSR1 = 0x06,
  SMX_DSP_GAIN = 0x07,
  SMX_DSP_ENVX = 0x08,
  SMX_DSP_OUTX = 0x09,
  SMX_DSP_MVOLL = 0x0C,
  SMX_DSP_EFB = 0x0D,
  SMX_DSP_FIR = 0x0F,
  SMX_DSP_EVOLL = 0x2C,
  SMX_DSP_PMON = 0x2D,
  SMX_DSP_NON = 0x3D,
  SMX_DSP_KON = 0x4C,
  SMX_DSP_EON = 0x4D,
  SMX_DSP_KOFF = 0x5C,
  SMX_DSP_DIR = 0x5D,
  SMX_DSP_FLG = 0x6C,
  SMX_DSP_ESA = 0x6D,
  SMX_DSP_ENDX = 0x7C,
  SMX_DSP_EDL = 0x7D
};

enum { SMX_ENV_RELEASE = 0, SMX_ENV_ATTACK = 1, SMX_ENV_DECAY = 2, SMX_ENV_SUSTAIN = 3 };

/* The counter cycles through 30720 values, one per sample. */
#define SMX_DSP_COUNTER_RANGE 30720

/* Samples between envelope or noise steps for each 5-bit rate. Rate 0 never fires. */
static const Uint16 smx_dsp_rates[32] = {SMX_DSP_COUNTER_RANGE + 1,
                                         2048,
                                         1536,
                                         1280,
                                         1024,
                                         768,
                                         640,
                                         512,
                                         384,
                                         320,
                                         256,
                                         192,
                                         160,
                                         128,
                                         96,
                                         80,
                                         64,
                                         48,
                                         40,
                                         32,
                                         24,
                                         20,
                                         16,
                                         12,
                                         10,
                                         8,
                                         6,
                                         5,
                                         4,
                                         3,
                                         2,
                                         1};

/* Rates in the same group of three fire on staggered samples. */
static const Uint16 smx_dsp_offsets[32] = {1,   0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040,
                                           536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040,
                                           536, 0, 1040, 536, 0, 1040, 0,   0};

/* Interpolation weights from the S-DSP's internal table. */
static const Sint16 smx_dsp_gauss[512] = {
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    2,    2,    2,    2,    2,
    2,    2,    3,    3,    3,    3,    3,    4,    4,    4,    4,    4,    5,    5,    5,    5,
    6,    6,    6,    6,    7,    7,    7,    8,    8,    8,    9,    9,    9,    10,   10,   10,
    11,   11,   11,   12,   12,   13,   13,   14,   14,   15,   15,   15,   16,   16,   17,   17,
    18,   19,   19,   20,   20,   21,   21,   22,   23,   23,   24,   24,   25,   26,   27,   27,
    28,   29,   29,   30,   31,   32,   32,   33,   34,   35,   36,   36,   37,   38,   39,   40,
    41,   42,   43,   44,   45,   46,   47,   48,   49,   50,   51,   52,   53,   54,   55,   56,
    58,   59,   60,   61,   62,   64,   65,   66,   67,   69,   70,   71,   73,   74,   76,   77,
    78,   80,   81,   83,   84,   86,   87,   89,   90,   92,   94,   95,   97,   99,   100,  102,
    104,  106,  107,  109,  111,  113,  115,  117,  118,  120,  122,  124,  126,  128,  130,  132,
    134,  137,  139,  141,  143,  145,  147,  150,  152,  154,  156,  159,  161,  163,  166,  168,
    171,  173,  175,  178,  180,  183,  186,  188,  191,  193,  196,  199,  201,  204,  207,  210,
    212,  215,  218,  221,  224,  227,  230,  233,  236,  239,  242,  245,  248,  251,  254,  257,
    260,  263,  267,  270,  273,  276,  280,  283,  286,  290,  293,  297,  300,  304,  307,  311,
    314,  318,  321,  325,  328,  332,  336,  339,  343,  347,  351,  354,  358,  362,  366,  370,
    374,  378,  381,  385,  389,  393,  397,  401,  405,  410,  414,  418,  422,  426,  430,  434,
    439,  443,  447,  451,  456,  460,  464,  469,  473,  477,  482,  486,  491,  495,  499,  504,
    508,  513,  517,  522,  527,  531,  536,  540,  545,  550,  554,  559,  563,  568,  573,  577,
    582,  587,  592,  596,  601,  606,  611,  615,  620,  625,  630,  635,  640,  644,  649,  654,
    659,  664,  669,  674,  678,  683,  688,  693,  698,  703,  708,  713,  718,  723,  728,  732,
    737,  742,  747,  752,  757,  762,  767,  772,  777,  782,  787,  792,  797,  802,  806,  811,
    816,  821,  826,  831,  836,  841,  846,  851,  855,  860,  865,  870,  875,  880,  884,  889,
    894,  899,  904,  908,  913,  918,  923,  927,  932,  937,  941,  946,  951,  955,  960,  965,
    969,  974,  978,  983,  988,  992,  997,  1001, 1005, 1010, 1014, 1019, 1023, 1027, 1032, 1036,
    1040, 1045, 1049, 1053, 1057, 1061, 1066, 1070, 1074, 1078, 1082, 1086, 1090, 1094, 1098, 1102,
    1106, 1109, 1113, 1117, 1121, 1125, 1128, 1132, 1136, 1139, 1143, 1146, 1150, 1153, 1157, 1160,
    1164, 1167, 1170, 1174, 1177, 1180, 1183, 1186, 1190, 1193, 1196, 1199, 1202, 1205, 1207, 1210,
    1213, 1216, 1219, 1221, 1224, 1227, 1229, 1232, 1234, 1237, 1239, 1241, 1244, 1246, 1248, 1251,
    1253, 1255, 1257, 1259, 1261, 1263, 1265, 1267, 1269, 1270, 1272, 1274, 1275, 1277, 1279, 1280,
    1282, 1283, 1284, 1286, 1287, 1288, 1290, 1291, 1292, 1293, 1294, 1295, 1296, 1297, 1297, 1298,
    1299, 1300, 1300, 1301, 1302, 1302, 1303, 1303, 1303, 1304, 1304, 1304, 1304, 1304, 1305, 1305,
};

/* Taps of the windowed sinc the listener can choose, and its phases between samples. */
#define SMX_DSP_SINC_TAPS 8u
#define SMX_DSP_SINC_PHASES 256u

static float smx_dsp_sinc_table[SMX_DSP_SINC_PHASES][SMX_DSP_SINC_TAPS];
static bool smx_dsp_sinc_ready = false;

static float smx_dsp_bessel_i0(float x) {
  float sum = 1.0f;
  float term = 1.0f;
  Uint32 k = 1;

  for (; k < 24u; k += 1) {
    term *= (x / (2.0f * (float)k)) * (x / (2.0f * (float)k));
    sum += term;
  }
  return sum;
}

/*
 * Kaiser-windowed sinc weights for taps at -3..4 around a point a fraction
 * `phase / SMX_DSP_SINC_PHASES` past tap 0, normalized to unity gain.
 */
static void smx_dsp_sinc_init(void) {
  const float beta = 5.0f;
  const float half = (float)SMX_DSP_SINC_TAPS / 2.0f;
  Uint32 phase = 0;

  for (; phase < SMX_DSP_SINC_PHASES; phase += 1) {
    float frac = (float)phase / (float)SMX_DSP_SINC_PHASES;
    float sum = 0.0f;
    Uint32 tap = 0;
    for (; tap < SMX_DSP_SINC_TAPS; tap += 1) {
      float x = (float)tap - (half - 1.0f) - frac;
      float ratio = x / half;
      float window = 0.0f;
      float sinc = 1.0f;
      if (ratio > -1.0f && ratio < 1.0f) {
        window =
            smx_dsp_bessel_i0(beta * SDL_sqrtf(1.0f - ratio * ratio)) / smx_dsp_bessel_i0(beta);
      }
      if (x != 0.0f) {
        sinc = SDL_sinf(SDL_PI_F * x) / (SDL_PI_F * x);
      }
      smx_dsp_sinc_table[phase][tap] = sinc * window;
      sum += sinc * window;
    }
    for (tap = 0; tap < SMX_DSP_SINC_TAPS; tap += 1) {
      smx_dsp_sinc_table[phase][tap] /= sum;
    }
  }
  smx_dsp_sinc_ready = true;
}

static Sint32 smx_dsp_clamp16(Sint32 value) {
  if (value > 32767) {
    return 32767;
  }
  if (value < -32768) {
    return -32768;
  }
  return value;
}

static Uint8 smx_dsp_vreg(const SmxDsp *dsp, Uint8 voice, Uint8 reg) {
  return dsp->regs[(Uint8)(voice * 0x10u + reg)];
}

static Uint8 smx_dsp_ram(const SmxDsp *dsp, Uint32 addr) {
  return dsp->ram[addr & 0xFFFFu];
}

static bool smx_dsp_fires(const SmxDsp *dsp, Uint8 rate) {
  return ((Uint32)dsp->counter + smx_dsp_offsets[rate]) % smx_dsp_rates[rate] == 0;
}

void smx_dsp_init(SmxDsp *dsp, Uint8 *ram) {
  Uint8 voice = 0;

  SDL_memset(dsp, 0, sizeof(*dsp));
  dsp->ram = ram;
  /* Reset holds FLG at $E0, so echo cannot write over the IPL's zero page. */
  dsp->regs[SMX_DSP_FLG] = 0xE0;
  dsp->noise = 0x4000;
  dsp->every_other = true;
  if (!smx_dsp_sinc_ready) {
    smx_dsp_sinc_init();
  }
  for (; voice < 8; voice += 1) {
    dsp->voices[voice].brr_offset = 1;
  }
}

Uint8 smx_dsp_read(const SmxDsp *dsp, Uint8 addr) {
  return dsp->regs[addr & 0x7Fu];
}

void smx_dsp_write(SmxDsp *dsp, Uint8 addr, Uint8 value) {
  addr = (Uint8)(addr & 0x7Fu);
  dsp->regs[addr] = value;
  if (addr == SMX_DSP_KON) {
    dsp->new_kon = value;
  } else if (addr == SMX_DSP_ENDX) {
    dsp->endx_buf = 0;
    dsp->regs[SMX_DSP_ENDX] = 0;
  }
}

static void smx_dsp_envelope(SmxDsp *dsp, Uint8 index) {
  SmxDspVoice *voice = &dsp->voices[index];
  Sint32 env = voice->env;
  Uint8 data = smx_dsp_vreg(dsp, index, SMX_DSP_ADSR1);
  Uint8 rate = 0;

  if (voice->env_mode == SMX_ENV_RELEASE) {
    env -= 0x8;
    voice->env = env < 0 ? 0 : env;
    return;
  }
  if ((dsp->t_adsr0 & 0x80) != 0) {
    if (voice->env_mode >= SMX_ENV_DECAY) {
      env -= 1;
      env -= env >> 8;
      rate = (Uint8)(data & 0x1F);
      if (voice->env_mode == SMX_ENV_DECAY) {
        rate = (Uint8)(((dsp->t_adsr0 >> 3) & 0x0E) + 0x10);
      }
    } else {
      rate = (Uint8)((dsp->t_adsr0 & 0x0F) * 2 + 1);
      env += rate < 31 ? 0x20 : 0x400;
    }
  } else {
    Uint8 mode = 0;
    data = smx_dsp_vreg(dsp, index, SMX_DSP_GAIN);
    mode = (Uint8)(data >> 5);
    if (mode < 4) {
      env = (Sint32)data * 0x10;
      rate = 31;
    } else {
      rate = (Uint8)(data & 0x1F);
      if (mode == 4) {
        env -= 0x20;
      } else if (mode == 5) {
        env -= 1;
        env -= env >> 8;
      } else {
        env += 0x20;
        /* Bent line: slow to +8 once the last envelope reached $600. */
        if (mode == 7 && (Uint32)voice->hidden_env >= 0x600) {
          env += 0x8 - 0x20;
        }
      }
    }
  }
  if ((env >> 8) == (data >> 5) && voice->env_mode == SMX_ENV_DECAY) {
    voice->env_mode = SMX_ENV_SUSTAIN;
  }
  voice->hidden_env = env;
  /* A linear decrease below zero also lands here through the unsigned compare. */
  if ((Uint32)env > 0x7FF) {
    env = env < 0 ? 0 : 0x7FF;
    if (voice->env_mode == SMX_ENV_ATTACK) {
      voice->env_mode = SMX_ENV_DECAY;
    }
  }
  if (smx_dsp_fires(dsp, rate)) {
    voice->env = env;
  }
}

/* Four BRR samples from the two nibble bytes, filtered against the previous two. */
static void smx_dsp_decode(SmxDsp *dsp, SmxDspVoice *voice) {
  Uint16 nybbles = (Uint16)(((Uint16)dsp->t_brr_byte << 8) |
                            smx_dsp_ram(dsp, (Uint32)voice->brr_addr + voice->brr_offset + 1u));
  Uint8 header = dsp->t_brr_header;
  Uint8 shift = (Uint8)(header >> 4);
  Uint8 filter = (Uint8)(header & 0x0C);
  Uint8 pos = voice->buf_pos;
  Uint8 i = 0;

  voice->buf_pos = (Uint8)(voice->buf_pos + 4u >= 12u ? 0 : voice->buf_pos + 4u);
  for (; i < 4; i += 1) {
    Sint32 sample = (Sint32)(Sint16)nybbles >> 12;
    Sint32 p1 = voice->buf[pos + i + 11u];
    Sint32 p2 = voice->buf[pos + i + 10u] >> 1;

    nybbles = (Uint16)(nybbles << 4);
    /* A multiply, since shifting a negative value left is undefined in C. */
    sample = (sample * ((Sint32)1 << shift)) >> 1;
    if (shift >= 0x0D) {
      sample = sample < 0 ? -2048 : 0;
    }
    if (filter == 0x04) {
      sample += p1 >> 1;
      sample += (-p1) >> 5;
    } else if (filter == 0x08) {
      sample += p1;
      sample -= p2;
      sample += p2 >> 4;
      sample += (p1 * -3) >> 6;
    } else if (filter == 0x0C) {
      sample += p1;
      sample -= p2;
      sample += (p1 * -13) >> 7;
      sample += (p2 * 3) >> 4;
    }
    sample = smx_dsp_clamp16(sample);
    sample = (Sint16)(sample * 2);
    voice->buf[pos + i] = (Sint16)sample;
    voice->buf[pos + i + 12u] = (Sint16)sample;
    voice->heard[voice->heard_head] = (Sint16)sample;
    voice->heard_head = (Uint8)((voice->heard_head + 1u) % SMX_DSP_HEARD_SAMPLES);
  }
}

static Sint32 smx_dsp_interpolate(const SmxDspVoice *voice) {
  Uint32 offset = (Uint32)(voice->interp_pos >> 4) & 0xFFu;
  const Sint16 *fwd = &smx_dsp_gauss[255u - offset];
  const Sint16 *rev = &smx_dsp_gauss[offset];
  const Sint16 *in = &voice->buf[(Uint32)(voice->interp_pos >> 12) + voice->buf_pos];
  Sint32 out = ((Sint32)fwd[0] * in[0]) >> 11;

  out += ((Sint32)fwd[256] * in[1]) >> 11;
  out += ((Sint32)rev[256] * in[2]) >> 11;
  /* The first three products wrap at 16 bits before the last one is added. */
  out = (Sint16)out;
  out += ((Sint32)rev[0] * in[3]) >> 11;
  return smx_dsp_clamp16(out) & ~1;
}

/*
 * Sample `k` of the four the Gaussian filter reads (0-3; others reach further
 * back or ahead) from the voice's decode history. The Gaussian's in[k] is the
 * sample 11 - i - k back from the newest, for i the whole part of the position.
 * Samples not yet decoded repeat the newest.
 */
static float smx_dsp_heard_sample(const SmxDspVoice *voice, Sint32 k) {
  Sint32 back = 11 - (Sint32)(voice->interp_pos >> 12) - k;

  if (back < 0) {
    back = 0;
  }
  return (float)voice->heard[(voice->heard_head + SMX_DSP_HEARD_SAMPLES - 1u - (Uint32)back) %
                             SMX_DSP_HEARD_SAMPLES];
}

/* The point between in[1] and in[2] the Gaussian filter samples, by `heard`. */
static Sint32 smx_dsp_heard_interpolate(const SmxDsp *dsp, const SmxDspVoice *voice) {
  float out = 0.0f;

  if (dsp->heard == SMX_DSP_CUBIC) {
    float t = (float)(voice->interp_pos & 0xFFF) / 4096.0f;
    float p0 = smx_dsp_heard_sample(voice, 0);
    float p1 = smx_dsp_heard_sample(voice, 1);
    float p2 = smx_dsp_heard_sample(voice, 2);
    float p3 = smx_dsp_heard_sample(voice, 3);
    out =
        p1 + 0.5f * t *
                 (p2 - p0 +
                  t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
  } else {
    const float *weights = smx_dsp_sinc_table[((Uint32)voice->interp_pos >> 4) & 0xFFu];
    Sint32 tap = 0;
    for (; tap < (Sint32)SMX_DSP_SINC_TAPS; tap += 1) {
      out += weights[tap] * smx_dsp_heard_sample(voice, tap - 2);
    }
  }
  return smx_dsp_clamp16((Sint32)(out < 0.0f ? out - 0.5f : out + 0.5f));
}

static void smx_dsp_voice_output(SmxDsp *dsp, Uint8 index, Uint8 ch) {
  Sint32 amp = (dsp->t_output * (Sint8)smx_dsp_vreg(dsp, index, (Uint8)(SMX_DSP_VOLL + ch))) >> 7;

  dsp->t_main_out[ch] = smx_dsp_clamp16(dsp->t_main_out[ch] + amp);
  if (dsp->heard != SMX_DSP_GAUSSIAN) {
    Sint32 heard =
        (dsp->t_heard_output * (Sint8)smx_dsp_vreg(dsp, index, (Uint8)(SMX_DSP_VOLL + ch))) >> 7;
    dsp->t_heard_main[ch] = smx_dsp_clamp16(dsp->t_heard_main[ch] + heard);
  }
  if ((dsp->t_eon & (1u << index)) != 0) {
    dsp->t_echo_out[ch] = smx_dsp_clamp16(dsp->t_echo_out[ch] + amp);
  }
}

static void smx_dsp_v1(SmxDsp *dsp, Uint8 index) {
  dsp->t_dir_addr = (Uint16)(dsp->t_dir * 0x100u + dsp->t_srcn * 4u);
  dsp->t_srcn = smx_dsp_vreg(dsp, index, SMX_DSP_SRCN);
}

static void smx_dsp_v2(SmxDsp *dsp, Uint8 index) {
  Uint32 entry = dsp->t_dir_addr;

  if (dsp->voices[index].kon_delay == 0) {
    entry += 2u;
  }
  dsp->t_brr_next_addr =
      (Uint16)(smx_dsp_ram(dsp, entry) | ((Uint16)smx_dsp_ram(dsp, entry + 1u) << 8));
  dsp->t_adsr0 = smx_dsp_vreg(dsp, index, SMX_DSP_ADSR0);
  dsp->t_pitch = smx_dsp_vreg(dsp, index, SMX_DSP_PITCHL);
}

static void smx_dsp_v3a(SmxDsp *dsp, Uint8 index) {
  dsp->t_pitch += (Sint32)(smx_dsp_vreg(dsp, index, SMX_DSP_PITCHH) & 0x3F) << 8;
}

static void smx_dsp_v3b(SmxDsp *dsp, Uint8 index) {
  SmxDspVoice *voice = &dsp->voices[index];

  dsp->t_brr_byte = smx_dsp_ram(dsp, (Uint32)voice->brr_addr + voice->brr_offset);
  dsp->t_brr_header = smx_dsp_ram(dsp, voice->brr_addr);
}

static void smx_dsp_v3c(SmxDsp *dsp, Uint8 index) {
  SmxDspVoice *voice = &dsp->voices[index];
  Uint8 bit = (Uint8)(1u << index);
  Sint32 output = 0;

  if ((dsp->t_pmon & bit) != 0) {
    dsp->t_pitch += ((dsp->t_output >> 5) * dsp->t_pitch) >> 10;
  }
  if (voice->kon_delay != 0) {
    /* Key on: five samples of silence while the first block is fetched. */
    if (voice->kon_delay == 5) {
      voice->brr_addr = dsp->t_brr_next_addr;
      voice->brr_offset = 1;
      voice->buf_pos = 0;
      dsp->t_brr_header = 0;
    }
    voice->env = 0;
    voice->hidden_env = 0;
    voice->interp_pos = 0;
    voice->kon_delay = (Uint8)(voice->kon_delay - 1u);
    if ((voice->kon_delay & 3u) != 0) {
      voice->interp_pos = 0x4000;
    }
    dsp->t_pitch = 0;
  }
  output = smx_dsp_interpolate(voice);
  if ((dsp->t_non & bit) != 0) {
    output = (Sint16)(dsp->noise * 2u);
  }
  dsp->t_output = ((output * voice->env) >> 11) & ~1;
  if (dsp->heard != SMX_DSP_GAUSSIAN) {
    Sint32 heard = (dsp->t_non & bit) != 0 ? output : smx_dsp_heard_interpolate(dsp, voice);
    dsp->t_heard_output = ((heard * voice->env) >> 11) & ~1;
  }
  voice->envx_out = (Uint8)(voice->env >> 4);
  if ((dsp->regs[SMX_DSP_FLG] & 0x80) != 0 || (dsp->t_brr_header & 3u) == 1) {
    voice->env_mode = SMX_ENV_RELEASE;
    voice->env = 0;
  }
  if (dsp->every_other) {
    if ((dsp->t_koff & bit) != 0) {
      voice->env_mode = SMX_ENV_RELEASE;
    }
    if ((dsp->kon & bit) != 0) {
      voice->kon_delay = 5;
      voice->env_mode = SMX_ENV_ATTACK;
    }
  }
  if (voice->kon_delay == 0) {
    smx_dsp_envelope(dsp, index);
  }
}

static void smx_dsp_v4(SmxDsp *dsp, Uint8 index) {
  SmxDspVoice *voice = &dsp->voices[index];

  dsp->t_looped = 0;
  if (voice->interp_pos >= 0x4000) {
    smx_dsp_decode(dsp, voice);
    voice->brr_offset = (Uint8)(voice->brr_offset + 2u);
    if (voice->brr_offset >= 9) {
      voice->brr_addr = (Uint16)(voice->brr_addr + 9u);
      if ((dsp->t_brr_header & 1u) != 0) {
        voice->brr_addr = dsp->t_brr_next_addr;
        dsp->t_looped = (Uint8)(1u << index);
      }
      voice->brr_offset = 1;
    }
  }
  voice->interp_pos = (voice->interp_pos & 0x3FFF) + dsp->t_pitch;
  if (voice->interp_pos > 0x7FFF) {
    voice->interp_pos = 0x7FFF;
  }
  smx_dsp_voice_output(dsp, index, 0);
}

static void smx_dsp_v5(SmxDsp *dsp, Uint8 index) {
  smx_dsp_voice_output(dsp, index, 1);
  dsp->endx_buf = (Uint8)(dsp->regs[SMX_DSP_ENDX] | dsp->t_looped);
  if (dsp->voices[index].kon_delay == 5) {
    dsp->endx_buf = (Uint8)(dsp->endx_buf & ~(1u << index));
  }
}

static void smx_dsp_v6(SmxDsp *dsp, Uint8 index) {
  (void)index;
  dsp->outx_buf = (Uint8)(dsp->t_output >> 8);
}

static void smx_dsp_v7(SmxDsp *dsp, Uint8 index) {
  dsp->regs[SMX_DSP_ENDX] = dsp->endx_buf;
  dsp->envx_buf = dsp->voices[index].envx_out;
}

static void smx_dsp_v8(SmxDsp *dsp, Uint8 index) {
  dsp->regs[(Uint8)(index * 0x10u + SMX_DSP_OUTX)] = dsp->outx_buf;
}

static void smx_dsp_v9(SmxDsp *dsp, Uint8 index) {
  dsp->regs[(Uint8)(index * 0x10u + SMX_DSP_ENVX)] = dsp->envx_buf;
}

/* The combined steps the hardware runs for three voices on one cycle. */
static void smx_dsp_v7_v4_v1(SmxDsp *dsp, Uint8 index) {
  smx_dsp_v7(dsp, index);
  smx_dsp_v1(dsp, (Uint8)(index + 3u));
  smx_dsp_v4(dsp, (Uint8)(index + 1u));
}

static void smx_dsp_v8_v5_v2(SmxDsp *dsp, Uint8 index) {
  smx_dsp_v8(dsp, index);
  smx_dsp_v5(dsp, (Uint8)(index + 1u));
  smx_dsp_v2(dsp, (Uint8)(index + 2u));
}

static void smx_dsp_v9_v6_v3(SmxDsp *dsp, Uint8 index) {
  smx_dsp_v9(dsp, index);
  smx_dsp_v6(dsp, (Uint8)(index + 1u));
  smx_dsp_v3a(dsp, (Uint8)(index + 2u));
  smx_dsp_v3b(dsp, (Uint8)(index + 2u));
  smx_dsp_v3c(dsp, (Uint8)(index + 2u));
}

static Uint16 smx_dsp_echo_addr(const SmxDsp *dsp, Uint8 ch) {
  return (Uint16)(dsp->t_echo_ptr + ch * 2u);
}

static void smx_dsp_echo_read(SmxDsp *dsp, Uint8 ch) {
  Uint16 addr = smx_dsp_echo_addr(dsp, ch);
  Sint16 sample = (Sint16)(smx_dsp_ram(dsp, addr) | ((Uint16)smx_dsp_ram(dsp, addr + 1u) << 8));

  dsp->hist[dsp->hist_pos][ch] = (Sint16)(sample >> 1);
  dsp->hist[dsp->hist_pos + 8u][ch] = (Sint16)(sample >> 1);
}

/* Tap 7 applies to the newest history sample and tap 0 to the oldest. */
static Sint32 smx_dsp_fir(const SmxDsp *dsp, Uint8 tap, Uint8 ch) {
  return ((Sint32)dsp->hist[dsp->hist_pos + tap + 1u][ch] *
          (Sint8)dsp->regs[(Uint8)(SMX_DSP_FIR + tap * 0x10u)]) >>
         6;
}

static Sint32 smx_dsp_mix(const SmxDsp *dsp, Sint32 dry, Uint8 ch) {
  Sint32 main = (Sint16)((dry * (Sint8)dsp->regs[(Uint8)(SMX_DSP_MVOLL + ch * 0x10u)]) >> 7);
  Sint32 echo =
      (Sint16)((dsp->t_echo_in[ch] * (Sint8)dsp->regs[(Uint8)(SMX_DSP_EVOLL + ch * 0x10u)]) >> 7);
  return smx_dsp_clamp16(main + echo);
}

static Sint32 smx_dsp_echo_output(const SmxDsp *dsp, Uint8 ch) {
  return smx_dsp_mix(dsp, dsp->t_main_out[ch], ch);
}

static void smx_dsp_echo_write(SmxDsp *dsp, Uint8 ch) {
  if ((dsp->t_echo_flags & 0x20) == 0) {
    Uint16 addr = smx_dsp_echo_addr(dsp, ch);
    dsp->ram[addr] = (Uint8)dsp->t_echo_out[ch];
    dsp->ram[(Uint16)(addr + 1u)] = (Uint8)(dsp->t_echo_out[ch] >> 8);
  }
  dsp->t_echo_out[ch] = 0;
}

static void smx_dsp_emit(SmxDsp *dsp, Sint32 left, Sint32 right) {
  Uint32 slot = (dsp->out_head + dsp->out_count) % SMX_DSP_OUT_FRAMES;

  dsp->out[slot * 2u] = (Sint16)left;
  dsp->out[slot * 2u + 1u] = (Sint16)right;
  if (dsp->out_count < SMX_DSP_OUT_FRAMES) {
    dsp->out_count += 1;
  } else {
    dsp->out_head = (dsp->out_head + 1u) % SMX_DSP_OUT_FRAMES;
  }
}

static void smx_dsp_echo_22(SmxDsp *dsp) {
  dsp->hist_pos = (Uint8)((dsp->hist_pos + 1u) & 7u);
  dsp->t_echo_ptr = (Uint16)(dsp->t_esa * 0x100u + dsp->echo_offset);
  smx_dsp_echo_read(dsp, 0);
  dsp->t_echo_in[0] = smx_dsp_fir(dsp, 0, 0);
  dsp->t_echo_in[1] = smx_dsp_fir(dsp, 0, 1);
}

static void smx_dsp_echo_23(SmxDsp *dsp) {
  dsp->t_echo_in[0] += smx_dsp_fir(dsp, 1, 0) + smx_dsp_fir(dsp, 2, 0);
  dsp->t_echo_in[1] += smx_dsp_fir(dsp, 1, 1) + smx_dsp_fir(dsp, 2, 1);
  smx_dsp_echo_read(dsp, 1);
}

static void smx_dsp_echo_24(SmxDsp *dsp) {
  dsp->t_echo_in[0] += smx_dsp_fir(dsp, 3, 0) + smx_dsp_fir(dsp, 4, 0) + smx_dsp_fir(dsp, 5, 0);
  dsp->t_echo_in[1] += smx_dsp_fir(dsp, 3, 1) + smx_dsp_fir(dsp, 4, 1) + smx_dsp_fir(dsp, 5, 1);
}

static void smx_dsp_echo_25(SmxDsp *dsp) {
  Uint8 ch = 0;

  for (; ch < 2; ch += 1) {
    /* Taps 0-6 wrap at 16 bits, then tap 7 is added and clamped. */
    Sint32 sum = (Sint16)(dsp->t_echo_in[ch] + smx_dsp_fir(dsp, 6, ch));
    sum += (Sint16)smx_dsp_fir(dsp, 7, ch);
    dsp->t_echo_in[ch] = smx_dsp_clamp16(sum) & ~1;
  }
}

static void smx_dsp_echo_26(SmxDsp *dsp) {
  Uint8 ch = 0;

  dsp->t_main_out[0] = smx_dsp_echo_output(dsp, 0);
  for (; ch < 2; ch += 1) {
    Sint32 feedback = (Sint16)((dsp->t_echo_in[ch] * (Sint8)dsp->regs[SMX_DSP_EFB]) >> 7);
    dsp->t_echo_out[ch] = smx_dsp_clamp16(dsp->t_echo_out[ch] + feedback) & ~1;
  }
}

static void smx_dsp_echo_27(SmxDsp *dsp) {
  Sint32 left = dsp->t_main_out[0];
  Sint32 right = smx_dsp_echo_output(dsp, 1);

  if (dsp->heard != SMX_DSP_GAUSSIAN) {
    left = smx_dsp_mix(dsp, dsp->t_heard_main[0], 0);
    right = smx_dsp_mix(dsp, dsp->t_heard_main[1], 1);
  }
  dsp->t_main_out[0] = 0;
  dsp->t_main_out[1] = 0;
  dsp->t_heard_main[0] = 0;
  dsp->t_heard_main[1] = 0;
  if ((dsp->regs[SMX_DSP_FLG] & 0x40) != 0) {
    left = 0;
    right = 0;
  }
  smx_dsp_emit(dsp, left, right);
}

static void smx_dsp_echo_29(SmxDsp *dsp) {
  dsp->t_esa = dsp->regs[SMX_DSP_ESA];
  if (dsp->echo_offset == 0) {
    dsp->echo_length = (Uint16)((dsp->regs[SMX_DSP_EDL] & 0x0F) * 0x800u);
  }
  dsp->echo_offset = (Uint16)(dsp->echo_offset + 4u);
  if (dsp->echo_offset >= dsp->echo_length) {
    dsp->echo_offset = 0;
  }
  smx_dsp_echo_write(dsp, 0);
  dsp->t_echo_flags = dsp->regs[SMX_DSP_FLG];
}

static void smx_dsp_misc_29(SmxDsp *dsp) {
  dsp->every_other = !dsp->every_other;
  /* KON clears itself 63 cycles after the DSP last took it. */
  if (dsp->every_other) {
    dsp->new_kon = (Uint8)(dsp->new_kon & ~dsp->kon);
  }
}

static void smx_dsp_misc_30(SmxDsp *dsp) {
  if (dsp->every_other) {
    dsp->kon = dsp->new_kon;
    dsp->t_koff = dsp->regs[SMX_DSP_KOFF];
  }
  dsp->counter -= 1;
  if (dsp->counter < 0) {
    dsp->counter = SMX_DSP_COUNTER_RANGE - 1;
  }
  if (smx_dsp_fires(dsp, (Uint8)(dsp->regs[SMX_DSP_FLG] & 0x1F))) {
    Uint16 feedback = (Uint16)((dsp->noise << 13) ^ (dsp->noise << 14));
    dsp->noise = (Uint16)((feedback & 0x4000) ^ (dsp->noise >> 1));
  }
}

static void smx_dsp_cycle(SmxDsp *dsp) {
  switch (dsp->phase) {
  case 0:
    smx_dsp_v5(dsp, 0);
    smx_dsp_v2(dsp, 1);
    break;
  case 1:
    smx_dsp_v6(dsp, 0);
    smx_dsp_v3a(dsp, 1);
    smx_dsp_v3b(dsp, 1);
    smx_dsp_v3c(dsp, 1);
    break;
  case 2:
  case 5:
  case 8:
  case 11:
  case 14:
    smx_dsp_v7_v4_v1(dsp, (Uint8)((dsp->phase - 2u) / 3u));
    break;
  case 3:
  case 6:
  case 9:
  case 12:
  case 15:
    smx_dsp_v8_v5_v2(dsp, (Uint8)((dsp->phase - 3u) / 3u));
    break;
  case 4:
  case 7:
  case 10:
  case 13:
  case 16:
    smx_dsp_v9_v6_v3(dsp, (Uint8)((dsp->phase - 4u) / 3u));
    break;
  case 17:
    smx_dsp_v1(dsp, 0);
    smx_dsp_v7(dsp, 5);
    smx_dsp_v4(dsp, 6);
    break;
  case 18:
    smx_dsp_v8_v5_v2(dsp, 5);
    break;
  case 19:
    smx_dsp_v9_v6_v3(dsp, 5);
    break;
  case 20:
    smx_dsp_v1(dsp, 1);
    smx_dsp_v7(dsp, 6);
    smx_dsp_v4(dsp, 7);
    break;
  case 21:
    smx_dsp_v8(dsp, 6);
    smx_dsp_v5(dsp, 7);
    smx_dsp_v2(dsp, 0);
    break;
  case 22:
    smx_dsp_v3a(dsp, 0);
    smx_dsp_v9(dsp, 6);
    smx_dsp_v6(dsp, 7);
    smx_dsp_echo_22(dsp);
    break;
  case 23:
    smx_dsp_v7(dsp, 7);
    smx_dsp_echo_23(dsp);
    break;
  case 24:
    smx_dsp_v8(dsp, 7);
    smx_dsp_echo_24(dsp);
    break;
  case 25:
    smx_dsp_v3b(dsp, 0);
    smx_dsp_v9(dsp, 7);
    smx_dsp_echo_25(dsp);
    break;
  case 26:
    smx_dsp_echo_26(dsp);
    break;
  case 27:
    dsp->t_pmon = (Uint8)(dsp->regs[SMX_DSP_PMON] & 0xFE);
    smx_dsp_echo_27(dsp);
    break;
  case 28:
    dsp->t_non = dsp->regs[SMX_DSP_NON];
    dsp->t_eon = dsp->regs[SMX_DSP_EON];
    dsp->t_dir = dsp->regs[SMX_DSP_DIR];
    dsp->t_echo_flags = dsp->regs[SMX_DSP_FLG];
    break;
  case 29:
    smx_dsp_misc_29(dsp);
    smx_dsp_echo_29(dsp);
    break;
  case 30:
    smx_dsp_misc_30(dsp);
    smx_dsp_v3c(dsp, 0);
    smx_dsp_echo_write(dsp, 1);
    break;
  default:
    smx_dsp_v4(dsp, 0);
    smx_dsp_v1(dsp, 2);
    break;
  }
  dsp->phase = (Uint8)((dsp->phase + 1u) & 31u);
}

void smx_dsp_run(SmxDsp *dsp, Uint32 cycles) {
  for (; cycles != 0; cycles -= 1) {
    smx_dsp_cycle(dsp);
  }
}

Uint32 smx_dsp_drain(SmxDsp *dsp, Sint16 *frames, Uint32 max) {
  Uint32 count = 0;

  for (; count < max && dsp->out_count != 0; count += 1) {
    frames[count * 2u] = dsp->out[dsp->out_head * 2u];
    frames[count * 2u + 1u] = dsp->out[dsp->out_head * 2u + 1u];
    dsp->out_head = (dsp->out_head + 1u) % SMX_DSP_OUT_FRAMES;
    dsp->out_count -= 1;
  }
  return count;
}
