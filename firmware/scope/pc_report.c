//----------------------------------------------------------------------------------------------------------------------------------
// pc_report.c - diagnostics report text (pc_report.h, protocol row 55)
//----------------------------------------------------------------------------------------------------------------------------------

//Not a hot path: optimise for size (at -O3 the inlined text helpers cost about 120 KB)
#pragma GCC optimize ("Os")

#include <string.h>
#include "cap_stage.h"
#include "pc_report.h"
#include "pc_ident.h"

//----------------------------------------------------------------------------------------------------------------------------------
// Text builder

void pc_text_init(pc_text *t, char *buf, uint32_t max)
{
  t->buf  = buf;
  t->max  = max;
  t->len  = 0;
  t->full = 0;

  if(max)
    buf[0] = 0;
}

static void pc_text_ch(pc_text *t, char c)
{
  if(t->len + 1 < t->max)
  {
    t->buf[t->len++] = c;
    t->buf[t->len]   = 0;
  }
  else
    t->full = 1;
}

void pc_text_str(pc_text *t, const char *s)
{
  while(*s)
    pc_text_ch(t, *s++);
}

void pc_text_udec(pc_text *t, uint32_t v)
{
  char d[10];
  int  n = 0;

  do
  {
    d[n++] = (char)('0' + v % 10);
    v /= 10;
  } while(v);

  while(n)
    pc_text_ch(t, d[--n]);
}

void pc_text_dec(pc_text *t, int32_t v)
{
  if(v < 0)
  {
    pc_text_ch(t, '-');
    pc_text_udec(t, 0u - (uint32_t)v);
  }
  else
    pc_text_udec(t, (uint32_t)v);
}

void pc_text_hex(pc_text *t, uint32_t v, uint32_t digits)
{
  static const char hx[] = "0123456789abcdef";

  pc_text_str(t, "0x");

  while(digits--)
    pc_text_ch(t, hx[(v >> (4 * digits)) & 0xF]);
}

void pc_text_fix(pc_text *t, int32_t v, uint32_t denom, uint32_t decimals)
{
  uint64_t scale = 1, a = (v < 0) ? (uint64_t)(0u - (uint32_t)v) : (uint64_t)v, q, f;
  uint32_t i;

  for(i=0;i<decimals;i++)
    scale *= 10;

  q = (a * scale + denom / 2) / denom;
  f = q % scale;
  q /= scale;

  if((v < 0) && (q || f))
    pc_text_ch(t, '-');

  pc_text_udec(t, (uint32_t)q);

  if(decimals)
  {
    char d[12];

    pc_text_ch(t, '.');

    for(i=decimals;i>0;i--)
    {
      d[i - 1] = (char)('0' + f % 10);
      f /= 10;
    }

    for(i=0;i<decimals;i++)
      pc_text_ch(t, d[i]);
  }
}

void pc_text_nl(pc_text *t)
{
  pc_text_str(t, "\r\n");
}

//----------------------------------------------------------------------------------------------------------------------------------
// Little-endian fields of the protocol bodies

static uint32_t g16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t g32(const uint8_t *p) { return g16(p) | (g16(p + 2) << 16); }

static void line(pc_text *t, const char *s)
{
  pc_text_str(t, s);
  pc_text_nl(t);
}

static void kv(pc_text *t, const char *k)
{
  pc_text_str(t, "  ");
  pc_text_str(t, k);
  pc_text_str(t, ": ");
}

static void date6(pc_text *t, const uint8_t *d)
{
  static const char sep[6] = { '-', '-', ' ', ':', ':', 0 };
  uint32_t i;

  pc_text_str(t, "20");

  for(i=0;i<6;i++)
  {
    pc_text_ch(t, (char)('0' + d[i] / 10 % 10));
    pc_text_ch(t, (char)('0' + d[i] % 10));

    if(sep[i])
      pc_text_ch(t, sep[i]);
  }
}

static void duration_min(pc_text *t, uint32_t minutes)
{
  pc_text_udec(t, minutes / 60);
  pc_text_str(t, " h ");
  pc_text_udec(t, minutes % 60);
  pc_text_str(t, " min");
}

static void version16(pc_text *t, const uint8_t *v)
{
  char s[17];

  memcpy(s, v, 16);
  s[16] = 0;
  pc_text_str(t, s[0] ? s : "?");
}

static const char *exc_name(uint32_t type)
{
  static const char *n[7] = { "jump to the image entry", "undefined instruction", "SWI", "prefetch abort", "data abort",
                              "reserved vector", "FIQ" };

  return (type < 7) ? n[type] : "unknown exception";
}

