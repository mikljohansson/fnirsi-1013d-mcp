//----------------------------------------------------------------------------------------------------------------------------------
// pc_life.h - unit life record (fw 0.2.52, protocol 1.16, docs/protocol.md section 14 row 55): power-on count, operating
// minutes, how the previous power-on ended, and the restart record that survives a power cycle. Stored in settings sector
// 709 words 224..255 (inside the 1.00o5 checksum of words 2..255; older firmware keeps words it does not use).
//
// Pure C, no globals, no hardware: the target glue (pc_diag.c) owns the state; host_tests/test_life.c tests it.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_LIFE_H
#define PC_LIFE_H

#include <stdint.h>

#define PC_LIFE_WORD0        224          //first settings word of the record
#define PC_LIFE_WORDS        32           //words 224..255
#define PC_LIFE_MARKER       0x11FE
#define PC_LIFE_F_OPEN       0x0001       //word 225: session open (boot write), cleared by the power-off save
#define PC_LIFE_DIRTY_WORDS  PC_LIFE_WORD0  //settings words 2..223 decide "changed since the last save"

//How the previous power-on ended (GET_HEALTH byte 33)
#define PC_LIFE_END_UNKNOWN  0
#define PC_LIFE_END_SAVED    1            //settings saved at power-off
#define PC_LIFE_END_UNSAVED  2            //warm restart, power lost, viewer open at power-off, or the write failed

//Restart record, as GET_LINK_STATS 49-59 at the boot after the restart
typedef struct
{
  uint8_t  valid;
  uint8_t  type;                          //exception type (counter 50)
  uint16_t restarts;                      //counter 49, capped 65535
  uint32_t power_on;                      //power-on it happened in
  uint32_t exc_lr, spsr, exc_sp;          //counters 51, 52, 53
  uint32_t reset_lr, reset_sp, reset_cpsr;//counters 54, 55, 56
  uint32_t fsr, far;                      //counters 58, 59
  uint32_t minutes;                       //operating minutes when stored
} pc_life_restart;

typedef struct
{
  uint16_t flags;                         //PC_LIFE_F_*
  uint32_t power_on;                      //0 = unknown
  uint32_t minutes;                       //operating minutes up to the last write
  pc_life_restart rst;
} pc_life;

//Words 224..255 <-> record. unpack returns 1 when the marker is present (else l is zeroed, 0)
void pc_life_pack(const pc_life *l, uint16_t *w);
int  pc_life_unpack(const uint16_t *w, pc_life *l);

//Restart record from the 10 counters 49..56, 58, 59 (in that order: restarts, type, exc LR, SPSR, exc SP, reset LR,
//reset SP, reset CPSR, fault status, fault address)
void pc_life_set_restart(pc_life *l, const uint32_t *v, uint32_t minutes);

//Boot: from the record found on the card (found = unpack's result), whether this boot is a warm restart (restarts > 0,
//v = its counters as above) and the settings sector's validity. Updates l for this power-on (power_on + 1 unless a
//restart, restart record stored, session open) and returns PC_LIFE_END_* for the previous power-on.
int  pc_life_boot(pc_life *l, int found, int sector_valid, const uint32_t *restart_v);

//1.00o5 settings checksum (sum of words 2..255) into words 0-1; 1 when words 2-3 hold the expected version and 0-1 match
void     pc_life_settings_seal(uint16_t *s);
int      pc_life_settings_valid(const uint16_t *s, uint16_t ver_hi, uint16_t ver_lo);

#endif /* PC_LIFE_H */
