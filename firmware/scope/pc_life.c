//----------------------------------------------------------------------------------------------------------------------------------
// pc_life.c - unit life record in settings words 224..255 (pc_life.h, protocol row 55)
//----------------------------------------------------------------------------------------------------------------------------------

#include "pc_life.h"

//Word offsets inside the record (word 224 = 0)
#define LW_MARKER    0
#define LW_FLAGS     1
#define LW_POWER_ON  2
#define LW_MINUTES   4
#define LW_RFLAGS    6
#define LW_RESTARTS  7
#define LW_TYPE      8
#define LW_RPOWER    10
#define LW_EXC_LR    12
#define LW_SPSR      14
#define LW_EXC_SP    16
#define LW_RST_LR    18
#define LW_RST_SP    20
#define LW_RST_CPSR  22
#define LW_FSR       24
#define LW_FAR       26
#define LW_RMINUTES  28

static void put32(uint16_t *w, uint32_t v)
{
  w[0] = (uint16_t)v;
  w[1] = (uint16_t)(v >> 16);
}

static uint32_t get32(const uint16_t *w)
{
  return (uint32_t)w[0] | ((uint32_t)w[1] << 16);
}

void pc_life_pack(const pc_life *l, uint16_t *w)
{
  const pc_life_restart *r = &l->rst;
  uint32_t i;

  for(i=0;i<PC_LIFE_WORDS;i++)
    w[i] = 0;

  w[LW_MARKER] = PC_LIFE_MARKER;
  w[LW_FLAGS]  = l->flags;
  put32(w + LW_POWER_ON, l->power_on);
  put32(w + LW_MINUTES, l->minutes);

  if(r->valid)
  {
    w[LW_RFLAGS]   = 1;
    w[LW_RESTARTS] = r->restarts;
    w[LW_TYPE]     = r->type;
    put32(w + LW_RPOWER,   r->power_on);
    put32(w + LW_EXC_LR,   r->exc_lr);
    put32(w + LW_SPSR,     r->spsr);
    put32(w + LW_EXC_SP,   r->exc_sp);
    put32(w + LW_RST_LR,   r->reset_lr);
    put32(w + LW_RST_SP,   r->reset_sp);
    put32(w + LW_RST_CPSR, r->reset_cpsr);
    put32(w + LW_FSR,      r->fsr);
    put32(w + LW_FAR,      r->far);
    put32(w + LW_RMINUTES, r->minutes);
  }
}

int pc_life_unpack(const uint16_t *w, pc_life *l)
{
  pc_life_restart *r = &l->rst;

  *l = (pc_life){ 0 };

  if(w[LW_MARKER] != PC_LIFE_MARKER)
    return 0;

  l->flags    = w[LW_FLAGS];
  l->power_on = get32(w + LW_POWER_ON);
  l->minutes  = get32(w + LW_MINUTES);

  if(w[LW_RFLAGS] & 1)
  {
    r->valid      = 1;
    r->restarts   = w[LW_RESTARTS];
    r->type       = (uint8_t)w[LW_TYPE];
    r->power_on   = get32(w + LW_RPOWER);
    r->exc_lr     = get32(w + LW_EXC_LR);
    r->spsr       = get32(w + LW_SPSR);
    r->exc_sp     = get32(w + LW_EXC_SP);
    r->reset_lr   = get32(w + LW_RST_LR);
    r->reset_sp   = get32(w + LW_RST_SP);
    r->reset_cpsr = get32(w + LW_RST_CPSR);
    r->fsr        = get32(w + LW_FSR);
    r->far        = get32(w + LW_FAR);
    r->minutes    = get32(w + LW_RMINUTES);
  }

  return 1;
}

void pc_life_set_restart(pc_life *l, const uint32_t *v, uint32_t minutes)
{
  pc_life_restart *r = &l->rst;

  r->valid      = 1;
  r->restarts   = (uint16_t)((v[0] > 0xFFFF) ? 0xFFFF : v[0]);
  r->type       = (uint8_t)v[1];
  r->power_on   = l->power_on;
  r->exc_lr     = v[2];
  r->spsr       = v[3];
  r->exc_sp     = v[4];
  r->reset_lr   = v[5];
  r->reset_sp   = v[6];
  r->reset_cpsr = v[7];
  r->fsr        = v[8];
  r->far        = v[9];
  r->minutes    = minutes;
}

int pc_life_boot(pc_life *l, int found, int sector_valid, const uint32_t *restart_v)
{
  int end = PC_LIFE_END_UNKNOWN;

  if(found)
    end = (l->flags & PC_LIFE_F_OPEN) ? PC_LIFE_END_UNSAVED : PC_LIFE_END_SAVED;
  else
    *l = (pc_life){ 0 };

  //A warm restart is the same power-on: the count does not move. Without a valid settings sector the count is unknown.
  if(!sector_valid)
    l->power_on = 0;
  else if(!restart_v)
    l->power_on++;

  if(restart_v)
    pc_life_set_restart(l, restart_v, l->minutes);

  l->flags |= PC_LIFE_F_OPEN;
  return end;
}

void pc_life_settings_seal(uint16_t *s)
{
  uint32_t sum = 0, i;

  for(i=2;i<256;i++)
    sum += s[i];

  s[0] = (uint16_t)(sum >> 16);
  s[1] = (uint16_t)sum;
}

int pc_life_settings_valid(const uint16_t *s, uint16_t ver_hi, uint16_t ver_lo)
{
  uint32_t sum = 0, i;

  for(i=2;i<256;i++)
    sum += s[i];

  return (s[0] == (uint16_t)(sum >> 16)) && (s[1] == (uint16_t)sum) && (s[2] == ver_hi) && (s[3] == ver_lo);
}