//Restart: v = counters 49..56, 58, 59 (restarts, type, exc LR, SPSR, exc SP, reset LR, reset SP, reset CPSR, FSR, FAR)
static void restart_words(pc_text *t, const uint32_t *v)
{
  uint32_t type = v[1];

  pc_text_str(t, exc_name(type));

  if(type == 0)
  {
    pc_text_str(t, ", LR at entry ");
    pc_text_hex(t, v[5], 8);
  }
  else
  {
    pc_text_str(t, " at PC ");
    pc_text_hex(t, v[2] - ((type == 4) ? 8 : ((type == 1) || (type == 3)) ? 4 : 0), 8);
    pc_text_str(t, " (LR ");
    pc_text_hex(t, v[2], 8);
    pc_text_str(t, ", SPSR ");
    pc_text_hex(t, v[3], 8);
    pc_text_str(t, ", SP ");
    pc_text_hex(t, v[4], 8);
    pc_text_str(t, ")");

    if((type == 3) || (type == 4))
    {
      pc_text_str(t, ", fault status ");
      pc_text_hex(t, v[8], 3);
    }
    if(type == 4)
    {
      pc_text_str(t, ", fault address ");
      pc_text_hex(t, v[9], 8);
    }
  }

  pc_text_str(t, "; at _reset LR ");
  pc_text_hex(t, v[5], 8);
  pc_text_str(t, " SP ");
  pc_text_hex(t, v[6], 8);
  pc_text_str(t, " CPSR ");
  pc_text_hex(t, v[7], 8);
}

//----------------------------------------------------------------------------------------------------------------------------------
// Sections

//Callers of events 21 and 22 (FPGA_WHO_*)
static const char *pc_who[5] = { "UI", "host", "calibration", "class probe", "full-rate check" };

static const char *pc_counter_names[PC_LINK_COUNTERS] =
{
  "rx_packets", "rx_bytes", "rx_stalls", "rx_swept", "tx_packets", "tx_zlps", "tx_kicked", "tx_flushes", "link_resets",
  "dtr_up", "dtr_down", "frames", "hdr_crc_errors", "payload_crc_errors", "oversize", "version_errors",
  "responses_dropped", "rx_timeouts", "junk_bytes", "responses", "duplicates", "tx_aborts", "disp_main", "disp_busy",
  "disp_isr", "held", "isr_skipped", "init_to_first_poll_ms", "first_pass_ms", "first_conversion_ms+1",
  "first_capture_ms+1", "mark_battery_ms+1", "mark_acquisition_ms+1", "mark_touch_ms+1", "mark_rtc_ms+1",
  "bus_resets", "suspends", "resumes", "disconnects", "ep0_setups", "ep0_bad_setups", "rx_isr_stalls", "rx_releases",
  "rx_restalls", "rx_empty_releases", "rxcount_max", "rxcount_over_512", "retired_47", "boot_splash_ms+1", "restarts",
  "restart_exc_type", "restart_exc_lr", "restart_exc_spsr", "restart_exc_sp", "restart_reset_lr", "restart_reset_sp",
  "restart_reset_cpsr", "retired_57", "restart_fault_status", "restart_fault_address", "usb_resumes_inferred",
  "usb_soft_reconnects", "adc_fullrate_bad", "adc_fullrate_recovered", "fpga_rate_writes", "fpga_filter_writes",
  "fpga_sampling_resets", "adc_fullrate_by_data", "ui_yields", "ui_yield_refused"
};

