//----------------------------------------------------------------------------------------------------------------------------------
// phos_map.h - digital phosphor hit-count map (fw 0.2.46, docs/plans/phosphor-design.md): rasterise records into an
// 8-bit per-pixel map, paint it through a palette, decay it per pass with a dithered factor for the elapsed time.
//
// Integer only, no globals, no hardware: builds for the scope and on the host (host_tests/test_phosphor.c).
//
// Layout: column-major, PHOS_H bytes per column (cells[x * PHOS_H + row]), so a record's vertical run per column and a
// column's lit range are contiguous. Cells are accessed as little-endian 32-bit words (4 rows per word): the scope's
// DDR is uncached (~90 cycles per access), so every hot loop works on words. ext[x] = lo | (hi + 1) << 16 is the range
// of rows that may be non-zero; 0 = empty column (a zeroed map is a valid empty map).
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PHOS_MAP_H
#define PHOS_MAP_H

#include <stdint.h>

#define PHOS_W            728       //columns: screen x 2..729
#define PHOS_H            400       //rows: screen y 49..448 (scope_get_y_sample's range)
#define PHOS_X0           2
#define PHOS_Y0           49

#define PHOS_PERSIST_02S  0
#define PHOS_PERSIST_1S   1
#define PHOS_PERSIST_5S   2
#define PHOS_PERSIST_INF  3
#define PHOS_PERSIST_COUNT 4

#define PHOS_DT_MAX       200       //ms of decay applied per pass at most (a stall freezes the picture instead of wiping it)
#define PHOS_INTERVAL_INIT 33       //ms between records assumed before the first measurement
#define PHOS_INTERVAL_MAX 2000
#define PHOS_K_ONE        (1u << 24)

typedef struct
{
  uint8_t  *cells;                  //PHOS_W * PHOS_H, 4-byte aligned
  uint32_t *ext;                    //PHOS_W
} phos_map;

//Decay per millisecond in Q24 for a persistence setting (PHOS_K_ONE = no decay): a cell no longer hit falls to 1/16
//in the persistence time T, k1 = exp(-ln 16 / (1000 T))
extern const uint32_t phos_persist_k1[PHOS_PERSIST_COUNT];

//k1^dt in Q24: the factor for dt milliseconds
uint32_t phos_k_dt(uint32_t k1, uint32_t dt);

//Q16 factor for phos_render (65536 = none)
static inline uint32_t phos_k16(uint32_t k_q24) { return (k_q24 + 128) >> 8; }

//Increment per hit for records interval_ms apart: 1.25 x 255 x (1 - k1^interval), 1..255; 1 without decay. A pixel hit
//by every record then settles at saturation whatever the record rate; one hit by a fraction p of the records at about
//p x 319.
uint32_t phos_inc(uint32_t k1, uint32_t interval_ms);

//Record interval estimate: exponential average over 8 records, interval_ms clamped to PHOS_INTERVAL_MAX
uint32_t phos_interval_update(uint32_t avg_ms, uint32_t interval_ms);

//Next dither seed (one step per decaying pass)
static inline uint32_t phos_seed_next(uint32_t seed) { return (seed + 40503u) & 0xFFFF; }

//Empty map: zero the lit runs, all extents empty
void phos_clear(phos_map *m);

//Add one record. codes: n samples (read as aligned words, so up to 3 bytes past both ends may be read); lut: code ->
//row 0..PHOS_H-1; sample i at column x0_q16 + i * dx_q16 (Q16, dx_q16 > 0). Columns outside xmin..xmax (inclusive,
//clipped to the map) are not written. Each column gets one run from its lowest to its highest point, extended towards
//the next column's first point (exclusive), so consecutive samples are joined like a line; columns between two
//samples are interpolated. Returns the cells touched (the cost model's unit).
uint32_t phos_add_record(phos_map *m, const uint8_t *codes, uint32_t n, const uint16_t *lut,
                         int32_t x0_q16, int32_t dx_q16, int32_t xmin, int32_t xmax, uint32_t inc);

//Add a line from (x1, y1) to (x2, y2) in map coordinates (Bresenham), without its first pixel when skip_first (for
//chained segments). Points outside the map are clipped per pixel. Returns the cells touched.
uint32_t phos_add_line(phos_map *m, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int skip_first, uint32_t inc);

//Paint the map, then decay it: dst points at the pixel of column 0, row 0; stride in pixels. Non-zero cells get
//pal[count], zero cells are left alone. With k16 < 65536 every lit cell then decays, count = (count k16 + d) >> 16 with
//d = (seed + 25033 x column) mod 65536 (dither: the expected count is exactly count x k16 / 65536 and every cell
//reaches 0), and the extents shrink to the words still lit. With under != 0 (already rendered this pass, so already
//decayed with the same k16) a cell is painted only where its decayed count exceeds under's: the brighter channel wins.
//Returns the pixels written.
uint32_t phos_render(phos_map *m, const phos_map *under, uint16_t *dst, uint32_t stride, const uint16_t *pal,
                     uint32_t k16, uint32_t seed);

//Cells currently non-zero (tests and diagnostics; reads the whole lit area)
uint32_t phos_lit(const phos_map *m);

//Palettes (256 RGB565 entries, entry 0 unused): brightness on a square-root curve of the count, 24 % for one hit to
//100 % at saturation. Mono = one colour (0x00RRGGBB); heat = dark blue, blue, cyan, green, yellow, red, white.
void phos_palette_mono(uint16_t *pal, uint32_t rgb);
void phos_palette_heat(uint16_t *pal);

uint32_t phos_isqrt(uint32_t v);

#endif
