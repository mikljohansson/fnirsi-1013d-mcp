//----------------------------------------------------------------------------------------------------------------------------------
// pc_diag.c - target side of the diagnostics readers (fw 0.2.52, protocol 1.16, docs/protocol.md section 14 row 55):
//  - unit life record in settings words 224..255 (pc_life.h): power-on count, operating minutes, how the previous
//    power-on ended, the restart record kept over power cycles; the boot write of sector 709
//  - settings save state: changed since the last save (CRC of words 2..223 as the save would write them), in-session
//    save results
//  - calibration provenance trailer in sector 708 (adc_cal.h acal_prov), stamped when a save carries a new block
//  - CP15 control word, the report file (FatFs) and the firmware-side entry points for the Diagnostics pages
// Hooks (pc_hooks.h): PC_SETTINGS_STORE / PC_SETTINGS_SAVED (scope_functions.c settings save), PC_CAL_LOADED /
// PC_CAL_SAVE (sector 708 load / save), PC_POWER_OFF (power_and_battery.c).
//----------------------------------------------------------------------------------------------------------------------------------

#include <string.h>
#include "types.h"
#include "variables.h"
#include "timer.h"
#include "sd_card_interface.h"
#include "touchpanel.h"
#include "DS3231.h"
#include "scope_functions.h"
#include "ff.h"
#include "pc_hal.h"
#include "pc_cmds.h"
#include "pc_hooks.h"
#include "pc_life.h"
#include "pc_ident.h"
#include "pc_report.h"
#include "adc_cal.h"

//pc_hal_scope.c
pc_session *pc_glue_session(void);
int         pc_glue_rtc_present(void);

static pc_life  pc_life_rec;              //this power-on's record; minutes = the stored base at boot
static uint8    pc_life_flags;            //PC_LIFEF_*
static uint8    pc_life_end;              //PC_LIFE_END_*
static uint8    pc_set_flags;             //PC_SETF_*
static uint8    pc_set_saves;
static int32    pc_set_last = 1;          //1 = no save since boot
static uint32   pc_set_base_crc;          //words 2..223 as last loaded / saved
static uint8    pc_set_base_ok;
static uint32   pc_set_eval_ms;           //time of the last dirty evaluation
static uint8    pc_set_evaluated;

//----------------------------------------------------------------------------------------------------------------------------------
// Unit life

static uint32 pc_life_minutes(void)
{
  return pc_life_rec.minutes + timer0ticks / 60000;
}

//pc_glue_init, after pc_restart_check: restart_v = counters 49..56, 58, 59 after a warm restart, else NULL
void pc_diag_boot(const uint32_t *restart_v)
{
  uint16 *w = settingsworkbuffer;
  int     valid = pc_life_settings_valid(w, SETTING_SECTOR_VERSION_HIGH, SETTING_SECTOR_VERSION_LOW);
  int     found = valid && pc_life_unpack(w + PC_LIFE_WORD0, &pc_life_rec);

  pc_life_end   = (uint8)pc_life_boot(&pc_life_rec, found, valid, restart_v);
  pc_life_flags = (uint8)((found ? PC_LIFEF_FOUND : 0) | (valid ? PC_LIFEF_VALID : 0));

  //One write of sector 709 per boot: the count, the open session and a restart record reach the card now, so a crash
  //followed by a power cycle (or a flat battery) is still visible. The other words exactly as loaded.
  if(valid)
  {
    pc_life_pack(&pc_life_rec, w + PC_LIFE_WORD0);
    pc_life_settings_seal(w);
    pc_life_flags |= (pc_hal_sd_write(SETTINGS_SECTOR, 1, (const uint8_t *)w) == 0) ? PC_LIFEF_BOOT_WRITE : PC_LIFEF_BOOT_FAIL;
  }
}

//PC_SETTINGS_STORE: scope_save_config_data, before the checksum
void pc_glue_settings_store(uint16 *w)
{
  pc_life rec = pc_life_rec;

  rec.minutes = pc_life_minutes();
  pc_life_pack(&rec, w + PC_LIFE_WORD0);
}

//PC_POWER_OFF: the power-off save that follows closes the session (no save in a viewer: the flag stays set)
void pc_glue_power_off(void)
{
  pc_life_rec.flags &= (uint16)~PC_LIFE_F_OPEN;
}

