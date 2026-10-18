//----------------------------------------------------------------------------------------------------------------------------------
// spectrum.c - on-scope spectrum analyser (protocol builds only). fw 0.2.27: the FFT benchmark (FFT_BENCH, C2).
//----------------------------------------------------------------------------------------------------------------------------------

#include "spectrum.h"
#include "fx_fft.h"
#include "timer.h"
#include "variables.h"

//Work memory in DDR (.bss, uncached: the MMU and so the D-cache are off)
static int32 spec_win[FX_FFT_WIN_WORDS(FX_FFT_LOG2_MAX)];
static int32 spec_buf[FX_FFT_BUF_WORDS(FX_FFT_LOG2_MAX)];
static int16 spec_cdb[(FX_FFT_N_MAX / 2) + 1];

//On-chip SRAM A (32 KB at 0): at run time only the low vectors (0x00-0x3F) live there; the bootloader's stack (and the
//one ENTER_FEL sets up, 0x7C00) sit at the top. The benchmark borrows 0x1000-0x7000 for N <= 4096 (24,588 bytes).
#define SPEC_SRAM_BASE        ((int32 *)0x00001000)
#define SPEC_SRAM_LOG2_MAX    12

//----------------------------------------------------------------------------------------------------------------------------------

uint32 spectrum_us(void)
{
  uint32 t, c, pend;

  //Consistent pair: retry when the tick changed under us
  do
  {
    t    = timer0ticks;
    c    = *TMR0_CUR_VALUE_REG;
    pend = *TMR_IRQ_STA_REG & 1;
  } while(t != timer0ticks);

  //Reloaded but the tick interrupt not yet taken (IRQs masked or just raised): the counter is near the top again
  if(pend && (c > 12000))
    t++;

  return (t * 1000) + ((24000 - c) / 24);
}

static uint32 cp15_control(void)
{
  uint32 v;

  __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
  return v;
}

static void put16(uint8 *p, uint32 v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8 *p, uint32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

//----------------------------------------------------------------------------------------------------------------------------------
//One entry (24 bytes): u16 N, u8 window, u8 memory (0 DDR, 1 SRAM), u32 init us, u32 run min us, u32 run max us,
//u32 dB conversion min us (N/2 + 1 bins), u16 peak bin (DC excluded), i16 peak level (centi-dB re 1 code RMS)

static uint8 *bench_one(uint8 *o, int log2n, int sram, uint32 reps)
{
  FX_FFT  p;
  int32  *win = sram ? SPEC_SRAM_BASE : spec_win;
  int32  *buf = sram ? SPEC_SRAM_BASE + FX_FFT_WIN_WORDS(log2n) : spec_buf;
  uint32  N = 1u << log2n, half = N / 2, i, k, peak = 1;
  uint32  t0, t_init, t_run, run_min = 0xFFFFFFFF, run_max = 0, cdb_min = 0xFFFFFFFF;
  int16   pv;

  t0 = spectrum_us();
  fx_fft_init(&p, log2n, FX_WIN_HANN, win, buf);
  t_init = spectrum_us() - t0;

  for(i = 0; i < reps; i++)
  {
    t0 = spectrum_us();
    fx_fft_run_u8(&p, (const uint8 *)channel1tracebuffer, 128);
    t_run = spectrum_us() - t0;

    if(t_run < run_min) run_min = t_run;
    if(t_run > run_max) run_max = t_run;

    t0 = spectrum_us();
    fx_fft_cdb(&p, 0, half + 1, spec_cdb);
    t_run = spectrum_us() - t0;

    if(t_run < cdb_min) cdb_min = t_run;
  }

  for(k = 2; k <= half; k++)
    if(spec_cdb[k] > spec_cdb[peak])
      peak = k;
  pv = spec_cdb[peak];

  put16(o, N); o[2] = FX_WIN_HANN; o[3] = sram;
  put32(o + 4, t_init); put32(o + 8, run_min); put32(o + 12, run_max); put32(o + 16, cdb_min);
  put16(o + 20, peak); put16(o + 22, (uint16)pv);
  return o + 24;
}

uint32 spectrum_bench(uint8 *out, uint32 reps)
{
  static const uint8 ddr[]  = { 10, 11, 12, 14 };
  static const uint8 sram[] = { 10, 11, 12 };
  uint8  *o = out + 8;
  uint32  i;

  for(i = 0; i < sizeof(ddr); i++)
    o = bench_one(o, ddr[i], 0, reps);

  for(i = 0; i < sizeof(sram); i++)
    if(sram[i] <= SPEC_SRAM_LOG2_MAX)
      o = bench_one(o, sram[i], 1, reps);

  put16(out, (uint32)(o - out - 8) / 24);
  put16(out + 2, reps);
  put32(out + 4, cp15_control());
  return (uint32)(o - out);
}