static void sec_identity(pc_text *t, pc_session *s, const uint8_t *sd, int sd_ok, const uint8_t *h)
{
  const pc_image_id *r = &pc_image_ident;
  uint32_t cp15 = g32(h + 16);

  line(t, "FNIRSI-1013D-AI diagnostics report");
  line(t, "==================================");
  pc_text_str(t, "Firmware running: ");
  pc_text_str(t, PC_FW_VERSION);
  pc_text_str(t, ", protocol ");
  pc_text_udec(t, PC_PROTO_MAJOR);
  pc_text_str(t, ".");
  pc_text_udec(t, PC_PROTO_MINOR);
  pc_text_str(t, ", git ");
  pc_text_hex(t, r->build, 8);
  pc_text_str(t, " of ");
  date6(t, r->time);
  line(t, " UTC");

  pc_text_str(t, "Card image (sector 750): ");

  if(!sd_ok)
    pc_text_str(t, "no card");
  else
  {
    switch(sd[64])
    {
      case PC_ID_RECORD:
      case PC_ID_TAIL:
        version16(t, sd + 68);
        pc_text_str(t, ", protocol ");
        pc_text_udec(t, sd[66]);
        pc_text_str(t, ".");
        pc_text_udec(t, sd[67]);
        pc_text_str(t, ", git ");
        pc_text_hex(t, g32(sd + 84), 8);
        pc_text_str(t, " of ");
        date6(t, sd + 88);
        pc_text_str(t, " UTC");
        pc_text_str(t, (sd[65] & 1) ? " (the running image)" : " (differs from the running image: a power cycle starts it)");
        break;
      case PC_ID_VERSION_ONLY:
        version16(t, sd + 68);
        pc_text_str(t, ", protocol ");
        pc_text_udec(t, sd[66]);
        pc_text_str(t, ".");
        pc_text_udec(t, sd[67]);
        pc_text_str(t, " (no build identity in this image; differs from the running image: a power cycle starts it)");
        break;
      case PC_ID_NO_IMAGE:
        pc_text_str(t, "no open-firmware image (or unreadable)");
        break;
      default:
        pc_text_str(t, "unknown (an image without a version text)");
        break;
    }
  }

  pc_text_nl(t);
  pc_text_str(t, "FPGA: version word ");
  pc_text_hex(t, s->var.version, 4);
  pc_text_str(t, ", variant ");
  pc_text_udec(t, s->var.variant);
  pc_text_str(t, ", ring ");
  pc_text_udec(t, s->var.ring_words);
  pc_text_str(t, " words, caps ");
  pc_text_hex(t, s->var.caps, 8);
  pc_text_str(t, "; firmware built for ");
  pc_text_hex(t, PC_FW_FPGA_VERSION, 4);

  if(s->var.mismatch)
    pc_text_str(t, ": MISMATCH, the acquisition does not run");

  pc_text_nl(t);

  //1.24: FPGA 0x153A capture stages (GET_HEALTH 104..123, cap_stage.h)
  if(h[106] & PC_CAPF_HAS)
  {
    char     c[48];
    uint32_t k;

    cs_text(c, (uint16_t)(h[104] | (h[105] << 8)));
    pc_text_str(t, "ADC capture stages (0x63): ");
    pc_text_str(t, c);
    pc_text_str(t, (h[106] & PC_CAPF_TRAINED) ? ", trained" : ((h[106] & PC_CAPF_OTHER) ? ", power-on (stored for another bitstream)" :
                                                                                     ", power-on (not trained)"));
    pc_text_str(t, "; last 0x36 counts");

    for(k=0;k<4;k++)
    {
      pc_text_str(t, " ");
      pc_text_udec(t, (uint32_t)(h[108 + 2 * k] | (h[109 + 2 * k] << 8)));
    }

    pc_text_str(t, (h[106] & PC_CAPF_READ) ? "" : " (none read yet)");
    pc_text_str(t, ", captures read ");
    pc_text_udec(t, (uint32_t)(h[116] | (h[117] << 8) | (h[118] << 16) | ((uint32_t)h[119] << 24)));
    pc_text_str(t, ", flagged ");
    pc_text_udec(t, (uint32_t)(h[120] | (h[121] << 8) | (h[122] << 16) | ((uint32_t)h[123] << 24)));
    pc_text_str(t, (h[106] & PC_CAPF_REPEAT) ? "; a core flagged 3 times within a minute: recalibrate" : "");
    pc_text_nl(t);
  }

  //1.19: the full-rate capture check (GET_HEALTH 88..103, enc_heal.h)
  {
    static const char *mode[3] = { "none", "record data (B against A neighbours)", "FPGA read 0x33" };

    pc_text_str(t, "Full-rate capture check: ");
    pc_text_str(t, mode[(h[88] < 3) ? h[88] : 0]);

    //fw 0.2.72: an unknown convention at boot moves the check to the record data (and leaves the core order unverified)
    if((h[88] == EH_MODE_FLAG) || (h[90] & PC_ENCF_CONV_BAD))
    {
      pc_text_str(t, ", 0x33 at boot ");
      pc_text_hex(t, h[91], 2);
      pc_text_str(t, (h[90] & PC_ENCF_CONV_BAD) ? " (UNKNOWN encode convention: core order unverified)" :
                                                   " (convention 1, B first)");
      pc_text_str(t, ", last ");

      if(h[90] & PC_ENCF_READ)
        pc_text_hex(t, h[89], 2);
      else
        pc_text_str(t, "none");
    }

    pc_text_str(t, "; ");
    pc_text_udec(t, g32(h + 100));
    pc_text_str(t, " checked, ");
    pc_text_udec(t, g32(h + 92));
    pc_text_str(t, " bad, ");
    pc_text_udec(t, g32(h + 96));
    pc_text_str(t, " recovered");
    pc_text_str(t, (h[90] & PC_ENCF_GARBLED) ? "; the last one was accepted GARBLED" : "");
    pc_text_nl(t);
  }
  pc_text_str(t, "USB serial: ");
  line(t, PC_SERIAL);
  pc_text_str(t, "CPU: CP15 control ");
  pc_text_hex(t, cp15, 8);
  pc_text_str(t, (cp15 & 1) ? ": MMU on" : ": MMU off");
  pc_text_str(t, (cp15 & 4) ? ", D-cache on" : ", D-cache off");
  pc_text_str(t, (cp15 & 0x1000) ? ", I-cache on" : ", I-cache off");
  pc_text_nl(t);
}

