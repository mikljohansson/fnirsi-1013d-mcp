// test_adc_cal.c - per-core ADC calibration v3 (adc_cal.c): sector block, correction maths, measurement checks, the gain
// step, an end-to-end model of the 8-bit readout loop (gain + dither, rail rule) and the 16-bit filter path, and a model
// of the owner's CH1 built from the 2026-10-18 gain bench (docs/hardware-status.md), run natively. One clock class since
// fw 0.2.105 (FPGA 0x153D runs the ADCs at full rate at every rate): the model is the bench's full-rate class.

#include <stdio.h>
#include <string.h>

#include "adc_cal.h"
#include "pc_decim.h"
#include "test_util.h"

static uint32_t rng = 12345;

static int32_t iabs_t(int32_t v) { return (v < 0) ? -v : v; }

static uint32_t rnd(void)
{
  rng = rng * 1103515245u + 12345u;
  return rng >> 8;
}

//Gaussian-ish noise in 1/1024 code: sum of 4 uniforms, RMS about sigma
static int32_t noise1024(int32_t sigma1024)
{
  int32_t s = 0, i;

  for(i=0;i<4;i++)
    s += (int32_t)(rnd() & 0xFFFF) - 32768;

  //uniform(-32768, 32768) has RMS 18919; four of them 37838
  return (int32_t)(((int64_t)s * sigma1024) / 37838);
}