void pc_hal_life_clear_restart(void)
{
  pc_life_rec.rst.valid = 0;
}

static uint32 pc_set_crc(void)
{
  return acal_crc32((const uint8_t *)(settingsworkbuffer + 2), (PC_LIFE_DIRTY_WORDS - 2) * 2);
}

//PC_SETTINGS_SAVED: scope_save_configuration_data after its write (power-off saves end in the ISR's while(1))
void pc_glue_settings_saved(int32 rc)
{
  if(pc_set_saves < 255)
    pc_set_saves++;

  pc_set_last = rc;

  if(rc == 0)
  {
    pc_set_base_crc = pc_set_crc();
    pc_set_base_ok  = 1;
    pc_set_flags   &= (uint8)~(PC_SETF_DIRTY | PC_SETF_SAVE_FAILED);
  }
  else
    pc_set_flags |= PC_SETF_SAVE_FAILED;
}

//Main loop: rebuild the buffer as the power-off save would (it does the same first) and compare. The first evaluation
//sets the baseline, after the boot corrections of pc_ui_fix_settings.
void pc_diag_settings_check(void)
{
  uint32 crc;

  scope_save_config_data();
  crc = pc_set_crc();

  if(!pc_set_base_ok)
  {
    pc_set_base_crc = crc;
    pc_set_base_ok  = 1;
  }

  if(crc != pc_set_base_crc)
    pc_set_flags |= PC_SETF_DIRTY;
  else
    pc_set_flags &= (uint8)~PC_SETF_DIRTY;

  pc_set_eval_ms   = timer0ticks;
  pc_set_evaluated = 1;
}

//Every main-loop pass: once per second
void pc_diag_service(void)
{
  if(!pc_set_evaluated || (timer0ticks - pc_set_eval_ms >= 1000))
    pc_diag_settings_check();
}

void pc_hal_life(pc_hw_life *l)
{
  l->power_on       = pc_life_rec.power_on;
  l->minutes        = pc_life_minutes();
  l->life_flags     = pc_life_flags;
  l->last_end       = pc_life_end;
  l->settings_flags = pc_set_flags;
  l->saves          = pc_set_saves;
  l->last_save      = pc_set_last;
  l->dirty_age_ms   = pc_set_evaluated ? (timer0ticks - pc_set_eval_ms) : 0xFFFFFFFFu;
  l->rst            = pc_life_rec.rst;
}

//----------------------------------------------------------------------------------------------------------------------------------
// CP15