static void sec_life(pc_text *t, const uint8_t *h)
{
  static const char *ends[3] = { "unknown (no life record yet)", "settings saved at power-off",
                                 "without a power-off save (restart, power lost, viewer open, or the write failed)" };
  uint32_t up = g32(h + 28);
  int32_t  last = (int32_t)g32(h + 36);

  pc_text_nl(t);
  line(t, "Unit");
  kv(t, "uptime");
  duration_min(t, up / 60000);
  pc_text_str(t, " ");
  pc_text_udec(t, up / 1000 % 60);
  pc_text_str(t, " s (");
  pc_text_udec(t, up);
  line(t, " ms)");
  kv(t, "power-on");

  if(g32(h + 20))
  {
    pc_text_str(t, "#");
    pc_text_udec(t, g32(h + 20));
  }
  else
    pc_text_str(t, "unknown (no valid settings sector at boot)");

  pc_text_str(t, ", operating time ");
  duration_min(t, g32(h + 24));
  pc_text_nl(t);
  kv(t, "previous power-on ended");
  line(t, ends[(h[33] < 3) ? h[33] : 0]);
  kv(t, "settings sector 709");
  pc_text_str(t, (h[32] & PC_LIFEF_VALID) ? "valid at boot" : "invalid at boot: defaults loaded");
  pc_text_str(t, (h[32] & PC_LIFEF_BOOT_WRITE) ? ", boot record written" :
                 (h[32] & PC_LIFEF_BOOT_FAIL) ? ", boot record write FAILED" : ", no boot record written");
  pc_text_nl(t);
  kv(t, "settings changed since the last save");
  pc_text_str(t, (h[34] & PC_SETF_DIRTY) ? "yes (saved at power-off)" : "no");
  pc_text_str(t, ", in-session saves ");
  pc_text_udec(t, h[35]);

  if(h[35])
  {
    pc_text_str(t, ", last ");

    if(last)
    {
      pc_text_str(t, "FAILED (driver error ");
      pc_text_dec(t, last);
      pc_text_str(t, ")");
    }
    else
      pc_text_str(t, "ok");
  }

  pc_text_nl(t);
}

static void sec_usb(pc_text *t, const uint8_t *h)
{
  static const char *st[5] = { "off (mass storage or FEL)", "reconnecting (soft disconnect)",
                               "not configured (no PC, or enumerating)", "configured", "suspended (host asleep, idle, or cable pulled)" };
  uint8_t f = h[5];

  pc_text_nl(t);
  line(t, "USB");
  kv(t, "state");
  pc_text_str(t, (h[4] < 5) ? st[h[4]] : "?");
  pc_text_str(t, (f & PC_USBF_HIGH_SPEED) ? ", high speed" : ", full speed");
  pc_text_str(t, (f & PC_USBF_DTR) ? ", port open" : ", port closed");
  pc_text_nl(t);
  kv(t, "POWER");
  pc_text_hex(t, h[6], 2);
  pc_text_str(t, ", address ");
  pc_text_udec(t, h[7]);
  pc_text_str(t, ", last SET_CONFIGURATION ");

  if(g32(h + 12) == 0xFFFFFFFFu)
    pc_text_str(t, "never");
  else
  {
    pc_text_udec(t, g32(h + 12));
    pc_text_str(t, " ms ago");
  }

  if(f & PC_USBF_SUSPENDED)
  {
    pc_text_str(t, ", suspended for ");
    pc_text_udec(t, g32(h + 8));
    pc_text_str(t, " ms");
  }

  pc_text_nl(t);
}

static void sec_restarts(pc_text *t, const uint32_t *v, const uint8_t *h)
{
  uint32_t rv[10], i;

  pc_text_nl(t);
  line(t, "Restarts");
  kv(t, "this power-on");
  pc_text_udec(t, v[49]);

  if(v[49])
  {
    for(i=0;i<8;i++)
      rv[i] = v[49 + i];
    rv[8] = v[58];
    rv[9] = v[59];
    pc_text_str(t, ", last: ");
    restart_words(t, rv);
  }

  pc_text_nl(t);
  kv(t, "stored record");

  if(!(h[44] & 1))
    pc_text_str(t, "none");
  else
  {
    rv[0] = g16(h + 46);
    rv[1] = h[45];

    for(i=0;i<6;i++)
      rv[2 + i] = g32(h + 52 + 4 * i);
    rv[8] = g32(h + 76);
    rv[9] = g32(h + 80);
    pc_text_str(t, (h[44] & 2) ? "this power-on (#" : "an earlier power-on (#");
    pc_text_udec(t, g32(h + 48));
    pc_text_str(t, ", at ");
    duration_min(t, g32(h + 84));
    pc_text_str(t, " operating time), ");
    pc_text_udec(t, rv[0]);
    pc_text_str(t, " restart(s), last: ");
    restart_words(t, rv);
  }

  pc_text_nl(t);
}

