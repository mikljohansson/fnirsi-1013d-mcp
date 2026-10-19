//----------------------------------------------------------------------------------------------------------------------------------
// enc_heal.h - full-rate capture check after a rate change, with retry (fw 0.2.61, fpga/al3/README.md "Findings"
// item 3, firmware/AGENTS.md section 37). Pure C, no globals, host-buildable (host_tests/test_enc_heal.c); the target
// glue that captures, waits and writes 0x0D is enc_heal_hw.c.
//
// Bench 2026-10-19. AL3 0x1532..0x1536: a 0x0D (or 0x53) write can leave one ADC chip's encode clocks half a word out of
// phase with the RAM write clock until the next such accident; at 200 MSa/s one core of that chip is then captured
// while its outputs change (CH1 core B 15-29 codes RMS in 27 of 60 rate changes, 1.1 otherwise). 0x1537: the phase is
// deterministic, but right after a 0x0D write to divider 0 CH2 core A reads 32-34 codes in 35 of 40 trials and
// recovers later (static captures clean on all cores). Gated rates are not affected on either.
//
// The check takes one full-rate capture and measures, per channel, how far core B's samples lie from the mean of their
// two core A neighbours (both neighbour pairs: the order of the cores is not known on these bitstreams), RMS about the
// mean residual. A signal well under 100 MHz cancels, the cores' offset difference cancels with the mean, the ADC noise
// leaves about 1.3 codes; a garbage core leaves tens. Above EH_LIMIT16 the retry re-issues the rate write (0x0D = 1,
// then 0: each write re-rolls the state, waiting does not help: 0x1537 bench 16:40, a bad state persists until the next
// write), optionally waits EH_WAIT_MS, and checks again with a new conversion, at most EH_MAX_TRIES times. The limit,
// the try count and the wait are the tuning constants, here only. Every bitstream is checked (no version skip).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef ENC_HEAL_H
#define ENC_HEAL_H

#include <stdint.h>

#define EH_LIMIT16      (8 * 16)    //residual RMS per channel, 1/16 code (bench: 1.3 good, 15-29 codes bad)
#define EH_MAX_TRIES    16          //retries per check
#define EH_WAIT_MS      0           //wait after each re-write, ms (0x1537: no settling seen, 0..300 ms)
#define EH_SAMPLES      500         //per core in the check capture

//1 for the bitstreams with the documented random race: AL3 0x1532..0x1536 (Diagnostics' amber FPGA line)
int      eh_affected(uint16_t fpga_version);

//Residual RMS (1/16 code) of one channel's capture in the trace layout: buf[2i] core B, buf[2i + 1] core A, n pairs.
//The smaller of the two neighbour choices (B against A[i-1], A[i] or against A[i], A[i+1])
uint32_t eh_residual16(const uint8_t *buf, uint32_t n);

//The steps the heal needs from the target (or a host mock)
typedef struct
{
  void    *ctx;
  void   (*retry)(void *ctx);                       //before a new check: 0x0D = 1, then 0 (then EH_WAIT_MS)
  void   (*capture)(void *ctx, uint32_t res16[2]);  //one full-rate capture, eh_residual16 per channel
} eh_ops;

typedef struct
{
  uint32_t res16[2];      //residuals of the last check
  uint32_t first16;       //the larger residual of the first check
  uint8_t  bad;           //the first check found a channel above the limit
  uint8_t  tries;         //retries made
  uint8_t  ok;            //the last check passed (healed when bad)
} eh_result;

//Check, and heal when a channel is above the limit; returns r->ok
int      eh_run(const eh_ops *o, eh_result *r);

//Target (enc_heal_hw.c): the check and retry at the current settings when the divider is 0 (1 otherwise, nothing done;
//every bitstream). Front end, offsets and the UI's trace buffers untouched; sample counts and trigger mode restored. A
//bad first check is counted (GET_LINK_STATS 62), a recovery too (63), and logged (event type 21)
int      fpga_encode_heal(void);

//The same, only when 0x0D was written since the last check (fpga_rate_dirty): the UI acquisition before its conversion
//(scope_functions.c) and the host ARM (pc_hal_arm) call it; returns 1 when nothing was to check or the check passed
int      fpga_encode_check_dirty(void);

#endif /* ENC_HEAL_H */