uint32_t pc_hal_cp15_control(void)
{
  uint32 c;

  __asm__ __volatile__ ("mrc p15, 0, %0, c1, c0, 0" : "=r" (c));
  return c;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Calibration provenance (sector 708 bytes 432..487)

static acal_channel pc_prov_base[2];       //the block as loaded or last saved
static uint8        pc_prov_base_ok;
static acal_prov    pc_prov;
static uint8        pc_prov_state;        //ACAL_PROV_*
static uint8        pc_prov_raw[ACAL_PROV_BYTES];

//PC_CAL_LOADED: end of scope_load_input_calibration_data (buffer = the sector as read)
void pc_glue_cal_loaded(const uint8 *sector)
{
  pc_prov_base_ok = adc_cal_loaded;

  if(adc_cal_loaded)
  {
    memcpy(pc_prov_base, adc_cal, sizeof(pc_prov_base));
    pc_prov_state = (uint8)acal_prov_unpack(sector + ACAL_PROV_OFFSET, &pc_prov);
    memcpy(pc_prov_raw, sector + ACAL_PROV_OFFSET, ACAL_PROV_BYTES);
  }
  else
  {
    pc_prov_state = ACAL_PROV_NONE;
    memset(pc_prov_raw, 0, sizeof(pc_prov_raw));
  }
}

static uint8 pc_bcd(uint8 v)
{
  return (uint8)((v >> 4) * 10 + (v & 15));
}

//PC_CAL_SAVE: scope_save_input_calibration_data, after the v3 block was packed into the cleared buffer
void pc_glue_cal_save(uint8 *sector)
{
  static const acal_channel none[2];
  uint8 what, t[7];

  if(!adc_cal_loaded)
    return;

  what = acal_prov_what(pc_prov_base_ok ? pc_prov_base : none, adc_cal);

  //A new calibration: stamp it
  if(what)
  {
    memset(&pc_prov, 0, sizeof(pc_prov));
    pc_prov.build = pc_image_ident.build;
    memcpy(pc_prov.version, pc_image_ident.version, 16);
    pc_prov.version[15] = 0;
    pc_prov.fpga      = pc_hal_fpga_version();
    pc_prov.what      = what;
    pc_prov.uptime_ms = timer0ticks;
    pc_prov.power_on  = pc_life_rec.power_on;
    pc_prov.minutes   = pc_life_rec.power_on ? pc_life_minutes() : 0;

    if(pc_glue_rtc_present())
    {
      tp_i2c_read_data(RTC_DEVICE_ADDR, 0x00, t, 7);
      pc_prov.rtc     = 1;
      pc_prov.time[0] = pc_bcd(t[6]);
      pc_prov.time[1] = pc_bcd(t[5] & 0x1F);
      pc_prov.time[2] = pc_bcd(t[4] & 0x3F);
      pc_prov.time[3] = pc_bcd(t[2] & 0x3F);
      pc_prov.time[4] = pc_bcd(t[1] & 0x7F);
      pc_prov.time[5] = pc_bcd(t[0] & 0x7F);
    }

    acal_prov_pack(&pc_prov, pc_prov_raw);
    pc_prov_state = ACAL_PROV_VALID;
  }

  if(pc_prov_state == ACAL_PROV_VALID)
    memcpy(sector + ACAL_PROV_OFFSET, pc_prov_raw, ACAL_PROV_BYTES);

  memcpy(pc_prov_base, adc_cal, sizeof(pc_prov_base));
  pc_prov_base_ok = 1;
}

int pc_hal_cal_prov(uint8_t *raw)
{
  //No block in use (RESET cleared it): no provenance either
  if(!adc_cal_loaded)
    pc_prov_state = ACAL_PROV_NONE;

  if(pc_prov_state == ACAL_PROV_NONE)
    memset(raw, 0, ACAL_PROV_BYTES);
  else
    memcpy(raw, pc_prov_raw, ACAL_PROV_BYTES);

  return pc_prov_state;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Report file and the firmware entry points for the Diagnostics pages

int pc_hal_report_write(const char *text, uint32_t len, uint32_t power_on, char *name, uint32_t name_max)
{
  static FIL fp;
  static const char base[] = "reports/report-00000-00.txt";
  FRESULT r;
  UINT    bw;
  uint32  i, n, p = power_on;

  if(name_max < sizeof(base))
    return FR_INVALID_PARAMETER;

  r = f_mkdir("reports");

  if((r != FR_OK) && (r != FR_EXIST))
    return r;

  memcpy(name, base, sizeof(base));

  for(i=0;i<5;i++, p/=10)
    name[19 - i] = (char)('0' + p % 10);

  for(n=0;n<100;n++)
  {
    name[21] = (char)('0' + n / 10);
    name[22] = (char)('0' + n % 10);
    r = f_open(&fp, name, FA_CREATE_NEW | FA_WRITE);

    if(r != FR_EXIST)
      break;
  }

  if(r != FR_OK)
    return r;

  r = f_write(&fp, text, len, &bw);

  if((r == FR_OK) && (bw != len))
    r = FR_DENIED;

  if(f_close(&fp) != FR_OK && (r == FR_OK))
    r = FR_DISK_ERR;

  return r;
}

//"Save report" (Diagnostics button): 0 or the FatFs FRESULT; name gets the file name
int pc_glue_save_report(char *name, uint32 name_max)
{
  static uint8 ws[65536] __attribute__((aligned(4)));
  uint32_t written;

  if(pc_hal_sd_sectors() == 0)
    return FR_NOT_READY;

  return pc_save_report(pc_glue_session(), ws, sizeof(ws), &written, name, name_max);
}

//Firmware tile ("Running X from RAM, card boots Y"): identify the card image (first call reads the card, MAIN context)
const pc_card_id *pc_glue_card_ident(void)
{
  static uint8 scratch[32768] __attribute__((aligned(4)));

  return pc_ident_card(1, scratch, sizeof(scratch));
}

//Status page snapshot: the GET_HEALTH body (96 bytes, protocol row 55) and the running image's identity
void pc_glue_health(uint8 *body96)
{
  pc_health_body(body96);
}