static void sec_counters(pc_text *t, const uint32_t *v, uint32_t n)
{
  uint32_t i;

  pc_text_nl(t);
  pc_text_str(t, "Link counters (GET_LINK_STATS, n = ");
  pc_text_udec(t, n);
  line(t, ")");

  for(i=0;i<n;i++)
  {
    pc_text_str(t, "  ");
    pc_text_udec(t, i);
    pc_text_str(t, " ");
    pc_text_str(t, pc_counter_names[i]);
    pc_text_str(t, " = ");

    if((i >= 51) && (i <= 56))
      pc_text_hex(t, v[i], 8);
    else
      pc_text_udec(t, v[i]);

    pc_text_nl(t);
  }
}

static void sec_sd(pc_text *t, const uint8_t *sd, int sd_ok)
{
  static const char *magic[3] = { "no image", "eGON.BT0", "eGON.EXE" };
  static const char *check[4] = { "checksum not checked", "checksum ok", "checksum BAD", "length invalid" };
  uint32_t i;

  pc_text_nl(t);
  line(t, "SD card");

  if(!sd_ok)
  {
    line(t, "  no card");
    return;
  }

  kv(t, "sectors");
  pc_text_udec(t, g32(sd));
  pc_text_str(t, ", partition at ");
  pc_text_udec(t, g32(sd + 4));
  pc_text_str(t, ", write window 16..");
  pc_text_udec(t, g32(sd + 8));
  pc_text_nl(t);

  for(i=0;i<3;i++)
  {
    const uint8_t *e = sd + 16 + 16 * i;

    pc_text_str(t, "  sector ");
    pc_text_udec(t, g32(e));
    pc_text_str(t, ": ");
    pc_text_str(t, (e[4] < 3) ? magic[e[4]] : "?");

    if(e[4])
    {
      pc_text_str(t, ", length ");
      pc_text_udec(t, g32(e + 8));
      pc_text_str(t, ", ");
      pc_text_str(t, (e[5] < 4) ? check[e[5]] : "?");
    }

    pc_text_nl(t);
  }
}

static void q16(pc_text *t, uint32_t v)
{
  pc_text_fix(t, (int32_t)v, 65536, 5);
}