//One ADC code for a true level in 1/1024 code, with noise
static int32_t adc(int32_t level1024, int32_t sigma1024)
{
  int32_t v = level1024 + noise1024(sigma1024);
  v = (v + 512) >> 10;
  return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

//The 8-bit readout loop of fpga_read_adc_data for one sample: gain, offset, dither, clamp
static int32_t loop_code(int32_t raw, const acal_corr *k, int core, uint32_t *lfsr)
{
  int32_t s;

  ACAL_DITHER_STEP(*lfsr);
  s = (raw * (int32_t)(core ? k->gain_b : k->gain_a) + (core ? k->off_b : k->off_a) + (int32_t)(*lfsr >> 16)) >> 16;
  return (s < 0) ? 0 : ((s > 255) ? 255 : s);
}

//The owner's CH1 (bench 2026-10-18) at full rate: zeros A 131.7 / B 129.2, slopes -0.2633 / -0.2529 codes per count,
//the bench gains A 0.9951 / B 1.0278 (on the gated scale of the fw 0.2.45-0.2.104 block they come from)
static void unit_ch1(acal_channel *c, const uint16_t *pwm)
{
  uint32_t r;

  memset(c, 0, sizeof(*c));

  for(r=0;r<ACAL_RANGES;r++)
  {
    c->pwm[r] = pwm[r];
    c->zero[0][r]  = (int16_t)(131.7 * 16);
    c->zero[1][r]  = (int16_t)(129.2 * 16);
    c->slope[0][r] = -17257;
    c->slope[1][r] = -16572;
    c->noise[0][r] = 12;
    c->noise[1][r] = 19;
  }

  c->gain[0] = 32607;
  c->gain[1] = 33678;
  c->gain_range = 2;
  c->gain_flags = 0;
  c->gain_lever = 1416;
}

//The vectors file's example (gen_vectors.py adc_cal_v3_section): unit_ch1 on both channels, edits on CH2
static void example_channels(acal_channel ch[2])
{
  static const uint16_t pwm[7] = { 904, 900, 908, 903, 908, 903, 903 };

  unit_ch1(&ch[0], pwm);
  unit_ch1(&ch[1], pwm);
  ch[1].zero[1][4]  = -77;
  ch[1].slope[0][5] = -20000;
  ch[1].noise[1][2] = 47;
  ch[1].gain[1]     = 31000;
  ch[1].gain_range = ACAL_GAIN_NONE;
  ch[1].gain_flags = ACAL_GAIN_KEPT;
  ch[1].gain_lever = 0;
}

static void test_crc_and_block(void)
{
  uint8_t  blk[ACAL_BLOCK_BYTES], b2[ACAL_BLOCK_BYTES];
  acal_channel in[2], out[2];
  uint16_t pwm[7] = { 904, 900, 908, 903, 908, 903, 903 };
  uint32_t i;
  const uint8_t *p;

  //CRC-32/ISO-HDLC check value (zlib.crc32(b"123456789"))
  T_EQ(acal_crc32((const uint8_t *)"123456789", 9), 0xCBF43926u);

  T_EQ(ACAL_CH_BYTES, 144);
  T_EQ(ACAL_BLOCK_BYTES, 304);
  T_TRUE(ACAL_SECTOR_OFFSET >= 120);                          //after Atlan's checksummed bytes 16..119
  T_TRUE(ACAL_SECTOR_OFFSET + ACAL_BLOCK_BYTES <= 512);

  example_channels(in);

  memset(blk, 0xEE, sizeof(blk));
  acal_pack(in, blk);
  T_EQ(blk[0], 'A'); T_EQ(blk[1], 'C'); T_EQ(blk[2], 'A'); T_EQ(blk[3], 'L');
  T_EQ(blk[4], 3); T_EQ(blk[5], 0);                           //version
  T_EQ(blk[6] | (blk[7] << 8), ACAL_BLOCK_BYTES);
  T_EQ(blk[8], ACAL_BLOCK_CLASSES); T_EQ(blk[8], 1); T_EQ(blk[9], 0); T_EQ(blk[10], 0); T_EQ(blk[11], 0);
  T_EQ(blk[12] | (blk[13] << 8), 904);                        //CH1 pwm[0]
  T_EQ(blk[ACAL_HDR_BYTES + ACAL_CH_BYTES] | (blk[ACAL_HDR_BYTES + ACAL_CH_BYTES + 1] << 8), 904);   //CH2 pwm[0]
  p = blk + ACAL_HDR_BYTES;
  T_EQ(p[12] | (p[13] << 8), 2107);                           //zero[slot 0][A][0] = 131.7 x 16
  T_EQ((int16_t)(p[60] | (p[61] << 8)), -17257);              //slope[0][A][0]
  T_EQ(p[108], 12);                                           //noise[0][A][0]
  T_EQ(p[132] | (p[133] << 8), 32607);                        //gain[0][A]
  T_EQ(p[134] | (p[135] << 8), 33678);                        //gain[0][B]
  //slot 1 (the gated class of fw 0.2.45-0.2.104): zeros
  for(i=0;i<24;i++)
  {
    T_EQ(p[36 + i], 0); T_EQ(p[84 + i], 0);
  }
  for(i=0;i<12;i++)
    T_EQ(p[120 + i], 0);
  for(i=0;i<4;i++)
    T_EQ(p[136 + i], 0);
  T_EQ(p[140], 2); T_EQ(p[141], 0); T_EQ(p[142] | (p[143] << 8), 1416);
  p += ACAL_CH_BYTES;
  T_EQ(p[140], ACAL_GAIN_NONE); T_EQ(p[141], ACAL_GAIN_KEPT);

  memset(out, 0, sizeof(out));
  T_EQ(acal_unpack(blk, out), ACAL_OK);
  T_MEM(&in[0], &out[0], sizeof(acal_channel));
  T_MEM(&in[1], &out[1], sizeof(acal_channel));

  //every single-bit flip is caught
  for(i=0;i<ACAL_BLOCK_BYTES*8;i+=7)
  {
    memcpy(b2, blk, sizeof(b2));
    b2[i / 8] ^= (uint8_t)(1 << (i % 8));
    T_TRUE(acal_unpack(b2, out) != ACAL_OK);
  }

  memset(b2, 0, sizeof(b2));
  T_EQ(acal_unpack(b2, out), ACAL_E_MAGIC);                   //an older firmware's save: zeros
  memcpy(b2, blk, sizeof(b2)); b2[4] = 2; b2[6] = 0x48;       //a v2 header (fw 0.2.43-0.2.44, 328 bytes)
  T_EQ(acal_unpack(b2, out), ACAL_E_VERSION);
  memcpy(b2, blk, sizeof(b2)); b2[8] = 3;
  T_EQ(acal_unpack(b2, out), ACAL_E_VERSION);
  memcpy(b2, blk, sizeof(b2)); b2[8] = 0;
  T_EQ(acal_unpack(b2, out), ACAL_E_VERSION);

  //A block of fw 0.2.45-0.2.104 (classes 2, slot 1 the gated class): loads, slot 0 only, no forced recalibration
  {
    uint32_t ch, crc;

    memcpy(b2, blk, sizeof(b2));
    b2[8] = 2;

    for(ch=0;ch<2;ch++)
    {
      uint8_t *q = b2 + ACAL_HDR_BYTES + ch * ACAL_CH_BYTES;

      for(i=0;i<24;i++) { q[36 + i] = (uint8_t)(0x31 + i); q[84 + i] = (uint8_t)(0x91 + i); }
      for(i=0;i<12;i++) q[120 + i] = (uint8_t)(7 + i);
      for(i=0;i<4;i++)  q[136 + i] = (uint8_t)(0x80 + i);
    }

    crc = acal_crc32(b2, ACAL_BLOCK_BYTES - 4);
    b2[300] = (uint8_t)crc; b2[301] = (uint8_t)(crc >> 8); b2[302] = (uint8_t)(crc >> 16); b2[303] = (uint8_t)(crc >> 24);
    memset(out, 0, sizeof(out));
    T_EQ(acal_unpack(b2, out), ACAL_OK);
    T_MEM(&in[0], &out[0], sizeof(acal_channel));
    T_MEM(&in[1], &out[1], sizeof(acal_channel));

    //and saves back as a one-class block
    acal_pack(out, b2);
    T_MEM(b2, blk, sizeof(blk));
  }
  memcpy(b2, blk, sizeof(b2)); b2[100] ^= 1;
  T_EQ(acal_unpack(b2, out), ACAL_E_CRC);

  T_TRUE(acal_bound(&in[0], pwm));
  pwm[3] = 860;
  T_TRUE(!acal_bound(&in[0], pwm));

  //gains as applied: Q15 x 2, unity when never measured
  T_EQ(acal_gain16(&in[0], 1), 2 * 33678);
  T_EQ(acal_gain16(&in[1], 1), 65536);
}

static void test_round_pair(void)
{
  int32_t da, db, worst_spread = 0, worst_mean = 0;
  int16_t a, b;

  for(da=-600;da<=600;da+=3)
    for(db=-600;db<=600;db+=5)
    {
      int32_t ra, rb;
      acal_round_pair(da, db, &a, &b);
      ra = da - 16 * a;                                       //correction still missing, 1/16 code
      rb = db - 16 * b;

      if(ra - rb > worst_spread) worst_spread = ra - rb;
      if(rb - ra > worst_spread) worst_spread = rb - ra;
      if(ra + rb > worst_mean) worst_mean = ra + rb;
      if(-(ra + rb) > worst_mean) worst_mean = -(ra + rb);
    }

  T_TRUE(worst_spread <= 8);                                  //A - B within 1/2 code
  T_TRUE(worst_mean <= 16);                                   //mean within 1/2 code (sum of two within 1 code)

  //the owner's CH1 at T = 128: A 130.93 (2095), B 125.58 (2009) -> -3 / +2 (1.00o5 stored -2 / +2)
  acal_round_pair(2048 - 2095, 2048 - 2009, &a, &b);
  T_EQ(a, -3);
  T_EQ(b, 2);
}

//slope (Q16) x dcoffset in 1/16 code, rounded half away from zero
static int32_t off16(int32_t slope, int32_t d)
{
  int64_t v = (int64_t)slope * d;
  return (int32_t)((v >= 0) ? (v + 2048) / 4096 : -((-v + 2048) / 4096));
}

static void test_correction(void)
{
  uint16_t pwm[7] = { 904, 900, 908, 903, 908, 903, 903 };
  acal_channel c;
  acal_corr k;
  int16_t ca, cb;
  int32_t d, core, raw;

  unit_ch1(&c, pwm);

  //1.00o5 fallback: no block, or not bound: whole-code offsets, unity gains, 1.00o5 rail limits
  acal_correction(0, pwm, 3, 0, 385, -2, 2, &k);
  T_EQ(k.version, 1); T_EQ(k.comp_a, -2); T_EQ(k.comp_b, 2); T_EQ(k.frac_a, 0); T_EQ(k.frac_b, 0);
  T_EQ(k.gain_a, 65536); T_EQ(k.gain_b, 65536); T_EQ(k.off_a, -2 * 65536); T_EQ(k.off_b, 2 * 65536);
  T_EQ(k.clip_lo, 2); T_EQ(k.clip_hi, 253); T_EQ(k.gain16, 65536);
  T_EQ(k.zero_q8, 128 * 256); T_EQ(k.corr16, 0); T_EQ(k.shift, 0);
  acal_correction(0, pwm, 3, 100, 385, -2, 3, &k);
  T_EQ(k.shift, 25);                                          //100 x 100 / 385 = 25.97, C truncation
  T_EQ(k.zero_q8, 128 * 256 - 6649);                          //1.00o5 formula, 25600 x 100 / 385 = 6649.35
  T_EQ(k.corr16, 8);
  pwm[0] = 1;
  acal_correction(&c, pwm, 3, 0, 385, -2, 2, &k);
  T_EQ(k.version, 1);
  pwm[0] = 904;

  //dither never adds a code to a whole-code offset: the 1.00o5 arithmetic exactly
  {
    uint32_t lfsr = 1;
    acal_correction(0, pwm, 3, 0, 385, -2, 2, &k);
    for(raw=0;raw<256;raw++)
    {
      T_EQ(loop_code(raw, &k, 0, &lfsr), (raw - 2 < 0) ? 0 : raw - 2);
      T_EQ(loop_code(raw, &k, 1, &lfsr), (raw + 2 > 255) ? 255 : raw + 2);
    }
  }

  //v3 with unity gains is v2 exactly: comp / frac at 0 V, offset 4096 x (T16 - z16)
  {
    acal_channel u = c;
    acal_gain_carry(0, &u);
    acal_correction(&u, pwm, 3, 0, 385, -2, 2, &k);
    T_EQ(k.version, 3);
    T_EQ(16 * k.comp_a + k.frac_a, 2048 - u.zero[0][3]);
    T_EQ(16 * k.comp_b + k.frac_b, 2048 - u.zero[1][3]);
    T_EQ(k.gain_a, 65536); T_EQ(k.gain_b, 65536); T_EQ(k.gain16, 65536);
    T_EQ(k.off_a, 4096 * (2048 - u.zero[0][3]));
    T_EQ(k.off_b, 4096 * (2048 - u.zero[1][3]));
    T_EQ(k.corr16, 2048 - (u.zero[0][3] + u.zero[1][3] + 1) / 2);
  }

  //with gains: the corrected mean (raw x gain + off) / 65536 = T + G (raw - z) for every raw code and offset
  for(d=-600;d<=600;d+=37)
    {
      acal_correction(&c, pwm, 3, (int16_t)d, 385, -2, 2, &k);
      T_EQ(k.zero_q8, 256 * (128 - k.shift));

      for(core=0;core<2;core++)
      {
        int32_t z16 = c.zero[core][3] + off16(c.slope[core][3], d);
        int64_t g   = acal_gain16(&c, core);
        uint32_t gk = core ? k.gain_b : k.gain_a;
        int32_t  ok = core ? k.off_b : k.off_a;

        T_EQ(gk, g);
        T_EQ(16 * (core ? k.comp_b : k.comp_a) + (core ? k.frac_b : k.frac_a), 16 * (128 - k.shift) - z16);

        for(raw=0;raw<256;raw+=15)
        {
          //exact model in 1/(16 x 65536) code vs the firmware's (raw x gain + off) x 16; rounding of off only
          int64_t want = (int64_t)16 * 65536 * (128 - k.shift) + g * (16 * raw - z16);
          int64_t got  = 16 * ((int64_t)raw * gk + ok);
          T_TRUE(want - got <= 8 && got - want <= 8);
        }
      }

      //rail limits: corrected codes of raw 0 and 255, the tighter of the two cores
      {
        int32_t lo_a = (int32_t)((int64_t)k.off_a >> 16), lo_b = (int32_t)((int64_t)k.off_b >> 16);
        int32_t hi_a = (int32_t)(((int64_t)255 * k.gain_a + k.off_a) >> 16);
        int32_t hi_b = (int32_t)(((int64_t)255 * k.gain_b + k.off_b) >> 16);
        T_EQ(k.clip_lo, (lo_a > lo_b) ? lo_a : lo_b);
        T_EQ(k.clip_hi, (hi_a < hi_b) ? hi_a : hi_b);
      }
    }

  //range 6 uses range 5
  c.zero[0][5] = 2100;
  acal_correction(&c, pwm, 6, 0, 385, -2, 2, &k);
  T_EQ(16 * k.comp_a + k.frac_a, 2048 - 2100);
  acal_correction(&c, pwm, 3, 0, 385, -2, 2, &k);
  T_EQ(16 * k.comp_b + k.frac_b, 2048 - c.zero[1][3]);
  T_EQ(k.gain_b, 2 * 33678);

  //16-bit path gain: harmonic mean of the gains, corr16 with it
  {
    uint64_t ga = 2 * 32607, gb = 2 * 33678;
    int64_t  zs = c.zero[0][3] + c.zero[1][3];
    T_EQ(k.gain16, (2 * ga * gb + (ga + gb) / 2) / (ga + gb));
    T_TRUE(k.gain16 > 66200 && k.gain16 < 66350);             //1.0112: the full rate on the old block's gated scale
    T_EQ(k.corr16, (2048 * 131072 - (int64_t)k.gain16 * zs - 65536) / 131072);
  }
  T_EQ(acal_scale(1u << 24, 65536), 1u << 24);
  T_EQ(acal_scale(100000, 66000), 100708);                   //100000 x 66000 / 65536 = 100708.0

  //1.00o5 fields from the v3 zeros averaged over the ranges
  unit_ch1(&c, pwm);
  acal_atlan_comps(&c, &ca, &cb);
  T_EQ(ca, -4);                                               //131.7 -> 127.7
  T_EQ(cb, -1);                                               //129.2 -> 128.2
}

static void test_measurement(void)
{
  uint8_t buf[4000];
  acal_acc a, b;
  uint32_t i;
  acal_range_meas m;
  acal_channel c;

  //mean and RMS: 0,2,0,2,... mean 1, RMS 1
  for(i=0;i<4000;i++) buf[i] = (uint8_t)((i & 1) ? 2 : 0);
  memset(&a, 0, sizeof(a));
  acal_acc_add(&a, buf, 2000, 2);                             //even positions: all 0
  T_EQ(acal_mean16(&a), 0);
  T_EQ(a.clipped, 2000);
  memset(&a, 0, sizeof(a));
  acal_acc_add(&a, buf, 4000, 1);
  T_EQ(acal_mean16(&a), 16);
  T_EQ(acal_rms16(&a), 16);
  memset(&b, 0, sizeof(b));
  acal_acc_add(&b, buf + 1, 2000, 2);                         //odd: all 2
  T_EQ(acal_mean16(&b), 32);
  T_EQ(acal_rms16(&b), 0);
  acal_acc_merge(&a, &b);
  T_EQ(a.n, 6000);

  //noisy samples: RMS within 5 % of the model
  rng = 99;
  for(i=0;i<4000;i++) buf[i] = (uint8_t)adc(128 * 1024, 1024);
  memset(&a, 0, sizeof(a));
  acal_acc_add(&a, buf, 4000, 1);
  T_TRUE(acal_rms16(&a) >= 16 && acal_rms16(&a) <= 18);     //1 code noise plus 1/sqrt(12) quantisation
  T_TRUE(acal_mean16(&a) >= 2046 && acal_mean16(&a) <= 2050);

  //coarse centre: 500 -> 220.0, 1200 -> 35.0: 128 at 500 + 92 x 700 / 185 = 848
  T_EQ(acal_centre_pwm(500, 220 * 16, 1200, 35 * 16), 848);
  T_EQ(acal_centre_pwm(500, 130 * 16, 1200, 120 * 16), -1);  //no response
  T_EQ(acal_centre_pwm(100, 120 * 16, 800, 10 * 16), -1);   //centre below ACAL_PWM_MIN

  //range store and every reject
  memset(&c, 0, sizeof(c));
  memset(&m, 0, sizeof(m));
  m.pwm_lo = 753; m.pwm_hi = 1053; m.pwm = 903;
  m.search16_lo[0] = 2095 + 634; m.search16_hi[0] = 2095 - 634;   //the 2 MSa/s search points: not stored
  m.search16_lo[1] = 2010 + 623; m.search16_hi[1] = 2010 - 623;
  m.mean16_lo[0] = 2107 + 632; m.mean16_hi[0] = 2107 - 632;
  m.mean16_lo[1] = 2067 + 607; m.mean16_hi[1] = 2067 - 607;
  m.zero16[0] = 2107; m.zero16[1] = 2067;
  m.noise16[0] = 9; m.noise16[1] = 18;
  T_EQ(acal_range_store(&m, &c, 3), ACAL_OK);
  T_EQ(c.pwm[3], 903);
  T_EQ(c.slope[0][3], -17258);                                //-1264 x 4096 / 300
  T_EQ(c.slope[1][3], -16575);
  T_EQ(c.zero[0][3], 2107);
  T_EQ(c.zero[1][3], 2067);
  T_EQ(c.noise[0][3], 9);
  T_EQ(c.noise[1][3], 18);

  {
    acal_range_meas x;
    acal_channel before = c;

    x = m; x.clipped = 1;                         T_EQ(acal_range_store(&x, &c, 2), ACAL_E_CLIP);
    x = m; x.noise16[1] = 49;                     T_EQ(acal_range_store(&x, &c, 2), ACAL_E_NOISE);
    x = m; x.drift16[0] = -9;                     T_EQ(acal_range_store(&x, &c, 2), ACAL_E_DRIFT);
    x = m; x.mean16_hi[0] = x.mean16_lo[0] - 900; T_EQ(acal_range_store(&x, &c, 2), ACAL_E_SLOPE);  //-0.1875
    x = m; x.mean16_hi[1] = x.mean16_lo[1] - 1400;
                                                  T_EQ(acal_range_store(&x, &c, 2), ACAL_E_SLOPE);  //A/B 0.026 apart
    x = m; x.zero16[1] = 2048 - 257;              T_EQ(acal_range_store(&x, &c, 2), ACAL_E_ZERO);
    x = m; x.zero16[0] = 2048 + 170; x.zero16[1] = 2048 - 170;
                                                  T_EQ(acal_range_store(&x, &c, 2), ACAL_E_SPREAD);
    x = m; x.search16_lo[0] = 0; x.search16_hi[1] = 4000;
                                                  T_EQ(acal_range_store(&x, &c, 2), ACAL_OK);       //search points
    c = before;
    T_MEM(&before, &c, sizeof(c));
    x = m; x.noise16[0] = 49;                     T_EQ(acal_range_store(&x, &c, 2), ACAL_E_NOISE);
    T_MEM(&before, &c, sizeof(c));                            //nothing stored on a reject
  }

  T_STR(acal_reason_text(ACAL_E_NOISE), "noisy: unplug probe");
  T_STR(acal_reason_text(ACAL_E_GAIN_LEVER), "gain: probe on post?");
  for(i=0;i<=20;i++)
    T_TRUE(strlen(acal_reason_text((int)i)) <= 21);
  T_EQ(acal_interp_pwm(753, 2048 + 634, 1053, 2048 - 634), 903);
  T_EQ(acal_interp_pwm(753, 2000, 1053, 2000), 753);
  T_STR(acal_reason_text(99), "no calibration data");
}

//The zero step's noise figure against the bench (fw 0.2.60, 0.2.59 wizard "too much noise"). cal_measure / cal_result
//of cal_wiz_ui.c on synthetic readouts: convs conversions of 500 samples per core (core A odd, core B even), first and
//second half in separate accumulators, merged per core. level16 / sd16 / move16 in 1/16 code; move16 is a per-conversion
//offset that alternates in sign (what moves between conversions: ripple phase, mains pickup), glitch a code added to one
//sample of core A per conversion
typedef struct { uint32_t noise16[2], within16[2]; int32_t mean16[2], drift16[2]; } zero_meas;

static zero_meas zero_sim(uint32_t convs, const int32_t level16[2], const int32_t sd16[2], int32_t drift16,
                          int32_t move16, int32_t glitch)
{
  uint8_t   buf[1000];
  acal_acc  acc[2][2], all;
  zero_meas z;
  uint32_t  i, j, h, k;

  memset(acc, 0, sizeof(acc));

  for(i=0;i<convs;i++)
  {
    h = (i >= convs / 2);

    for(j=0;j<1000;j++)
    {
      k = (j & 1) ? 0 : 1;                                    //odd core A, even core B
      buf[j] = (uint8_t)adc((level16[k] + (h ? drift16 : 0) + ((i & 1) ? move16 : -move16)) * 64, sd16[k] * 64);
    }

    if(glitch)
      buf[501] = (uint8_t)(buf[501] + glitch);

    acal_acc_add(&acc[h][0], buf + 1, 500, 2);
    acal_acc_add(&acc[h][1], buf, 500, 2);
  }

  for(k=0;k<2;k++)
  {
    all = acc[0][k];
    acal_acc_merge(&all, &acc[1][k]);
    z.mean16[k]   = acal_mean16(&all);
    z.drift16[k]  = acal_mean16(&acc[1][k]) - acal_mean16(&acc[0][k]);
    z.noise16[k]  = acal_rms16(&all);
    z.within16[k] = acal_rms16_within(&all);
  }

  return z;
}

static void test_zero_noise_bench(void)
{
  //Coordinator 2026-10-19 15:50, fw 0.2.59, probes off, raw std per core (codes) and means: CH1 range 3 A 0.50 B 1.11
  //(131.4 / 128.1), CH2 range 0 A 1.66 B 2.10 (129.8 / 130.1), the worst
  static const int32_t ch1_lvl[2] = { 2102, 2050 }, ch1_sd[2] = { 8, 18 };
  static const int32_t ch2_lvl[2] = { 2077, 2082 }, ch2_sd[2] = { 27, 34 };
  acal_range_meas m;
  acal_fail       f;
  zero_meas       z;
  uint32_t        k, convs;

  rng = 4242;

  //Both halves merged with a 0.3-code drift between them: per core RMS of the samples (plus the 1/sqrt(12) of the
  //rounding), the cores never pooled, the drift adds nothing measurable, within = total; far under the 3-code limit
  for(convs=8;convs<=64;convs+=56)
  {
    z = zero_sim(convs, ch1_lvl, ch1_sd, 5, 0, 0);
    T_TRUE(z.noise16[0] >= 8 && z.noise16[0] <= 11);          //0.50 code and the rounding: 0.58
    T_TRUE(z.noise16[1] >= 17 && z.noise16[1] <= 20);         //1.11 -> 1.15
    T_TRUE(z.mean16[0] >= 2100 && z.mean16[0] <= 2108);
    T_TRUE(z.mean16[1] >= 2048 && z.mean16[1] <= 2056);

    for(k=0;k<2;k++)
    {
      T_TRUE(z.noise16[k] < ACAL_MAX_NOISE16);
      T_TRUE(z.noise16[k] - z.within16[k] <= 1);
      T_TRUE(z.drift16[k] >= 3 && z.drift16[k] <= 7);
    }

    z = zero_sim(convs, ch2_lvl, ch2_sd, 5, 0, 0);
    T_TRUE(z.noise16[0] >= 26 && z.noise16[0] <= 30);
    T_TRUE(z.noise16[1] >= 33 && z.noise16[1] <= 37);
    T_TRUE(z.noise16[1] < ACAL_MAX_NOISE16);
  }

  //What does trip the limit: +-3 codes moving between conversions (pickup on the open input, ripple). The total
  //reads 3.2 codes, inside a capture it is still the bench's 1.1: the result line shows both
  z = zero_sim(64, ch1_lvl, ch1_sd, 0, 48, 0);
  T_TRUE(z.noise16[1] > ACAL_MAX_NOISE16);
  T_TRUE(z.within16[1] >= 17 && z.within16[1] <= 20);

  //A bus glitch of 64 codes on one sample in 500 per conversion: 2.9 codes RMS, inside the captures as well
  z = zero_sim(64, ch1_lvl, ch1_sd, 0, 0, 64);
  T_TRUE(z.noise16[0] >= 44 && z.noise16[0] <= 50);
  T_TRUE(z.noise16[0] - z.within16[0] <= 2);

  //acal_range_check says where: rate (always 200 MSa/s), core, value, within
  memset(&m, 0, sizeof(m));
  m.pwm_lo = 753; m.pwm_hi = 1053; m.pwm = 903;
  for(k=0;k<2;k++)
  {
    m.mean16_lo[k] = 2048 + 634;
    m.mean16_hi[k] = 2048 - 634;
    m.zero16[k]    = 2048;
    m.noise16[k]   = 18;
    m.within16[k]  = 17;
  }
  T_EQ(acal_range_check(&m, &f), ACAL_OK);
  T_EQ(f.value, 0);
  m.noise16[1] = 52;
  T_EQ(acal_range_check(&m, &f), ACAL_E_NOISE);
  T_EQ(f.rate, ACAL_RATE_FULL); T_EQ(f.core, 1); T_EQ(f.value, 52); T_EQ(f.within16, 17);
  //fw 0.2.100 (owner 2026-10-20 18:13): the limit Diagnostics shows, 3.0 codes; one since the gated class's 1.0 went
  //(fw 0.2.105). The owner's unit: stored 1.19 / 2.75 at the worst range per channel
  T_EQ(ACAL_MAX_NOISE16, 48);
  m.noise16[0] = 19; m.noise16[1] = 48;
  T_EQ(acal_range_check(&m, &f), ACAL_OK);
  m.noise16[0] = 49;
  T_EQ(acal_range_check(&m, &f), ACAL_E_NOISE);
  T_EQ(f.rate, ACAL_RATE_FULL); T_EQ(f.core, 0); T_EQ(f.value, 49);
  m.noise16[0] = m.noise16[1] = 18;
  m.drift16[0] = -11;
  T_EQ(acal_range_check(&m, &f), ACAL_E_DRIFT);
  T_EQ(f.core, 0); T_EQ(f.value, -11);
  m.drift16[0] = 0;
  m.zero16[0] = 2048 + 300;
  T_EQ(acal_range_check(&m, &f), ACAL_E_ZERO);
  T_EQ(f.rate, ACAL_RATE_FULL); T_EQ(f.core, 0); T_EQ(f.value, 300);
  T_EQ(acal_range_check(&m, 0), ACAL_E_ZERO);
}

//Gain step pieces: range choice, centring, the store checks, carry-over
static void test_gain_step(void)
{
  uint16_t pwm[7] = { 904, 900, 908, 903, 908, 903, 903 };
  acal_channel c, before, old;
  acal_gain_meas m, x;
  uint32_t k;

  //x1 probe: about 88 codes at range 2 stays there (176 at range 3 is too wide); x10: 8.8 codes -> range 5 (88);
  //nothing on the post: measure at the probe range (and fail the lever); huge: range 0
  T_EQ(acal_gain_range(88 * 16), 2);
  T_EQ(acal_gain_range(60 * 16), 3);
  T_EQ(acal_gain_range(61 * 16), 2);
  T_EQ(acal_gain_range(9 * 16), 5);
  T_EQ(acal_gain_range(24 * 16), 4);
  T_EQ(acal_gain_range(25 * 16), 3);
  T_EQ(acal_gain_range(0), ACAL_GAIN_PROBE_RANGE);
  T_EQ(acal_gain_range(-50), ACAL_GAIN_PROBE_RANGE);
  T_EQ(acal_gain_range(700 * 16), 0);
  T_EQ(acal_gain_mid_at(9 * 16, 2), 144);
  T_EQ(acal_gain_mid_at(9 * 16, 5), 1440);
  T_EQ(acal_gain_mid_at(-30, 0), -6);

  //centring: 9 codes above the zero with slopes about -0.262 codes per count -> 34 counts more PWM
  unit_ch1(&c, pwm);
  T_EQ(acal_gain_pwm(&c, 2, 144), 908 + 35);                 //the full-rate slopes, -0.258 mean
  T_EQ(acal_gain_pwm(&c, 2, -144), 908 - 35);
  T_EQ(acal_gain_pwm(&c, 2, 0), 908);
  T_EQ(acal_gain_pwm(&c, 2, 1000000), ACAL_PWM_MAX);

  //a clean measurement at range 2 (200 MSa/s): lows 93.0, highs 181.3 (A) and 178.4 (B)
  memset(&m, 0, sizeof(m));
  for(k=0;k<2;k++)
  {
    m.mean16[0][k] = 93 * 16;
    m.mean16[2][k] = 93 * 16 + 2;
  }
  m.mean16[1][0] = 1488 + 1414;
  m.mean16[1][1] = 1488 + 1366;

  //No gains in use (a first calibration): mean G 1; reference (span A + span B) / 2 = (2826 + 2730) / 2 = 2778
  //(1/32 code)
  acal_gain_carry(0, &c);
  T_EQ(acal_gain_store(&m, 2, &c), ACAL_OK);
  T_EQ(c.gain[0], (2778 * 32768 + 1413) / 2826);
  T_EQ(c.gain[1], (2778 * 32768 + 1365) / 2730);
  T_EQ(c.gain_range, 2); T_EQ(c.gain_flags, 0); T_EQ(c.gain_lever, 1389);

  //Gains in use (a fw 0.2.45-0.2.104 block: the full-rate class on the gated scale, about 1.1 % up): the new gains
  //give the mean of both cores the corrected span those gains give this measurement, so the volts calibration fitted
  //on it stays right; the split is the new measurement's. Again: unchanged
  c.gain[0] = 32607; c.gain[1] = 33678; c.gain_range = 1; c.gain_flags = ACAL_GAIN_KEPT;
  {
    int64_t keep = 32607LL * 2826 + 33678LL * 2730, now;

    T_EQ(acal_gain_store(&m, 2, &c), ACAL_OK);
    T_EQ(c.gain[0], (keep + 2826) / (2 * 2826));
    T_EQ(c.gain[1], (keep + 2730) / (2 * 2730));
    now = (int64_t)c.gain[0] * 2826 + (int64_t)c.gain[1] * 2730;
    T_TRUE(now - keep <= 2826 && keep - now <= 2826);
  }
  T_EQ(c.gain_range, 2); T_EQ(c.gain_flags, 0);
  before = c;
  T_EQ(acal_gain_store(&m, 2, &c), ACAL_OK);
  T_TRUE(iabs_t(c.gain[0] - before.gain[0]) <= 1 && iabs_t(c.gain[1] - before.gain[1]) <= 1);

  c = before;
  x = m; x.clipped = 3;                     T_EQ(acal_gain_store(&x, 2, &c), ACAL_E_GAIN_CLIP);
  x = m; x.mean16[2][1] += 9;               T_EQ(acal_gain_store(&x, 2, &c), ACAL_E_GAIN_DRIFT);
  x = m; x.mean16[2][0] -= 7;               T_EQ(acal_gain_store(&x, 2, &c), ACAL_OK);   //0.56 - 0.13 code: in
  c = before;
  x = m; x.mean16[1][0] = 1488 + 630;       T_EQ(acal_gain_store(&x, 2, &c), ACAL_E_GAIN_LEVER);
  for(k=0;k<2;k++) x.mean16[1][k] = m.mean16[0][k] - 1408;                       //levels swapped
                                            T_EQ(acal_gain_store(&x, 2, &c), ACAL_E_GAIN_LEVER);
  x = m; x.mean16[1][1] = 1488 + 1270;      T_EQ(acal_gain_store(&x, 2, &c), ACAL_E_GAIN_SPLIT);  //A/B 1.11
  T_MEM(&before, &c, sizeof(c));

  //carry-over: measured gains survive a new zero calibration, flagged; never measured -> unity
  old = before;
  memset(&c, 0x5A, sizeof(c));
  acal_gain_carry(&old, &c);
  T_MEM(c.gain, old.gain, sizeof(c.gain));
  T_EQ(c.gain_range, 2); T_EQ(c.gain_flags, ACAL_GAIN_KEPT); T_EQ(c.gain_lever, 1389);
  old.gain_range = ACAL_GAIN_NONE;
  acal_gain_carry(&old, &c);
  T_EQ(c.gain[1], ACAL_GAIN_ONE); T_EQ(c.gain_range, ACAL_GAIN_NONE); T_EQ(c.gain_flags, 0);
  memset(&c, 0x5A, sizeof(c));
  acal_gain_carry(0, &c);
  T_EQ(c.gain[0], ACAL_GAIN_ONE); T_EQ(c.gain_range, ACAL_GAIN_NONE); T_EQ(c.gain_lever, 0);
}

//----------------------------------------------------------------------------------------------------------------------------------
//Model of the owner's CH1 at range 2 (gain bench 2026-10-18, +-1.0045 V from a DMM-read reference against the shorted-input
//raw zeros), the full-rate class (every capture since FPGA 0x153D): codes per volt per core, the raw zeros at dcoffset 0,
//the offset slopes. The calibration post sits at -1.24 V (generator low) and +1.87 V (high) against probe ground.
//bm_gated_ref: the mean gated gain of both cores, the scale a fw 0.2.45-0.2.104 block's gains (and its volts calibration)
//are on.

static const double bm_gain[2]  = { 28.62, 27.71 };                              //[core A, B]
static const double bm_zero[2]  = { 131.73, 127.86 };
static const double bm_slope[2] = { -0.2648, -0.2533 };                          //codes per count
static const double bm_gated_ref = (28.54 + 28.42) / 2;
static double bm_bow = 0.0;                                                      //core A: -bow x (x - zero)^2

//True level (1/1024 code) of core k for input v at PWM offset dp from the centre
static int32_t bm_level(int k, double v, int32_t dp)
{
  double x = v * bm_gain[k] + bm_slope[k] * dp;                                //codes from the core's 0 V code

  if(k == 0)
    x -= bm_bow * x * x;

  return (int32_t)((bm_zero[k] + x) * 1024.0 + 0.5);
}

//One conversion: 500 samples per core, noise 1 code; at 200 MSa/s (full) a 25 kHz PWM ripple phase common to both
//cores per readout, at 2 MSa/s the record spans the ripple
static void bm_convert(int full, double v, int32_t dp, acal_acc acc[2])
{
  int32_t ripple = full ? (int32_t)(rnd() % 1024) - 512 : 0;                     //+-0.5 code, one phase per readout
  uint8_t s[2][500];
  int32_t i, k;

  for(k=0;k<2;k++)
  {
    int32_t lv = bm_level(k, v, dp) + ripple;

    for(i=0;i<500;i++)
      s[k][i] = (uint8_t)adc(lv, 1024);

    acal_acc_add(&acc[k], s[k], 500, 1);
  }
}

static void bm_mean(int full, double v, int32_t dp, int convs, int32_t mean16[2])
{
  acal_acc acc[2];
  int i;

  memset(acc, 0, sizeof(acc));

  for(i=0;i<convs;i++)
    bm_convert(full, v, dp, acc);

  mean16[0] = acal_mean16(&acc[0]);
  mean16[1] = acal_mean16(&acc[1]);
}

//The firmware's gain step (cal_wiz_ui.c cal_gain_check / cal_gain_measure) on the model; returns the rc, c gets the
//gains. symmetric = 0 skips the centring (levels at -35 / +53 codes from the zero)
static int bm_gain_step(acal_channel *c, int symmetric, uint8_t *range)
{
  static const double lev[3] = { -1.24, 1.87, -1.24 };
  int32_t lo[2], hi[2], span16, mid16, zm16, dp;
  acal_gain_meas m;
  uint8_t r;
  int l;

  //probe at range 2, 2 MSa/s, 2 conversions per level
  bm_mean(0, -1.24, 0, 2, lo);
  bm_mean(0, 1.87, 0, 2, hi);
  span16 = (hi[0] + hi[1] - lo[0] - lo[1]) / 2;
  mid16  = (hi[0] + hi[1] + lo[0] + lo[1]) / 4;
  zm16   = (c->zero[0][2] + c->zero[1][2]) / 2;
  r      = acal_gain_range(span16);
  *range = r;

  if(r != 2)
    return -1;                                                //the model has range 2 only

  dp = symmetric ? acal_gain_pwm(c, r, acal_gain_mid_at(mid16 - zm16, r)) - c->pwm[r] : 0;

  memset(&m, 0, sizeof(m));

  for(l=0;l<3;l++)
    bm_mean(1, lev[l], dp, 64, m.mean16[l]);

  return acal_gain_store(&m, r, c);
}

//Corrected per-core means (codes) at input v through acal_correction and the readout loop
static void bm_corrected(const acal_channel *c, const uint16_t *pwm, double v, double mean[2])
{
  acal_corr k;
  uint32_t lfsr = 7;
  int64_t  sum[2] = { 0, 0 };
  int32_t  i, n = 40000, core;

  acal_correction(c, pwm, 2, 0, 385, 0, 0, &k);

  for(i=0;i<n;i++)
  {
    core = i & 1;
    sum[core] += loop_code(adc(bm_level(core, v, 0), 1024), &k, core, &lfsr);
  }

  mean[0] = (double)sum[0] / (n / 2);
  mean[1] = (double)sum[1] / (n / 2);
}

//16-bit CIC2 at 1 MSa/s (N = 200, full-rate cores) with the readout's gain16 and corr16: the mean code before
//pc_decim_to_code rounds each sample to a whole code (that rounding has no dither; checked against it per sample)
static double bm_cic2(const acal_channel *c, const uint16_t *pwm, double v)
{
  pc_decim_cfg cfg;
  acal_corr k;
  double  acc = 0;
  int32_t i, j, m = 60;
  uint32_t scale;

  pc_decim_config(PC_DECIM_CIC2, 0, 199, 3000, PC_DECIM_HAS_CIC, &cfg);
  acal_correction(c, pwm, 2, 0, 385, 0, 0, &k);
  scale = acal_scale(cfg.scale_q24, k.gain16);

  for(i=0;i<m;i++)
  {
    uint64_t y = 0;
    double   q;

    //a constant input: CIC2 output = N^2 x the mean of the window's samples (half A, half B)
    for(j=0;j<200;j++)
      y += (uint64_t)adc(bm_level(j & 1, v, 0), 1024);

    y = (y * cfg.n) >> cfg.shift;
    q = ((double)y * scale + (double)k.corr16 * 1048576.0) / 16777216.0;
    T_EQ(pc_decim_to_code((uint16_t)y, scale, k.corr16), (int32_t)(q + 0.5));
    acc += q;
  }

  return acc / m;
}

static double dabs(double v)
{
  return (v < 0) ? -v : v;
}

static void test_bench_model(void)
{
  static const uint16_t pwm[7] = { 904, 900, 908, 903, 908, 903, 903 };
  acal_channel c;
  double  p[2], n[2], g_ref = (bm_gain[0] + bm_gain[1]) / 2, cic_p, cic_n;
  uint8_t r;
  int     k;

  rng = 4242;
  bm_bow = 0.0;

  //the calibration's zero step on the model gives these zeros (exact here) and slopes
  memset(&c, 0, sizeof(c));
  memcpy(c.pwm, pwm, sizeof(c.pwm));
  for(k=0;k<2;k++)
  {
    int32_t rr;
    for(rr=0;rr<ACAL_RANGES;rr++)
    {
      c.zero[k][rr]  = (int16_t)(bm_zero[k] * 16 + 0.5);
      c.slope[k][rr] = (int16_t)(bm_slope[k] * 65536 - 0.5);
    }
  }
  acal_gain_carry(0, &c);

  //Without gains (v2): the bench's numbers. A - B grows with the signal (0.91 code per volt, the fs/2 spur)
  bm_corrected(&c, pwm, 1.0045, p);
  bm_corrected(&c, pwm, -1.0045, n);
  T_TRUE(dabs((p[0] - p[1]) - 0.91) < 0.1);
  T_TRUE(dabs((n[0] - n[1]) + 0.91) < 0.1);
  T_TRUE(dabs((p[0] + p[1] - n[0] - n[1]) / 2 / 2.009 - 28.17) < 0.03);
  printf("bench model v2: A-B %+.3f / %+.3f, %.3f codes/V\n", p[0] - p[1], n[0] - n[1],
         (p[0] + p[1] - n[0] - n[1]) / 2 / 2.009);

  //The gain step at the post, no gains in use: G_k = mean gain / g_k (mean G 1)
  T_EQ(bm_gain_step(&c, 1, &r), ACAL_OK);
  T_EQ(r, 2);
  for(k=0;k<2;k++)
    T_TRUE(dabs(c.gain[k] / 32768.0 - g_ref / bm_gain[k]) < 0.0015);
  T_TRUE(c.gain_lever > 85 * 16 && c.gain_lever < 92 * 16);

  //With the gains: A - B under 0.1 code at +-1 V; the 8-bit path and CIC2 the mean codes per volt of both cores
  bm_corrected(&c, pwm, 1.0045, p);
  bm_corrected(&c, pwm, -1.0045, n);
  cic_p = bm_cic2(&c, pwm, 1.0045);
  cic_n = bm_cic2(&c, pwm, -1.0045);
  T_TRUE(dabs(p[0] - p[1]) < 0.1);
  T_TRUE(dabs(n[0] - n[1]) < 0.1);
  T_TRUE(dabs((p[0] + p[1]) / 2 - (128 + 1.0045 * g_ref)) < 0.1);
  T_TRUE(dabs((n[0] + n[1]) / 2 - (128 - 1.0045 * g_ref)) < 0.1);
  T_TRUE(dabs(cic_p - (128 + 1.0045 * g_ref)) < 0.1);
  T_TRUE(dabs(cic_n - (128 - 1.0045 * g_ref)) < 0.1);
  printf("bench model v3: A-B %+.3f / %+.3f, means %.2f / %.2f, CIC2 %.2f / %.2f (target %.2f / %.2f)\n",
         p[0] - p[1], n[0] - n[1], (p[0] + p[1]) / 2, (n[0] + n[1]) / 2, cic_p, cic_n, 128 + 1.0045 * g_ref,
         128 - 1.0045 * g_ref);

  //A unit calibrated by fw 0.2.45-0.2.104: its full-rate gains carry the gated scale its volts calibration was fitted
  //on (G_k = gated mean / g_k). Every capture is full rate now and reads on that scale with those gains, and a new gain
  //step keeps it (the mean G of the gains in use): within 0.1 % of the gated scale, A - B still under 0.1 code
  for(k=0;k<2;k++)
    c.gain[k] = (uint16_t)(32768.0 * bm_gated_ref / bm_gain[k] + 0.5);
  c.gain_flags = ACAL_GAIN_KEPT;
  bm_corrected(&c, pwm, 1.0045, p);
  bm_corrected(&c, pwm, -1.0045, n);
  T_TRUE(dabs((p[0] + p[1] - n[0] - n[1]) / 2 / 2.009 / bm_gated_ref - 1) < 0.001);
  T_EQ(bm_gain_step(&c, 1, &r), ACAL_OK);
  bm_corrected(&c, pwm, 1.0045, p);
  bm_corrected(&c, pwm, -1.0045, n);
  T_TRUE(dabs(p[0] - p[1]) < 0.1);
  T_TRUE(dabs((p[0] + p[1] - n[0] - n[1]) / 2 / 2.009 / bm_gated_ref - 1) < 0.001);
  printf("bench model, an older block's scale kept: %.3f codes/V (gated scale %.3f)\n",
         (p[0] + p[1] - n[0] - n[1]) / 2 / 2.009, bm_gated_ref);

  //Core A's bow (0.53 code at +-1 V, the bench's gated figure): with the levels centred the gain estimate stays within
  //0.15 %; uncentred (-35 / +53 codes) it is about 0.6 % off (A's span 1.2 % short, half of it in the reference). The
  //bow itself is INL and stays
  bm_bow = 0.53 / (1.0045 * 28.62) / (1.0045 * 28.62);
  acal_gain_carry(0, &c);
  T_EQ(bm_gain_step(&c, 1, &r), ACAL_OK);
  T_TRUE(dabs(c.gain[0] / 32768.0 - g_ref / bm_gain[0]) < 0.0015);
  {
    double centred = c.gain[0] / 32768.0;
    acal_gain_carry(0, &c);
    T_EQ(bm_gain_step(&c, 0, &r), ACAL_OK);
    T_TRUE(dabs(c.gain[0] / 32768.0 - g_ref / bm_gain[0]) > 0.004);
    printf("bench model, core A bow 0.53: gain A error %+.2f %% centred, %+.2f %% uncentred\n",
           (centred / (g_ref / bm_gain[0]) - 1) * 100, (c.gain[0] / 32768.0 / (g_ref / bm_gain[0]) - 1) * 100);
  }
  bm_bow = 0.0;

  //no probe on the post: both levels read the open input -> lever too small
  {
    acal_gain_meas m;
    int l;

    memset(&m, 0, sizeof(m));
    for(l=0;l<3;l++)
      bm_mean(1, 0.0, 0, 4, m.mean16[l]);
    T_EQ(acal_gain_store(&m, 2, &c), ACAL_E_GAIN_LEVER);
  }
}

//End to end: shorted input on the owner's CH1, simulated 8-bit readout loop as in fpga_read_adc_data (gain, offset,
//dither, clamp, + shift) and the rail rule, and the 16-bit boxcar path (pc_decim_to_code + feed): 0 V at T
static void test_end_to_end(void)
{
  uint16_t pwm[7] = { 904, 900, 908, 903, 908, 903, 903 };
  acal_channel c;
  acal_corr k;
  int32_t  d, zero1024[2], i, n = 20000;
  uint32_t lfsr = 1;
  int64_t  sum[2];

  unit_ch1(&c, pwm);

  for(d=-300;d<=300;d+=150)
    {
      acal_correction(&c, pwm, 3, (int16_t)d, 385, -2, 2, &k);

      //true zeros of the two cores at this offset (the model, with exact slopes)
      zero1024[0] = c.zero[0][3] * 64 + (int32_t)(((int64_t)c.slope[0][3] * d) / 64);
      zero1024[1] = c.zero[1][3] * 64 + (int32_t)(((int64_t)c.slope[1][3] * d) / 64);

      sum[0] = sum[1] = 0;

      for(i=0;i<n;i++)
      {
        int32_t core = i & 1;
        sum[core] += loop_code(adc(zero1024[core], 1000), &k, core, &lfsr);
      }

      //corrected core means equal T within 0.06 codes (noise 1 code / sqrt(10000) = 0.01, gain on the noise only)
      for(i=0;i<2;i++)
      {
        int64_t mean1024 = sum[i] * 1024 / (n / 2);
        int64_t t1024    = (int64_t)(128 - k.shift) * 1024;
        T_TRUE(mean1024 - t1024 < 64 && t1024 - mean1024 < 64);
      }
    }

  //rail rule (fpga_read_adc_data, core B pass): at or beyond the tighter core's limit both codes of the pair take the
  //more extreme one, so the cores saturate together
  {
    int32_t a, b, lo, hi;

    acal_correction(&c, pwm, 3, 0, 385, -2, 2, &k);
    lo = k.clip_lo;
    hi = k.clip_hi;
    T_TRUE(lo <= 0 && hi < 255);                              //both cores reach 0, core A tops out at 250

    for(a=0;a<256;a+=3)
      for(b=0;b<256;b+=5)
      {
        int32_t ra = a, rb = b;

        if((rb < lo) || (ra < lo))      { ra = rb = (ra < rb) ? ra : rb; }
        else if((rb > hi) || (ra > hi)) { ra = rb = (ra > rb) ? ra : rb; }

        if((a >= lo) && (b >= lo) && (a <= hi) && (b <= hi))
          T_TRUE(ra == a && rb == b);                         //inside both cores' reach: untouched
        else
          T_TRUE(ra == rb);
      }
  }

  //16-bit boxcar N = 200 at 1 MSa/s, both cores at full rate: out = sum of 100 A + 100 B samples >> S
  {
    pc_decim_cfg cfg;
    int64_t acc = 0, total = 0;
    int32_t j, m = 400;

    pc_decim_config(PC_DECIM_BOXCAR, 0, 199, 3000, PC_DECIM_HAS_CIC, &cfg);
    T_EQ(cfg.n, 200);
    acal_correction(&c, pwm, 3, 0, 385, -2, 2, &k);

    for(i=0;i<m;i++)
    {
      uint64_t y = 0;
      int32_t code;

      for(j=0;j<200;j++)
        y += (uint64_t)adc(c.zero[j & 1][3] * 64, 1000);

      y >>= cfg.shift;
      code = pc_decim_to_code((uint16_t)y, acal_scale(cfg.scale_q24, k.gain16), k.corr16);
      acc += code;
      total++;
    }

    {
      int64_t mean1024 = acc * 1024 / total;
      T_TRUE(mean1024 - 128 * 1024 < 80 && 128 * 1024 - mean1024 < 80);   //within 0.08 code
    }
  }
}

//--pack: the block of the vectors file's adc_cal_v3 example (gen_vectors.py adc_cal_v3_section) as hex
static int pack_example(void)
{
  acal_channel ch[2];
  uint8_t blk[ACAL_BLOCK_BYTES];
  uint32_t i;

  example_channels(ch);
  acal_pack(ch, blk);

  for(i=0;i<ACAL_BLOCK_BYTES;i++)
    printf("%02x", blk[i]);
  printf("\n");
  return 0;
}

//fw 0.2.47: the 16-bit trigger level goes through the same transform as the readout (gain16 in the scale, corr16).
//For every 8-bit level the stored-sample level must convert back to that level, and the old (uncorrected) conversion
//must be what remains with unity gain and no zero term.
static void test_trigger_level16(void)
{
  static const uint8_t kreq[] = { PC_DECIM_BOXCAR, PC_DECIM_CIC2, PC_DECIM_CIC3 };
  static const uint32_t div[] = { 1, 9, 199, 1999 };
  static const uint32_t g16[] = { 65536, 64880, 66192, 67700 };     //1.0, -1 %, +1 %, +3.3 %
  static const int32_t  c16[] = { 0, 37, -52, 300 };                 //1/16 code
  uint32_t a, b, g, z;
  int32_t  lv, worst = 0, moved = 0;

  for(a=0;a<sizeof(kreq);a++)
    for(b=0;b<sizeof(div)/sizeof(div[0]);b++)
    {
      pc_decim_cfg cfg;

      pc_decim_config(kreq[a], 0, div[b], 3000, PC_DECIM_HAS_CIC, &cfg);

      if(!cfg.k)
        continue;

      for(g=0;g<4;g++)
        for(z=0;z<4;z++)
        {
          uint32_t scale = acal_scale(cfg.scale_q24, g16[g]);

          for(lv=0;lv<256;lv++)
          {
            uint16_t L = pc_decim_level16_corr((uint8_t)lv, scale, c16[z]);
            int32_t  back = pc_decim_to_code(L, scale, c16[z]);
            int32_t  lo = c16[z] / 16 - 1, hi = c16[z] / 16 + (int32_t)(65535ull * scale >> 24) + 1;
            int32_t  d = back - lv;

            //Inside the representable span the round trip is exact; outside it clamps to the ends
            if((lv > lo + 1) && (lv < hi - 1))
            {
              T_EQ(back, lv);
              if(d < 0) d = -d;
              if(d > worst) worst = d;
            }

            //Unity gain, no zero term: the old conversion, to within one 16-bit step of the rounded scale
            if((g == 0) && (z == 0))
            {
              int32_t old = pc_decim_level16((uint8_t)lv, cfg.gain, cfg.shift);
              T_TRUE((L - old <= 1) && (old - L <= 1));
            }
            else if(L != pc_decim_level16((uint8_t)lv, cfg.gain, cfg.shift))
              moved++;
          }
        }
    }

  //The owner's full-rate case (fw-cal2, README 6.3): gain16 about 1 % off unity moves a mid-screen level by about one
  //code in the 8-bit domain, i.e. the old conversion put the trigger about 1 code (1 %) away from the trace
  {
    pc_decim_cfg cfg;
    uint32_t s1;
    int32_t  e;

    pc_decim_config(PC_DECIM_CIC2, 0, 199, 3000, PC_DECIM_HAS_CIC, &cfg);
    s1 = acal_scale(cfg.scale_q24, 64880);
    e  = pc_decim_to_code(pc_decim_level16(200, cfg.gain, cfg.shift), s1, 0) - 200;
    T_TRUE((e <= -1) && (e >= -3));
    T_EQ(pc_decim_to_code(pc_decim_level16_corr(200, s1, 0), s1, 0), 200);
  }

  T_EQ(worst, 0);
  T_TRUE(moved > 0);
  T_EQ(pc_decim_level16_corr(0, 1u << 24, 16), 0);                 //below the zero term: clamps to 0
  T_EQ(pc_decim_level16_corr(255, 1u << 10, 0), 65535);             //tiny scale: clamps to the 16-bit top
  T_EQ(pc_decim_level16_corr(128, 0, 0), 0);                        //no scale (K = 0): 0, never a division by zero
}

//fw 0.2.67: per-point extremes, the failure point and the CAL_PROBE body
static void test_points(void)
{
  static const uint8_t a0[6] = { 128, 129, 130, 131, 130, 129 }, a1[6] = { 126, 255, 125, 127, 126, 0 };
  acal_acc   acc[2][2];
  acal_point pt[ACAL_POINTS];
  acal_fail  f;
  uint8_t    b[ACAL_PROBE_BYTES + 8];
  uint32_t   n;

  //lo / hi per accumulator, kept by a merge with an empty one and combined by a merge of two
  memset(acc, 0, sizeof(acc));
  acal_acc_add(&acc[0][ACAL_CORE_A], a0, 6, 1);
  T_EQ(acc[0][0].lo, 128); T_EQ(acc[0][0].hi, 131);
  acal_acc_add(&acc[1][ACAL_CORE_A], a0 + 2, 3, 1);
  T_EQ(acc[1][0].lo, 130); T_EQ(acc[1][0].hi, 131);
  acal_acc_add(&acc[0][ACAL_CORE_B], a1, 3, 1);
  acal_acc_add(&acc[1][ACAL_CORE_B], a1 + 3, 3, 1);
  T_EQ(acc[0][1].lo, 125); T_EQ(acc[0][1].hi, 255); T_EQ(acc[1][1].lo, 0); T_EQ(acc[1][1].hi, 127);

  memset(pt, 0, sizeof(pt));
  acal_point_set(&pt[ACAL_PT_F_HI], 962, ACAL_RATE_FULL, 2, acc);
  T_EQ(pt[ACAL_PT_F_HI].pwm, 962); T_EQ(pt[ACAL_PT_F_HI].rate, ACAL_RATE_FULL); T_EQ(pt[ACAL_PT_F_HI].convs, 2);
  T_EQ(pt[ACAL_PT_F_HI].lo[0], 128); T_EQ(pt[ACAL_PT_F_HI].hi[0], 131);
  T_EQ(pt[ACAL_PT_F_HI].lo[1], 0); T_EQ(pt[ACAL_PT_F_HI].hi[1], 255);
  T_EQ(pt[ACAL_PT_F_HI].clipped[0], 0); T_EQ(pt[ACAL_PT_F_HI].clipped[1], 2);
  T_EQ(pt[ACAL_PT_F_HI].mean16[0], ((777 + 391) * 16 + 4) / 9);
  T_TRUE(pt[ACAL_PT_F_HI].rms16[1] > 16 * 70);

  //Clip: the first measured point after the coarse ones, its rate and the core with more clipped samples; a clipped
  //coarse point is never named; a 2 MSa/s slope point before a 200 MSa/s one
  pt[ACAL_PT_COARSE_HI] = pt[ACAL_PT_F_HI];
  pt[ACAL_PT_COARSE_HI].rate = ACAL_RATE_SEARCH;
  memset(&f, 0, sizeof(f));
  acal_fail_point(pt, ACAL_E_CLIP, &f);
  T_EQ(f.point, ACAL_PT_F_HI); T_EQ(f.pwm, 962); T_EQ(f.rate, ACAL_RATE_FULL); T_EQ(f.core, ACAL_CORE_B);
  T_EQ(f.clipped[1], 2); T_EQ(f.lo[1], 0); T_EQ(f.hi[1], 255); T_EQ(f.mean16[0], pt[ACAL_PT_F_HI].mean16[0]);
  pt[ACAL_PT_S_HI] = pt[ACAL_PT_COARSE_HI];
  acal_fail_point(pt, ACAL_E_CLIP, &f);
  T_EQ(f.point, ACAL_PT_S_HI); T_EQ(f.rate, ACAL_RATE_SEARCH);
  memset(&pt[ACAL_PT_S_HI], 0, sizeof(acal_point));

  //Noise: the 200 MSa/s zero point, not measured here -> none; measured -> it
  memset(&f, 0, sizeof(f));
  f.core = 1; f.value = 115;
  acal_fail_point(pt, ACAL_E_NOISE, &f);
  T_EQ(f.point, ACAL_PT_NONE);
  pt[ACAL_PT_F_ZERO] = pt[ACAL_PT_F_HI];
  pt[ACAL_PT_F_ZERO].pwm = 812;
  acal_fail_point(pt, ACAL_E_NOISE, &f);
  T_EQ(f.point, ACAL_PT_F_ZERO); T_EQ(f.pwm, 812); T_EQ(f.rate, ACAL_RATE_FULL); T_EQ(f.core, 1); T_EQ(f.value, 115);

  //Response: the coarse point and its rate; OK: nothing
  acal_fail_point(pt, ACAL_E_RESPONSE, &f);
  T_EQ(f.point, ACAL_PT_COARSE_HI); T_EQ(f.rate, ACAL_RATE_SEARCH);
  acal_fail_point(pt, ACAL_OK, &f);
  T_EQ(f.point, ACAL_PT_NONE);

  //acal_range_check starts with no point
  {
    acal_range_meas m;

    memset(&m, 0, sizeof(m));
    m.clipped = 1;
    f.point = 3;
    T_EQ(acal_range_check(&m, &f), ACAL_E_CLIP);
    T_EQ(f.point, ACAL_PT_NONE); T_EQ(f.value, 0);
  }

  //CAL_PROBE body: 248 bytes, header then 28 bytes per point
  f.core = 1; f.value = 115; f.within16 = 113;
  acal_fail_point(pt, ACAL_E_NOISE, &f);
  memset(b, 0xAA, sizeof(b));
  n = acal_probe_pack(b, 1, 4, ACAL_E_NOISE, ACAL_PROBE_GARBLED, 800, 812, 3, 2, &f, pt);
  T_EQ(n, ACAL_PROBE_BYTES); T_EQ(n, 252); T_EQ(b[252], 0xAA); T_EQ(b[248], 0); T_EQ(b[249], 0); T_EQ(b[250], 0);
  T_EQ(b[0], 1); T_EQ(b[1], 4); T_EQ(b[2], ACAL_E_NOISE); T_EQ(b[3], 0);
  T_EQ(b[4], ACAL_PT_F_ZERO); T_EQ(b[5], ACAL_RATE_FULL); T_EQ(b[6], 1); T_EQ(b[7], ACAL_PROBE_GARBLED);
  T_EQ(b[8], 115); T_EQ(b[12], 113);
  T_EQ(b[16] | (b[17] << 8), 800); T_EQ(b[18] | (b[19] << 8), 812); T_EQ(b[20], 3); T_EQ(b[22], 2);
  {
    const uint8_t *q = b + 24 + 28 * ACAL_PT_F_HI;

    T_EQ(q[0] | (q[1] << 8), 962); T_EQ(q[2], ACAL_RATE_FULL); T_EQ(q[3], 2);
    T_EQ((int32_t)(q[4] | (q[5] << 8) | (q[6] << 16) | ((uint32_t)q[7] << 24)), pt[ACAL_PT_F_HI].mean16[0]);
    T_EQ(q[12], 128); T_EQ(q[13], 131); T_EQ(q[14] | (q[15] << 8), 0);
    T_EQ(q[24], 0); T_EQ(q[25], 255); T_EQ(q[26] | (q[27] << 8), 2);
    q = b + 24 + 28 * ACAL_PT_S_LO;
    T_EQ(q[3], 0);                                              //not measured
    q = b + 24 + 28 * 4;
    T_EQ(q[3], 0);                                              //point 4 (the gated zero up to fw 0.2.104): never
  }
}

//fw 0.2.67, bench 2026-10-19 on FPGA 0x1538 (fw-cal5): at 200 MSa/s some readout lanes mix the bits of successive samples
//that straddle 0x7F / 0x80; READ_RAW showed outliers up to 255 only while a core's mean was 126..133. Model: bit 7 from
//the new sample, bits 6..0 from the one before, whenever two successive codes lie on either side of 127.5
static uint8_t rd_prev;

static uint8_t readout_mixed(int32_t v)
{
  uint8_t c = (uint8_t)v, out = c;

  if((c ^ rd_prev) & 0x80)
    out = (uint8_t)((c & 0x80) | (rd_prev & 0x7F));

  rd_prev = c;
  return out;
}

//convs conversions of 500 samples per core at a true level (1/1024 code), noise 1 code, through the faulty readout
//(mixed) or a clean one, into acc[half][core] as cal_measure fills it
static void fault_measure(int32_t level1024, uint32_t convs, int mixed, acal_acc acc[2][2])
{
  uint8_t  buf[1000];
  uint32_t i, j, k;

  memset(acc, 0, 4 * sizeof(acal_acc));

  for(i=0;i<convs;i++)
  {
    for(k=0;k<2;k++)
    {
      rd_prev = (uint8_t)(level1024 >> 10);

      for(j=0;j<500;j++)
      {
        int32_t v = adc(level1024, 1024);

        buf[2 * j + (k ? 0 : 1)] = mixed ? readout_mixed(v) : (uint8_t)v;
      }
    }

    acal_acc_add(&acc[i >= convs / 2][ACAL_CORE_A], buf + 1, 500, 2);
    acal_acc_add(&acc[i >= convs / 2][ACAL_CORE_B], buf, 500, 2);
  }
}

static void test_readout_fault(void)
{
  //Range 0 of the bench: zero 127.6 codes at the centre PWM 860, -0.44 codes per count (the READ_RAW sweep)
  const int32_t centre = 860, z1024 = 127 * 1024 + 614, slope1024 = -451;   //-0.44 code per count
  acal_acc   acc[2][2];
  uint32_t   clipped = 0;

#define LEVEL(pwm) (z1024 + slope1024 * ((pwm) - centre))
  rng = 777;

  //The zero point parked at the centre, where the level straddles 127.5 (0.2.66 and again since 0.2.79, the owner's
  //fail-fast decision): a faulty readout gives rail samples and several codes of noise (the 17:55 failure lines) and
  //fails the step loudly, while a clean readout of the same level is fine
  fault_measure(LEVEL(centre), 64, 1, acc);
  clipped = acc[0][0].clipped + acc[1][0].clipped + acc[0][1].clipped + acc[1][1].clipped;
  T_TRUE(clipped > 0);
  {
    acal_acc all = acc[0][1];

    acal_acc_merge(&all, &acc[1][1]);
    T_TRUE(acal_rms16(&all) > ACAL_MAX_NOISE16);
    T_EQ(all.hi, 255);
  }
  fault_measure(LEVEL(centre), 64, 0, acc);
  T_EQ(acc[0][0].clipped + acc[1][1].clipped, 0);
  //fw 0.2.105: the step fails with ACAL_E_CLIP at the zero point (fail fast). The "gated twin" rule that called it
  //ACAL_E_READOUT (fw 0.2.67-0.2.104) needed the gated class; ACAL_E_READOUT is now the capture-stage training's
#undef LEVEL
}

int main(int argc, char **argv)
{
  if((argc > 1) && !strcmp(argv[1], "--pack"))
    return pack_example();

  test_crc_and_block();
  test_round_pair();
  test_correction();
  test_measurement();
  test_zero_noise_bench();
  test_gain_step();
  test_bench_model();
  test_end_to_end();
  test_trigger_level16();
  test_points();
  test_readout_fault();
  return t_report("test_adc_cal");
}
