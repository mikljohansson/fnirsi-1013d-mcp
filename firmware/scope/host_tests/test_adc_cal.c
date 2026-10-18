// test_adc_cal.c - per-core ADC zero calibration (adc_cal.c): sector block, correction maths, measurement checks, and an
// end-to-end model of the 8-bit readout loop (dither) and the 16-bit filter path, run natively.

#include <stdio.h>
#include <string.h>

#include "adc_cal.h"
#include "pc_decim.h"
#include "test_util.h"

static uint32_t rng = 12345;

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

//The owner's CH1 (bench 2026-10-18): gated A 130.9 / B 125.6, full rate A 131.7 / B 129.2, slopes -0.2641 / -0.2598
static void unit_ch1(acal_channel *c, const uint16_t *pwm)
{
  uint32_t r;

  memset(c, 0, sizeof(*c));

  for(r=0;r<ACAL_RANGES;r++)
  {
    c->pwm[r] = pwm[r];
    c->zero[ACAL_GATED][0][r] = (int16_t)(130.9 * 16);
    c->zero[ACAL_GATED][1][r] = (int16_t)(125.6 * 16);
    c->zero[ACAL_FULL][0][r]  = (int16_t)(131.7 * 16);
    c->zero[ACAL_FULL][1][r]  = (int16_t)(129.2 * 16);
    c->slope[0][r] = -17308;
    c->slope[1][r] = -17026;
    c->noise[ACAL_GATED][0][r] = 10;
    c->noise[ACAL_GATED][1][r] = 21;
  }
}

static void test_crc_and_block(void)
{
  uint8_t  blk[ACAL_BLOCK_BYTES], b2[ACAL_BLOCK_BYTES];
  acal_channel in[2], out[2];
  uint16_t pwm[7] = { 904, 900, 908, 903, 908, 903, 903 };
  uint32_t i;

  //CRC-32/ISO-HDLC check value (zlib.crc32(b"123456789"))
  T_EQ(acal_crc32((const uint8_t *)"123456789", 9), 0xCBF43926u);

  T_EQ(ACAL_BLOCK_BYTES, 328);
  T_TRUE(ACAL_SECTOR_OFFSET >= 120);                          //after Atlan's checksummed bytes 16..119
  T_TRUE(ACAL_SECTOR_OFFSET + ACAL_BLOCK_BYTES <= 512);

  unit_ch1(&in[0], pwm);
  unit_ch1(&in[1], pwm);
  in[1].zero[ACAL_FULL][1][4] = -77;
  in[1].slope[0][5] = -20000;
  in[1].noise[ACAL_FULL][1][2] = 47;

  memset(blk, 0xEE, sizeof(blk));
  acal_pack(in, blk);
  T_EQ(blk[0], 'A'); T_EQ(blk[1], 'C'); T_EQ(blk[2], 'A'); T_EQ(blk[3], 'L');
  T_EQ(blk[4], 2); T_EQ(blk[5], 0);                           //version
  T_EQ(blk[6] | (blk[7] << 8), ACAL_BLOCK_BYTES);
  T_EQ(blk[8], 2); T_EQ(blk[9], 0); T_EQ(blk[10], 0); T_EQ(blk[11], 0);
  T_EQ(blk[12] | (blk[13] << 8), 904);                        //CH1 pwm[0]
  T_EQ(blk[ACAL_HDR_BYTES + ACAL_CH_BYTES] | (blk[ACAL_HDR_BYTES + ACAL_CH_BYTES + 1] << 8), 904);   //CH2 pwm[0]
  //CH1 zero[full][A][0] right after pwm: 131.7 x 16 = 2107
  T_EQ(blk[24] | (blk[25] << 8), 2107);

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
  memcpy(b2, blk, sizeof(b2)); b2[4] = 3;
  T_EQ(acal_unpack(b2, out), ACAL_E_VERSION);
  memcpy(b2, blk, sizeof(b2)); b2[8] = 3;
  T_EQ(acal_unpack(b2, out), ACAL_E_VERSION);
  memcpy(b2, blk, sizeof(b2)); b2[100] ^= 1;
  T_EQ(acal_unpack(b2, out), ACAL_E_CRC);

  T_TRUE(acal_bound(&in[0], pwm));
  pwm[3] = 860;
  T_TRUE(!acal_bound(&in[0], pwm));
}