static void sec_cal_channel(pc_text *t, const uint8_t *c)
{
  static const char *cls[2] = { "full", "gated" };
  const uint8_t *b = c + 112, *pv = c + 292;
  uint8_t f = c[73];
  uint32_t m, k, r;

  pc_text_str(t, "  CH");
  pc_text_udec(t, c[0] + 1u);
  pc_text_str(t, ": ");

  if(c[72] == 3)
    pc_text_str(t, "calibration v3 in use");
  else if(f & 1)
    pc_text_str(t, "v3 block present but not bound to the PWM centres: 1.00o5 compensations in use (run Calibrate)");
  else
    pc_text_str(t, "no v3 calibration: 1.00o5 compensations in use");

  if(f & 1)
  {
    pc_text_str(t, (f & 8) ? ", gains measured" : ", gains NOT measured");
    if(f & 16)
      pc_text_str(t, " (carried over from an earlier calibration)");
  }

  pc_text_str(t, (f & 4) ? "; current clock class gated" : "; current clock class full rate");
  pc_text_nl(t);

  pc_text_str(t, "    correction now: core A ");
  pc_text_fix(t, (int32_t)(int16_t)g16(c + 74) * 16 + c[78], 16, 2);
  pc_text_str(t, " codes, core B ");
  pc_text_fix(t, (int32_t)(int16_t)g16(c + 76) * 16 + c[79], 16, 2);
  pc_text_str(t, " codes, 0 V at code ");
  pc_text_fix(t, (int32_t)g32(c + 80), 256, 2);
  pc_text_str(t, ", gain A ");
  q16(t, g32(c + 92));
  pc_text_str(t, ", gain B ");
  q16(t, g32(c + 96));
  pc_text_str(t, ", 16-bit gain ");
  q16(t, g32(c + 108));
  pc_text_nl(t);

  pc_text_str(t, "    volts factors (input_calibration): ");
  for(r=0;r<7;r++)
  {
    pc_text_udec(t, g32(c + 20 + 4 * r));
    pc_text_str(t, (r < 6) ? " " : "");
  }
  pc_text_str(t, "; 1.00o5 compensations A ");
  pc_text_dec(t, (int16_t)g16(c + 48));
  pc_text_str(t, " B ");
  pc_text_dec(t, (int16_t)g16(c + 50));
  pc_text_str(t, "; DC shift centre ");
  pc_text_udec(t, g32(c + 52));
  pc_text_str(t, " size ");
  pc_text_dec(t, (int32_t)g32(c + 56));
  pc_text_str(t, " value ");
  pc_text_dec(t, (int32_t)g32(c + 60));
  pc_text_nl(t);

  if(f & 1)
  {
    pc_text_str(t, "    PWM centres per range:");
    for(r=0;r<ACAL_RANGES;r++)
    {
      pc_text_str(t, " ");
      pc_text_udec(t, g16(b + 2 * r));
    }
    pc_text_nl(t);

    for(m=0;m<ACAL_CLASSES;m++)
      for(k=0;k<2;k++)
      {
        pc_text_str(t, "    ");
        pc_text_str(t, cls[m]);
        pc_text_str(t, k ? " core B" : " core A");
        pc_text_str(t, ": zero codes");
        for(r=0;r<ACAL_RANGES;r++)
        {
          pc_text_str(t, " ");
          pc_text_fix(t, (int16_t)g16(b + 12 + 2 * (12 * m + 6 * k + r)), 16, 2);
        }
        pc_text_str(t, "; slope codes/count");
        for(r=0;r<ACAL_RANGES;r++)
        {
          pc_text_str(t, " ");
          pc_text_fix(t, (int16_t)g16(b + 60 + 2 * (12 * m + 6 * k + r)), 65536, 4);
        }
        pc_text_str(t, "; noise RMS codes");
        for(r=0;r<ACAL_RANGES;r++)
        {
          pc_text_str(t, " ");
          pc_text_fix(t, b[108 + 12 * m + 6 * k + r], 16, 2);
        }
        pc_text_str(t, "; gain ");
        pc_text_fix(t, (int32_t)g16(b + 132 + 2 * (2 * m + k)), 32768, 5);
        pc_text_nl(t);
      }

    pc_text_str(t, "    gains measured on range ");
    if(b[140] == ACAL_GAIN_NONE)
      pc_text_str(t, "never");
    else
      pc_text_udec(t, b[140]);
    pc_text_str(t, ", post span ");
    pc_text_fix(t, (int32_t)g16(b + 142), 16, 2);
    pc_text_str(t, " codes");
    pc_text_nl(t);
  }

  pc_text_str(t, "    v4 tracking: A-B offset ");
  pc_text_fix(t, (int16_t)g16(c + 258), 256, 3);
  pc_text_str(t, " codes, slope ");
  pc_text_fix(t, (int32_t)g32(c + 260), 1u << 24, 6);
  pc_text_str(t, ", records ");
  pc_text_udec(t, g32(c + 272));
  pc_text_str(t, (c[256] & 0x10) ? " (applied now)" : " (not applied now)");
  pc_text_str(t, "; full-class common mode ");
  pc_text_fix(t, (int16_t)g16(c + 280), 256, 3);
  pc_text_str(t, " codes from ");
  pc_text_udec(t, g16(c + 282));
  pc_text_str(t, " probes");
  pc_text_nl(t);

  if(c[0] == 0)
  {
    pc_text_str(t, "  Provenance: ");

    if(c[288] == ACAL_PROV_VALID)
    {
      static const char *part[4] = { "CH1 zeros", "CH1 gains", "CH2 zeros", "CH2 gains" };
      uint32_t i, any = 0;

      pc_text_str(t, "calibrated by fw ");
      version16(t, pv + 12);
      pc_text_str(t, " (git ");
      pc_text_hex(t, g32(pv + 8), 8);
      pc_text_str(t, ") on FPGA ");
      pc_text_hex(t, g16(pv + 28), 4);
      pc_text_str(t, ", ");
      pc_text_udec(t, g32(pv + 32) / 60000);
      pc_text_str(t, " min after power-on");

      if(g32(pv + 36))
      {
        pc_text_str(t, " #");
        pc_text_udec(t, g32(pv + 36));
      }

      if(pv[31])
      {
        pc_text_str(t, ", ");
        date6(t, pv + 44);
      }

      pc_text_str(t, "; wrote");
      for(i=0;i<4;i++)
        if(pv[30] & (1u << i))
        {
          pc_text_str(t, any++ ? ", " : " ");
          pc_text_str(t, part[i]);
        }
      if(!any)
        pc_text_str(t, " nothing new");

      if(g32(c + 348) != 0xFFFFFFFFu)
      {
        pc_text_str(t, "; since then ");
        pc_text_udec(t, g32(c + 348));
        pc_text_str(t, " power-ons, ");
        duration_min(t, g32(c + 352));
        pc_text_str(t, " operating time");
      }
    }
    else
      pc_text_str(t, (c[288] == ACAL_PROV_BAD) ? "unreadable" : "none recorded (calibrated before fw 0.2.52, or never)");

    pc_text_nl(t);
  }
}

