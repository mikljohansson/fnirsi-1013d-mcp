//----------------------------------------------------------------------------------------------------------------------------------
// settings_check.h - the stored settings record (sector 709) checked before it is applied (fw-settings0).
//
// The record is 256 little-endian words (variables.h *_SETTING_OFFSET): words 0-1 the 1.00o5 checksum (sum of words
// 2..255), words 2-3 the version (SETTING_SECTOR_VERSION_HIGH / _LOW), then the fields. A record is applied only when
//   - the sector was read (the SD read gives the whole 512 bytes or fails: there is no shorter record),
//   - the version and the checksum match, and
//   - every field the firmware applies as an index, an enum, a switch or a count lies in the range the UI and the SET_*
//     commands keep it in (srec_check_fields; the table in settings_check.c names each one).
// Anything else loads the defaults (scope_restore_config_data). A field that the load already clamps or migrates (trace
// positions, the trigger point, Trace style 3, a memory depth over the bitstream's, the marked words 202..223) is not a
// reason to reject: older records with those values still load as before.
//
// Why the whole record and not field by field: a checksum-valid record with a field out of range was written by a
// writer that did not hold the settings (bench 2026-11-04: a save from the power-off interrupt while the boot was still
// loading them, firmware/AGENTS.md section 94). Its in-range fields are not trustworthy either (they mix the user's values
// with compile-time defaults), and a single flipped bit fails the checksum. Rejecting it whole gives a known state.
//
// Pure C, no globals, no hardware: builds for the scope and on the host (host_tests/test_settings.c).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef SETTINGS_CHECK_H
#define SETTINGS_CHECK_H

#include <stdint.h>

#define SREC_WORDS        256

//Results (scope_settings_result, GET_HEALTH byte 32 bit 3 is set only for SREC_OK)
#define SREC_OK           0     //applied
#define SREC_NO_READ      1     //the SD read failed
#define SREC_BAD_VERSION  2     //words 2-3 are not this firmware's version (another firmware's or an empty sector)
#define SREC_BAD_SUM      3     //the checksum does not match (torn or damaged sector)
#define SREC_BAD_FIELD    4     //checksum fine, a field out of its range (bad_word = the first such word)

//The PWM centres a calibration may store (adc_cal.h ACAL_PWM_MIN..ACAL_PWM_MAX)
#define SREC_PWM_MIN      200
#define SREC_PWM_MAX      1800

//Version and checksum only (as 1.00o5 checked): SREC_OK, SREC_BAD_VERSION or SREC_BAD_SUM
int srec_check_header(const uint16_t *w, uint16_t ver_hi, uint16_t ver_lo);

//The fields only: SREC_OK or SREC_BAD_FIELD with *bad_word = the word of the first failing check (bad_word may be 0)
int srec_check_fields(const uint16_t *w, uint16_t *bad_word);

//Both, header first. *bad_word is 0 unless SREC_BAD_FIELD.
int srec_check(const uint16_t *w, uint16_t ver_hi, uint16_t ver_lo, uint16_t *bad_word);

//Short text for a result ("valid", "version", "checksum", "value out of range", "read failed")
const char *srec_text(int result);

#endif /* SETTINGS_CHECK_H */