static void test_round_pair(void)
{
  int32_t da, db, worst_spread = 0, worst_mean = 0;
  int16_t a, b;

  for(da=-600;da<=600;da+=3)
    for(db=-600;db<=600;db+=5)
    {
      int32_t ra = da - 16 * 0, rb;
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
  int32_t d;

  unit_ch1(&c, pwm);

  //1.00o5 fallback: no block, or not bound
  acal_correction(0, pwm, 3, 99, 0, 385, -2, 2, &k);
  T_EQ(k.v2, 0); T_EQ(k.comp_a, -2); T_EQ(k.comp_b, 2); T_EQ(k.frac_a, 0); T_EQ(k.frac_b, 0);
  T_EQ(k.zero_q8, 128 * 256); T_EQ(k.corr16, 0); T_EQ(k.shift, 0);
  acal_correction(0, pwm, 3, 99, 100, 385, -2, 3, &k);
  T_EQ(k.shift, 25);                                          //100 x 100 / 385 = 25.97, C truncation
  T_EQ(k.zero_q8, 128 * 256 - 6649);                          //1.00o5 formula, 25600 x 100 / 385 = 6649.35
  T_EQ(k.corr16, 8);
  pwm[0] = 1;
  acal_correction(&c, pwm, 3, 99, 0, 385, -2, 2, &k);
  T_EQ(k.v2, 0);
  pwm[0] = 904;

  //v2 at dcoffset 0, gated: corrections 128 - 130.9 = -2.9 -> -3 + 1/16 ... (-46.4 sixteenths: zero stored 2094)
  acal_correction(&c, pwm, 3, 99, 0, 385, -2, 2, &k);
  T_EQ(k.v2, 1); T_EQ(k.cls, ACAL_GATED);
  T_EQ(16 * k.comp_a + k.frac_a, 2048 - c.zero[ACAL_GATED][0][3]);
  T_EQ(16 * k.comp_b + k.frac_b, 2048 - c.zero[ACAL_GATED][1][3]);
  T_TRUE(k.frac_a < 16 && k.frac_b < 16);
  T_EQ(k.zero_q8, 128 * 256);
  T_EQ(k.corr16, 2048 - (c.zero[ACAL_FULL][0][3] + c.zero[ACAL_FULL][1][3] + 1) / 2);

  //full rate (divider 0) uses the full-rate zeros
  acal_correction(&c, pwm, 3, 0, 0, 385, -2, 2, &k);
  T_EQ(k.cls, ACAL_FULL);
  T_EQ(16 * k.comp_a + k.frac_a, 2048 - c.zero[ACAL_FULL][0][3]);
  T_EQ(16 * k.comp_b + k.frac_b, 2048 - c.zero[ACAL_FULL][1][3]);

  //range 6 uses range 5
  c.zero[ACAL_GATED][0][5] = 2100;
  acal_correction(&c, pwm, 6, 99, 0, 385, -2, 2, &k);
  T_EQ(16 * k.comp_a + k.frac_a, 2048 - 2100);

  //offset: zero_k + slope_k x dcoffset moved to T = 128 - shift, for every dcoffset in the UI range
  for(d=-600;d<=600;d+=37)
  {
    int32_t za, zb;
    acal_correction(&c, pwm, 3, 99, (int16_t)d, 385, -2, 2, &k);
    za = c.zero[ACAL_GATED][0][3] + off16(c.slope[0][3], d);
    zb = c.zero[ACAL_GATED][1][3] + off16(c.slope[1][3], d);
    T_EQ(za + 16 * k.comp_a + k.frac_a, 16 * (128 - k.shift));
    T_EQ(zb + 16 * k.comp_b + k.frac_b, 16 * (128 - k.shift));
    T_EQ(k.zero_q8, 256 * (128 - k.shift));
  }

  //1.00o5 fields from v2 zeros (gated, averaged over the ranges)
  unit_ch1(&c, pwm);
  acal_atlan_comps(&c, &ca, &cb);
  T_EQ(ca, -3);                                               //130.9 -> 127.9
  T_EQ(cb, 2);                                                //125.6 -> 127.6
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
  m.mean16_lo[0] = 2095 + 634; m.mean16_hi[0] = 2095 - 634;   //-0.2641 codes per count over 300 counts
  m.mean16_lo[1] = 2010 + 623; m.mean16_hi[1] = 2010 - 623;
  m.zero16[ACAL_GATED][0] = 2095; m.zero16[ACAL_GATED][1] = 2010;
  m.zero16[ACAL_FULL][0]  = 2107; m.zero16[ACAL_FULL][1]  = 2067;
  m.noise16[ACAL_GATED][0] = 10; m.noise16[ACAL_GATED][1] = 21;
  m.noise16[ACAL_FULL][0]  = 9;  m.noise16[ACAL_FULL][1]  = 18;
  T_EQ(acal_range_store(&m, &c, 3), ACAL_OK);
  T_EQ(c.pwm[3], 903);
  T_EQ(c.slope[0][3], -17312);                                //-1268 x 4096 / 300
  T_EQ(c.slope[1][3], -17012);
  T_EQ(c.zero[ACAL_FULL][1][3], 2067);
  T_EQ(c.noise[ACAL_GATED][1][3], 21);

  {
    acal_range_meas x;
    acal_channel before = c;

    x = m; x.clipped = 1;                         T_EQ(acal_range_store(&x, &c, 2), ACAL_E_CLIP);
    x = m; x.noise16[ACAL_FULL][1] = 49;          T_EQ(acal_range_store(&x, &c, 2), ACAL_E_NOISE);
    x = m; x.drift16[ACAL_GATED][0] = -9;         T_EQ(acal_range_store(&x, &c, 2), ACAL_E_DRIFT);
    x = m; x.mean16_hi[0] = x.mean16_lo[0] - 900; T_EQ(acal_range_store(&x, &c, 2), ACAL_E_SLOPE);  //-0.1875
    x = m; x.mean16_hi[1] = x.mean16_lo[1] - 1450; T_EQ(acal_range_store(&x, &c, 2), ACAL_E_SLOPE); //A/B 0.038 apart
    x = m; x.zero16[ACAL_GATED][1] = 2048 - 257;  T_EQ(acal_range_store(&x, &c, 2), ACAL_E_ZERO);
    x = m; x.zero16[ACAL_FULL][0] = 2048 + 170; x.zero16[ACAL_FULL][1] = 2048 - 170;
                                                  T_EQ(acal_range_store(&x, &c, 2), ACAL_E_SPREAD);
    x = m; x.zero16[ACAL_FULL][0] = 2095 + 193;   T_EQ(acal_range_store(&x, &c, 2), ACAL_E_CLASS);
    T_MEM(&before, &c, sizeof(c));                            //nothing stored on a reject
  }

  T_STR(acal_reason_text(ACAL_E_NOISE), "noisy: unplug probe");
  for(i=0;i<=12;i++)
    T_TRUE(strlen(acal_reason_text((int)i)) <= 21);
  T_EQ(acal_interp_pwm(753, 2048 + 634, 1053, 2048 - 634), 903);
  T_EQ(acal_interp_pwm(753, 2000, 1053, 2000), 753);
  T_STR(acal_reason_text(99), "no calibration data");
}

//End to end: shorted input on the owner's CH1, simulated 8-bit readout loop as in fpga_read_adc_data (comp + dither,
//clamp, + shift), and the 16-bit boxcar path (pc_decim_to_code + feed): both must put 0 V at T.
static void test_end_to_end(void)
{
  uint16_t pwm[7] = { 904, 900, 908, 903, 908, 903, 903 };
  acal_channel c;
  acal_corr k;
  int32_t  d, zero1024[2], i, n = 20000, cls;
  uint32_t lfsr = 1;
  int64_t  sum[2];

  unit_ch1(&c, pwm);

  for(cls=0;cls<2;cls++)
    for(d=-300;d<=300;d+=150)
    {
      uint32_t divider = cls ? 199 : 0;

      acal_correction(&c, pwm, 3, divider, (int16_t)d, 385, -2, 2, &k);

      //true zeros of the two cores at this offset (the model, with exact slopes)
      zero1024[0] = c.zero[cls][0][3] * 64 + (int32_t)(((int64_t)c.slope[0][3] * d) / 64);
      zero1024[1] = c.zero[cls][1][3] * 64 + (int32_t)(((int64_t)c.slope[1][3] * d) / 64);

      sum[0] = sum[1] = 0;

      for(i=0;i<n;i++)
      {
        int32_t core = i & 1;
        int32_t s = adc(zero1024[core], 1000);
        uint32_t frac = core ? k.frac_b : k.frac_a;

        ACAL_DITHER_STEP(lfsr);
        s += (core ? k.comp_b : k.comp_a) + ((lfsr >> 28) < frac);
        s  = (s < 0) ? 0 : ((s > 255) ? 255 : s);
        sum[core] += s;
      }

      //corrected core means equal T within 0.06 codes (noise 1 code / sqrt(10000) = 0.01, dither exact to 1/16)
      for(i=0;i<2;i++)
      {
        int64_t mean1024 = sum[i] * 1024 / (n / 2);
        int64_t t1024    = (int64_t)(128 - k.shift) * 1024;
        T_TRUE(mean1024 - t1024 < 64 && t1024 - mean1024 < 64);
      }
    }

  //16-bit boxcar N = 200 at 1 MSa/s, both cores at full rate: out = sum of 100 A + 100 B samples >> S
  {
    pc_decim_cfg cfg;
    int64_t acc = 0, total = 0;
    int32_t j, m = 400;

    pc_decim_config(PC_DECIM_BOXCAR, 0, 199, 3000, 1, &cfg);
    T_EQ(cfg.n, 200);
    acal_correction(&c, pwm, 3, 199, 0, 385, -2, 2, &k);

    for(i=0;i<m;i++)
    {
      uint64_t y = 0;
      int32_t code;

      for(j=0;j<200;j++)
        y += (uint64_t)adc(c.zero[ACAL_FULL][j & 1][3] * 64, 1000);

      y >>= cfg.shift;
      code = pc_decim_to_code((uint16_t)y, cfg.scale_q24, k.corr16);
      //fpga_read_adc_data adds comp per core after the glue took it off: net code
      acc += code;
      total++;
    }

    {
      int64_t mean1024 = acc * 1024 / total;
      T_TRUE(mean1024 - 128 * 1024 < 80 && 128 * 1024 - mean1024 < 80);   //within 0.08 code
    }
  }
}

//--pack: the block of the vectors file's adc_cal example (gen_vectors.py adc_cal_section) as hex, for check_vectors.py
static int pack_example(void)
{
  uint16_t pwm[7] = { 904, 900, 908, 903, 908, 903, 903 };
  acal_channel ch[2];
  uint8_t blk[ACAL_BLOCK_BYTES];
  uint32_t i;

  unit_ch1(&ch[0], pwm);
  unit_ch1(&ch[1], pwm);
  ch[1].zero[ACAL_FULL][1][4] = -77;
  ch[1].slope[0][5] = -20000;
  ch[1].noise[ACAL_FULL][1][2] = 47;
  acal_pack(ch, blk);

  for(i=0;i<ACAL_BLOCK_BYTES;i++)
    printf("%02x", blk[i]);
  printf("\n");
  return 0;
}

int main(int argc, char **argv)
{
  if((argc > 1) && !strcmp(argv[1], "--pack"))
    return pack_example();

  test_crc_and_block();
  test_round_pair();
  test_correction();
  test_measurement();
  test_end_to_end();
  return t_report("test_adc_cal");
}