//One event log entry in words (u32 ms, u8 type, u8 a, u16 b; protocol row 42); also the Diagnostics Log page
void pc_report_event(pc_text *t, const uint8_t *e)
{
  static const char *cause[4] = { "SOFs again", "EP0 setup", "EP2 OUT packet", "frame number moved" };
  uint32_t type = e[4], a = e[5], b = g16(e + 6);

  switch(type)
  {
    case 1:
    case 4:
      pc_text_str(t, (type == 1) ? "bus reset" : "disconnect");
      if(a)
      {
        pc_text_str(t, " after a suspend of ");
        pc_text_udec(t, b);
        pc_text_str(t, " ms");
      }
      break;
    case 2:
      pc_text_str(t, "suspend (POWER ");
      pc_text_hex(t, a, 2);
      pc_text_str(t, ", frame ");
      pc_text_udec(t, b);
      pc_text_str(t, ")");
      break;
    case 3:
      if(a & 0x80)
      {
        pc_text_str(t, "resume inferred (");
        pc_text_str(t, cause[(a >> 5) & 3]);
        pc_text_str(t, ")");
      }
      else
        pc_text_str(t, "RESUME interrupt");
      pc_text_str(t, ", suspended ");
      pc_text_udec(t, b);
      pc_text_str(t, " ms");
      break;
    case 5:
      pc_text_str(t, "EP0 setup request ");
      pc_text_hex(t, a, 2);
      pc_text_str(t, " type ");
      pc_text_hex(t, b >> 8, 2);
      pc_text_str(t, " value ");
      pc_text_hex(t, b & 0xFF, 2);
      break;
    case 6:
      pc_text_str(t, "EP0 packet not 8 bytes (");
      pc_text_udec(t, b);
      pc_text_str(t, ")");
      break;
    case 7:
      pc_text_str(t, "SET_CONFIGURATION (enumerated)");
      break;
    case 8:
      pc_text_str(t, "CLEAR_FEATURE(HALT) endpoint ");
      pc_text_hex(t, a, 2);
      break;
    case 9:
      pc_text_str(t, a ? "port opened (DTR on)" : "port closed (DTR off)");
      break;
    case 10:
      pc_text_str(t, "RX ring full, host held back");
      break;
    case 11:
      pc_text_str(t, "RX released");
      break;
    case 12:
      pc_text_str(t, "TX frame flushed (link reset or port closed)");
      break;
    case 13:
      pc_text_str(t, "TX frame aborted (no progress, or #RESET)");
      break;
    case 15:
      pc_text_str(t, "restart: ");
      pc_text_str(t, exc_name(a));
      pc_text_str(t, ", low PC bits ");
      pc_text_hex(t, b, 4);
      break;
    case 16:
    case 19:
      pc_text_str(t, (type == 16) ? "USB init state, field " : "USB state after init, field ");
      pc_text_udec(t, a);
      pc_text_str(t, " = ");
      pc_text_hex(t, b, 4);
      break;
    case 17:
      pc_text_str(t, "after the TX abort: POWER ");
      pc_text_hex(t, a, 2);
      pc_text_str(t, ", EP2 TXCSR ");
      pc_text_hex(t, b, 4);
      break;
    case 18:
      pc_text_str(t, "after the TX abort: EP2 TX interrupt ");
      pc_text_str(t, (a & 1) ? "on" : "off");
      pc_text_str(t, (a & 2) ? ", ZLP pending" : "");
      pc_text_str(t, ", frame ");
      pc_text_udec(t, b);
      break;
    case 20:
      pc_text_str(t, ((a & 3) == 1) ? "soft reconnect (suspend storm), " : "soft reconnect (long suspend), ");
      pc_text_udec(t, a >> 4);
      pc_text_str(t, " suspends, suspended ");
      pc_text_udec(t, b);
      pc_text_str(t, " s");
      break;
    case 21:
    {
      //1.19: a = reason << 5 | re-arms, bit 7 not recovered; b = caller << 12 | value (enc_heal.h)
      static const char *why[3] = { "after a rate write", "data check", "FPGA 0x33" };
      uint32_t r = (a >> 5) & 3;

      if(r == EH_WHY_CONV)
      {
        pc_text_str(t, "FPGA encode convention unknown at boot, 0x33 = ");
        pc_text_hex(t, b & 0xFF, 2);
        break;
      }

      pc_text_str(t, "full-rate ADC capture garbled (");
      pc_text_str(t, why[r]);
      pc_text_str(t, (a & 0x80) ? "), not recovered after " : "), recovered after ");
      pc_text_udec(t, a & 0x1F);
      pc_text_str(t, " re-arms, ");

      if(r == EH_WHY_FLAG)
      {
        pc_text_str(t, "0x33 = ");
        pc_text_hex(t, b & 0xFF, 2);
      }
      else
      {
        pc_text_str(t, "residual ");
        pc_text_udec(t, ((uint32_t)(b & 0xFFF) + 8) / 16);
        pc_text_str(t, " codes");
      }

      pc_text_str(t, ", by ");
      pc_text_str(t, ((b >> 12) < 5) ? pc_who[b >> 12] : "?");
      break;
    }
    case 23:
    {
      char c[48];
      uint32_t k, n = 0;

      if(a & 0x10)
      {
        cs_text(c, b);
        pc_text_str(t, (a & 0x0F) ? "capture stage training failed, no clean stage for" : "capture stages trained: ");
        if(!(a & 0x0F))
          pc_text_str(t, c);
      }
      else
      {
        pc_text_str(t, "capture outliers on");
      }

      for(k=0;k<4;k++)
        if(a & (1u << k))
        {
          cs_core_name(c, k);
          pc_text_str(t, n++ ? ", " : " ");
          pc_text_str(t, c);
        }

      if(!(a & 0x10))
      {
        pc_text_str(t, ", ");
        pc_text_udec(t, b);
        pc_text_str(t, " beyond the chip partner");
      }
      break;
    }
    case 22:
    {
      static const char *const *who = pc_who;

      pc_text_str(t, "FPGA write ");
      pc_text_hex(t, a, 2);
      pc_text_str(t, " = ");
      pc_text_udec(t, b & 0xFFF);
      pc_text_str(t, ((b & 0xFFF) == 0xFFF) ? "+ by " : " by ");
      pc_text_str(t, ((b >> 12) < 5) ? who[b >> 12] : "?");
      break;
    }
    default:
      pc_text_str(t, "event type ");
      pc_text_udec(t, type);
      pc_text_str(t, " a ");
      pc_text_hex(t, a, 2);
      pc_text_str(t, " b ");
      pc_text_hex(t, b, 4);
      break;
  }
}

static void ev_words(pc_text *t, const uint8_t *e)
{
  pc_text_str(t, "  ");
  pc_text_udec(t, g32(e));
  pc_text_str(t, " ms  ");
  pc_report_event(t, e);
  pc_text_nl(t);
}

//----------------------------------------------------------------------------------------------------------------------------------

uint32_t pc_report_build(pc_session *s, uint8_t *ws, uint32_t size)
{
  uint8_t  sd[PC_SD_INFO_LEN], h[PC_HEALTH_LEN], cal[PC_CAL_RESP_LEN];
  uint32_t v[PC_LINK_COUNTERS], n, total, k, bus_total, i, ch;
  uint8_t  battery, charging, raw;
  uint8_t *ev = ws + size - 1280;             //event entries: 128 + 16 of 8 bytes, at the workspace's end
  int      sd_ok;
  pc_text  t;

  //The card first: its reads use the workspace as scratch
  sd_ok = (pc_sd_info_body(s, 0, 1, sd, ws, PC_REPORT_SCRATCH) == 0);

  pc_health_body(h);
  n = pc_link_counters(s, v);
  pc_text_init(&t, (char *)ws, size - 1280);

  sec_identity(&t, s, sd, sd_ok, h);
  sec_life(&t, h);
  pc_hal_battery(&battery, &charging, &raw);
  kv(&t, "battery");
  pc_text_str(&t, "level ");
  pc_text_udec(&t, battery);
  pc_text_str(&t, " of 20");
  pc_text_str(&t, charging ? ", charging" : "");
  pc_text_nl(&t);
  sec_usb(&t, h);
  sec_restarts(&t, v, h);

  pc_text_nl(&t);
  line(&t, "Calibration");
  for(ch=0;ch<2;ch++)
  {
    pc_cal_body((int)ch, cal);
    sec_cal_channel(&t, cal);
  }

  sec_sd(&t, sd, sd_ok);
  sec_counters(&t, v, n);

  k = pc_hal_event_log(ev, 128, &total);
  pc_text_nl(&t);
  pc_text_str(&t, "Event log (last ");
  pc_text_udec(&t, k);
  pc_text_str(&t, " of ");
  pc_text_udec(&t, total);
  pc_text_str(&t, " events, now ");
  pc_text_udec(&t, pc_hal_ms());
  line(&t, " ms)");
  for(i=0;i<k;i++)
    ev_words(&t, ev + 8 * i);

  k = pc_hal_event_bus(ev, &bus_total);
  pc_text_nl(&t);
  pc_text_str(&t, "Bus-level events (last ");
  pc_text_udec(&t, k);
  pc_text_str(&t, " of ");
  pc_text_udec(&t, bus_total);
  line(&t, ")");
  for(i=0;i<k;i++)
    ev_words(&t, ev + 8 * i);

  if(t.full)
  {
    t.len = (t.len > 32) ? t.len - 32 : 0;
    t.buf[t.len] = 0;
    t.full = 0;
    pc_text_nl(&t);
    line(&t, "[report truncated]");
  }

  return t.len;
}

int pc_save_report(pc_session *s, uint8_t *ws, uint32_t size, uint32_t *written, char *name, uint32_t name_max)
{
  uint8_t  h[PC_HEALTH_LEN];
  uint32_t len = pc_report_build(s, ws, size);
  int      r;

  pc_health_body(h);
  r = pc_hal_report_write((const char *)ws, len, g32(h + 20), name, name_max);
  *written = r ? 0 : len;
  return r;
}
