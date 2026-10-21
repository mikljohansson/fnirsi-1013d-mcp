//----------------------------------------------------------------------------------------------------------------------------------
// test_trig_ui.c - the trigger menu's Type row as data (trig_ui.c, docs/plans/ui-rows.md section 2): the ladder, the
// word period per rate, the time-to-words conversion and the rate's range at 200 MSa/s, 1 MSa/s with the Filter, 10 kSa/s,
// 500 kSa/s, 500 Sa/s and EXT (the note's table in 2.4), the snap on a rate change, the steps at the ends, the registers
// each type arms, the host round trip (SET_TRIGGER2 shown as the nearest step), the settings words, the texts and the chip
// glyphs as pixels. Run natively.
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "trig_ui.h"
#include "test_util.h"

//The firmware's dividers (variables.c sample_rate_settings): 200 MSa/s 0, 1 MSa/s 199, 500 kSa/s 399, 10 kSa/s 19999,
//500 Sa/s 399999
#define D_200M    0
#define D_1M      199
#define D_500K    399
#define D_10K     19999
#define D_500     399999

//Ladder indices
enum { I_10NS, I_20NS, I_50NS, I_100NS, I_200NS, I_500NS, I_1US, I_2US, I_5US, I_10US, I_20US, I_50US, I_100US, I_200US,
       I_500US, I_1MS, I_2MS, I_5MS, I_10MS, I_20MS, I_50MS, I_100MS, I_200MS, I_500MS, I_1S, I_2S, I_5S, I_10S };

static void test_ladder(void)
{
  uint32 i;

  T_EQ(TUI_STEPS, 28);
  T_EQ(tui_step_t5(I_10NS), 2);
  T_EQ(tui_step_t5(I_20NS), 4);
  T_EQ(tui_step_t5(I_50NS), 10);
  T_EQ(tui_step_t5(I_1US), 200);
  T_EQ(tui_step_t5(I_2US), 400);
  T_EQ(tui_step_t5(I_50US), 10000);
  T_EQ(tui_step_t5(I_1MS), 200000);
  T_EQ(tui_step_t5(I_10S), 2000000000u);

  for(i = 1; i < TUI_STEPS; i++)
    T_TRUE(tui_step_t5(i) > tui_step_t5(i - 1));

  T_STR(tui_step_text(I_10NS), "10ns");
  T_STR(tui_step_text(I_2US), "2us");
  T_STR(tui_step_text(I_50US), "50us");
  T_STR(tui_step_text(I_1MS), "1ms");
  T_STR(tui_step_text(I_10S), "10s");
  T_STR(tui_step_text(99), "");
  T_STR(tui_idle_text(TUI_IDLE_OFF), "Off");
  T_STR(tui_idle_text(1 + I_50US), "50us");
}

//The word period: legacy 10 (D + 1) ns, 16-bit N x 5 ns with N = 2 (floor(D / 2) + 1), EXT 5 ns
static void test_tw(void)
{
  T_EQ(tui_tw5(D_200M, 0, 0), 2);              //10 ns
  T_EQ(tui_tw5(D_200M, 1, 0), 2);              //N = 2: 10 ns
  T_EQ(tui_tw5(D_1M, 0, 0), 400);              //2 us
  T_EQ(tui_tw5(D_1M, 1, 0), 200);              //N = 200: 1 us
  T_EQ(tui_tw5(D_500K, 0, 0), 800);            //4 us
  T_EQ(tui_tw5(D_500K, 1, 0), 400);            //2 us
  T_EQ(tui_tw5(D_10K, 0, 0), 40000);           //200 us
  T_EQ(tui_tw5(D_10K, 1, 0), 20000);           //100 us
  T_EQ(tui_tw5(D_500, 0, 0), 800000);          //4 ms
  T_EQ(tui_tw5(D_500, 1, 0), 400000);          //2 ms
  T_EQ(tui_tw5(D_500, 0, 1), 1);               //EXT at any rate
  T_EQ(tui_tw5(D_200M, 1, 1), 1);
}

//words = round(t / Tw), the ranges of the note's table (2.4) and the three rates of the brief
static void test_words_and_ranges(void)
{
  uint32 lo, hi;

  //200 MSa/s (Tw 10 ns): Width 20ns .. 100ms (2 .. 10^7 words; 200ms = 2 x 10^7 > 2^24 - 1), Idle 10ns .. 100ms
  T_EQ(tui_words(I_10NS, 2), 1);
  T_EQ(tui_words(I_20NS, 2), 2);
  T_EQ(tui_words(I_2US, 2), 200);
  T_EQ(tui_words(I_100MS, 2), 10000000);
  T_EQ(tui_words(I_200MS, 2), 20000000);
  tui_range(2, TUI_WIDTH_MIN, &lo, &hi);
  T_EQ(lo, I_20NS); T_EQ(hi, I_100MS);
  tui_range(2, TUI_IDLE_MIN, &lo, &hi);
  T_EQ(lo, I_10NS); T_EQ(hi, I_100MS);

  //1 MSa/s with the Filter (Tw 1 us): 2us = 2 words is the shortest Width (1us = 1 word is not), 10s = 10^7 words
  T_EQ(tui_words(I_1US, 200), 1);
  T_EQ(tui_words(I_2US, 200), 2);
  T_EQ(tui_words(I_500US, 200), 500);
  T_EQ(tui_words(I_10S, 200), 10000000);
  tui_range(200, TUI_WIDTH_MIN, &lo, &hi);
  T_EQ(lo, I_2US); T_EQ(hi, I_10S);
  tui_range(200, TUI_IDLE_MIN, &lo, &hi);
  T_EQ(lo, I_500NS); T_EQ(hi, I_10S);                   //500ns is half a word, rounded up to 1 (the same rule as the
                                                        //Width's 10us at 4 us words in the note's table)
  //1 MSa/s legacy (Tw 2 us): 5us rounds to 3 words (half up: 2.5), 2us to 1
  T_EQ(tui_words(I_2US, 400), 1);
  T_EQ(tui_words(I_5US, 400), 3);
  tui_range(400, TUI_WIDTH_MIN, &lo, &hi);
  T_EQ(lo, I_5US); T_EQ(hi, I_10S);

  //10 kSa/s legacy (Tw 200 us): 500us = 3 words (2.5 up), 200us = 1, 100us = 1 (0.5 up), 50us = 0; 10s = 50000 words
  T_EQ(tui_words(I_50US, 40000), 0);
  T_EQ(tui_words(I_100US, 40000), 1);
  T_EQ(tui_words(I_200US, 40000), 1);
  T_EQ(tui_words(I_500US, 40000), 3);
  T_EQ(tui_words(I_10S, 40000), 50000);
  tui_range(40000, TUI_WIDTH_MIN, &lo, &hi);
  T_EQ(lo, I_500US); T_EQ(hi, I_10S);
  tui_range(40000, TUI_IDLE_MIN, &lo, &hi);
  T_EQ(lo, I_100US); T_EQ(hi, I_10S);
  //10 kSa/s with the Filter (Tw 100 us): 200us = 2 words
  tui_range(20000, TUI_WIDTH_MIN, &lo, &hi);
  T_EQ(lo, I_200US); T_EQ(hi, I_10S);

  //The note's table: 500 kSa/s 10us legacy / 5us 16-bit .. 10s; 500 Sa/s 10ms / 5ms .. 10s; EXT 10ns .. 50ms
  tui_range(800, TUI_WIDTH_MIN, &lo, &hi);
  T_EQ(lo, I_10US); T_EQ(hi, I_10S);
  tui_range(400, TUI_WIDTH_MIN, &lo, &hi);
  T_EQ(lo, I_5US); T_EQ(hi, I_10S);
  tui_range(800000, TUI_WIDTH_MIN, &lo, &hi);
  T_EQ(lo, I_10MS); T_EQ(hi, I_10S);
  T_EQ(tui_words(I_10S, 800000), 2500);
  tui_range(400000, TUI_WIDTH_MIN, &lo, &hi);
  T_EQ(lo, I_5MS); T_EQ(hi, I_10S);
  tui_range(1, TUI_WIDTH_MIN, &lo, &hi);
  T_EQ(lo, I_10NS); T_EQ(hi, I_50MS);
  T_EQ(tui_words(I_50MS, 1), 10000000);
  T_EQ(tui_words(I_100MS, 1), 20000000);
  tui_range(1, TUI_IDLE_MIN, &lo, &hi);
  T_EQ(lo, I_10NS); T_EQ(hi, I_50MS);                  //decided 6: EXT Idle Off, then the 10ns floor; no 5ns step
}

//A rate change snaps the chosen type's parameter to the nearest step inside the new range and says which one moved
static void test_snap(void)
{
  tui_state s;

  //Pulse, Width 10s at 500 Sa/s; to 200 MSa/s: 10s is 10^9 words, the longest step is 100ms
  tui_defaults(&s);
  tui_set_type(&s, TUI_PULSE, 800000);
  s.width_i = I_10S;
  T_EQ(tui_rate(&s, 800000), TUI_SNAP_NONE);
  T_EQ(tui_rate(&s, 2), TUI_SNAP_WIDTH);
  T_EQ(s.width_i, I_100MS);
  T_EQ(tui_rate(&s, 2), TUI_SNAP_NONE);                 //settled: no second notice

  //Width 20ns at 200 MSa/s; to 1 MSa/s with the Filter: the shortest step is 2us
  s.width_i = I_20NS;
  T_EQ(tui_rate(&s, 200), TUI_SNAP_WIDTH);
  T_EQ(s.width_i, I_2US);
  //and to 10 kSa/s legacy: 500us
  T_EQ(tui_rate(&s, 40000), TUI_SNAP_WIDTH);
  T_EQ(s.width_i, I_500US);
  //a step inside both ranges does not move (100ms: the top step at 200 MSa/s, 25 words at 500 Sa/s)
  s.width_i = I_100MS;
  T_EQ(tui_rate(&s, 2), TUI_SNAP_NONE);
  T_EQ(tui_rate(&s, 800000), TUI_SNAP_NONE);
  T_EQ(s.width_i, I_100MS);
  //1ms at 500 Sa/s is a quarter word: the floor of that rate, 10ms
  s.width_i = I_1MS;
  T_EQ(tui_rate(&s, 800000), TUI_SNAP_WIDTH);
  T_EQ(s.width_i, I_10MS);

  //Nth: the Idle time snaps the same way, Off never does
  tui_defaults(&s);
  tui_set_type(&s, TUI_NTH, 2);
  s.idle_i = 1 + I_10NS;
  T_EQ(tui_rate(&s, 2), TUI_SNAP_NONE);
  T_EQ(tui_rate(&s, 40000), TUI_SNAP_IDLE);
  T_EQ(s.idle_i, 1 + I_100US);
  s.idle_i = TUI_IDLE_OFF;
  T_EQ(tui_rate(&s, 2), TUI_SNAP_NONE);
  T_EQ(s.idle_i, TUI_IDLE_OFF);

  //Edge and the host's timeout: nothing to snap; the other types' parameters are left as they are
  tui_defaults(&s);
  s.width_i = I_10S;
  T_EQ(tui_rate(&s, 2), TUI_SNAP_NONE);
  T_EQ(s.width_i, I_10S);
  s.type = TUI_TIMEOUT;
  T_EQ(tui_rate(&s, 2), TUI_SNAP_NONE);

  //A type chosen snaps its parameter silently
  tui_defaults(&s);
  s.width_i = I_10NS;
  tui_set_type(&s, TUI_PULSE, 2);
  T_EQ(s.width_i, I_20NS);
  s.idle_i = 1 + I_10S;
  tui_set_type(&s, TUI_NTH, 1);
  T_EQ(s.idle_i, 1 + I_50MS);
  tui_set_type(&s, 7, 2);                               //never a type the row lacks
  T_EQ(s.type, TUI_EDGE);
}

//The - / + stop at the rate's ends; Idle steps through Off
static void test_steps(void)
{
  tui_state s;
  int       k;

  tui_defaults(&s);
  tui_set_type(&s, TUI_PULSE, 2);                       //200 MSa/s: 20ns .. 100ms
  s.width_i = I_50NS;
  T_EQ(tui_step_width(&s, -1, 2), 1); T_EQ(s.width_i, I_20NS);
  T_EQ(tui_step_width(&s, -1, 2), 0); T_EQ(s.width_i, I_20NS);
  for(k = 0; k < 40; k++)
    (void)tui_step_width(&s, +1, 2);
  T_EQ(s.width_i, I_100MS);
  T_EQ(tui_step_width(&s, +1, 2), 0);
  T_EQ(tui_step_width(&s, -1, 2), 1); T_EQ(s.width_i, I_50MS);

  //Idle at 10 kSa/s legacy: Off <-> 100us .. 10s
  tui_defaults(&s);
  tui_set_type(&s, TUI_NTH, 40000);
  s.idle_i = TUI_IDLE_OFF;
  T_EQ(tui_step_idle(&s, -1, 40000), 0); T_EQ(s.idle_i, TUI_IDLE_OFF);
  T_EQ(tui_step_idle(&s, +1, 40000), 1); T_EQ(s.idle_i, 1 + I_100US);
  T_EQ(tui_step_idle(&s, -1, 40000), 1); T_EQ(s.idle_i, TUI_IDLE_OFF);
  s.idle_i = 1 + I_10S;
  T_EQ(tui_step_idle(&s, +1, 40000), 0);
  T_EQ(tui_step_idle(&s, -1, 40000), 1); T_EQ(s.idle_i, 1 + I_5S);
  //EXT: Off, then 10ns (decided 6)
  s.idle_i = TUI_IDLE_OFF;
  T_EQ(tui_step_idle(&s, +1, 1), 1); T_EQ(s.idle_i, 1 + I_10NS);

  //N by 1 in 1 .. 65535, the keypad's value clamped
  s.n = 1;
  T_EQ(tui_step_n(&s, -1), 0); T_EQ(s.n, 1);
  T_EQ(tui_step_n(&s, +1), 1); T_EQ(s.n, 2);
  s.n = 65535;
  T_EQ(tui_step_n(&s, +1), 0); T_EQ(s.n, 65535);
  T_EQ(tui_step_n(&s, -1), 1); T_EQ(s.n, 65534);
  tui_set_n(&s, 0);       T_EQ(s.n, 1);
  tui_set_n(&s, -5);      T_EQ(s.n, 1);
  tui_set_n(&s, 70000);   T_EQ(s.n, 65535);
  tui_set_n(&s, 1500);    T_EQ(s.n, 1500);
}

//What each type arms: 0x5E type and bit 3, 0x5F N, 0x60 H (fpga_trig's fields through the glue)
static void test_hw(void)
{
  tui_state s;
  uint8     type, longer;
  uint32    n, h;

  tui_defaults(&s);
  tui_hw(&s, 2, &type, &longer, &n, &h);
  T_EQ(type, TUI_EDGE); T_EQ(longer, 0); T_EQ(n, 0); T_EQ(h, 0);

  //Pulse Width 2us Shorter at 1 MSa/s with the Filter: N = 2 words; Longer sets the bit; at 200 MSa/s 200 words
  tui_set_type(&s, TUI_PULSE, 200);
  tui_hw(&s, 200, &type, &longer, &n, &h);
  T_EQ(type, TUI_PULSE); T_EQ(longer, 0); T_EQ(n, 2); T_EQ(h, 0);
  s.longer = 1;
  tui_hw(&s, 2, &type, &longer, &n, &h);
  T_EQ(type, TUI_PULSE); T_EQ(longer, 1); T_EQ(n, 200); T_EQ(h, 0);
  //10 kSa/s legacy: 2us is below the range; the glue snaps first (tui_rate), so the words are a step's
  T_EQ(tui_rate(&s, 40000), TUI_SNAP_WIDTH);
  tui_hw(&s, 40000, &type, &longer, &n, &h);
  T_EQ(n, 3);                                           //500us / 200us = 2.5, rounded up: 3 words (600 us armed)

  //Nth N 5, Idle 50us: at 200 MSa/s H = 5000 words; at 1 MSa/s with the Filter 50; Off = 0
  tui_defaults(&s);
  tui_set_type(&s, TUI_NTH, 2);
  s.n = 5;
  tui_hw(&s, 2, &type, &longer, &n, &h);
  T_EQ(type, TUI_NTH); T_EQ(longer, 0); T_EQ(n, 5); T_EQ(h, 5000);
  tui_hw(&s, 200, &type, &longer, &n, &h);
  T_EQ(n, 5); T_EQ(h, 50);
  s.idle_i = TUI_IDLE_OFF;
  tui_hw(&s, 200, &type, &longer, &n, &h);
  T_EQ(n, 5); T_EQ(h, 0);
  s.n = 65535;
  tui_hw(&s, 200, &type, &longer, &n, &h);
  T_EQ(n, 65535);

  //The host's timeout is not the UI's: armed as Edge if the UI ever re-derives it
  s.type = TUI_TIMEOUT;
  tui_hw(&s, 2, &type, &longer, &n, &h);
  T_EQ(type, TUI_EDGE); T_EQ(n, 0); T_EQ(h, 0);
}

//A host SET_TRIGGER2 shows as the nearest ladder step (the host's exact words are armed as sent, pc_hal_scope.c)
static void test_from_hw(void)
{
  tui_state s;

  tui_defaults(&s);
  //pulse width longer than 1500 words at 1 MSa/s with the Filter = 1.5 ms: 1ms (0.5 away) or 2ms (0.5 away): the lower
  tui_from_hw(&s, TUI_PULSE, 1, 1500, 0, 200);
  T_EQ(s.type, TUI_PULSE); T_EQ(s.longer, 1); T_EQ(s.width_i, I_1MS);
  tui_from_hw(&s, TUI_PULSE, 0, 1600, 0, 200);          //1.6 ms: 2ms
  T_EQ(s.longer, 0); T_EQ(s.width_i, I_2MS);
  //a width the ladder cannot hold at this rate shows the end of the range (1 word at 200 MSa/s: 20ns is the floor)
  tui_from_hw(&s, TUI_PULSE, 0, 1, 0, 2);
  T_EQ(s.width_i, I_20NS);
  tui_from_hw(&s, TUI_PULSE, 0, 16777215, 0, 2);
  T_EQ(s.width_i, I_100MS);
  //an exact step round-trips
  tui_from_hw(&s, TUI_PULSE, 0, tui_words(I_500US, 400), 0, 400);
  T_EQ(s.width_i, I_500US);
  tui_from_hw(&s, TUI_PULSE, 0, tui_words(I_10US, 800), 0, 800);       //3 words = 12 us: still 10us
  T_EQ(s.width_i, I_10US);

  //Nth: N clamped, H as the nearest step, 0 = Off; the Width of the Pulse type is kept
  tui_from_hw(&s, TUI_NTH, 0, 3, 100000, 200);          //100 ms at 1 us
  T_EQ(s.type, TUI_NTH); T_EQ(s.n, 3); T_EQ(s.idle_i, 1 + I_100MS); T_EQ(s.width_i, I_10US);
  tui_from_hw(&s, TUI_NTH, 0, 0, 0, 200);
  T_EQ(s.n, 1); T_EQ(s.idle_i, TUI_IDLE_OFF);
  tui_from_hw(&s, TUI_NTH, 0, 100000, 1, 1);            //EXT, H 1 clock = 5 ns: the 10ns floor
  T_EQ(s.n, 65535); T_EQ(s.idle_i, 1 + I_10NS);

  //Edge keeps every parameter; the timeout is shown as no type (no box chosen)
  tui_from_hw(&s, TUI_EDGE, 0, 0, 0, 200);
  T_EQ(s.type, TUI_EDGE); T_EQ(s.n, 65535); T_EQ(s.idle_i, 1 + I_10NS); T_EQ(s.width_i, I_10US);
  tui_from_hw(&s, TUI_TIMEOUT, 0, 2000, 0, 200);
  T_EQ(s.type, TUI_TIMEOUT); T_EQ(s.width_i, I_10US);
}

//Words 202 / 203: the marker, every field, the defaults on a sector without it
static void test_words(void)
{
  tui_state s, r;
  uint16    w0, w1;

  tui_defaults(&s);
  T_EQ(s.type, TUI_EDGE); T_EQ(s.longer, 0); T_EQ(s.width_i, I_2US); T_EQ(s.idle_i, 1 + I_50US); T_EQ(s.n, 2);
  w0 = tui_word0(&s);
  T_EQ(w0 & 0xE000, 0xA000);
  T_EQ(w0 & 3, 0);
  T_EQ((w0 >> 3) & 31, I_2US);
  T_EQ((w0 >> 8) & 31, 1 + I_50US);
  T_EQ(tui_word1(&s), 2);

  s.type = TUI_NTH; s.longer = 1; s.width_i = I_10S; s.idle_i = TUI_IDLE_OFF; s.n = 65535;
  w0 = tui_word0(&s);
  w1 = tui_word1(&s);
  T_EQ(w0, 0xA000 | 2 | 4 | (I_10S << 3));
  T_EQ(w1, 65535);
  T_EQ(tui_restore(&r, w0, w1), 1);
  T_EQ(r.type, TUI_NTH); T_EQ(r.longer, 1); T_EQ(r.width_i, I_10S); T_EQ(r.idle_i, TUI_IDLE_OFF); T_EQ(r.n, 65535);

  s.type = TUI_PULSE; s.idle_i = 1 + I_10S;
  T_EQ(tui_restore(&r, tui_word0(&s), tui_word1(&s)), 1);
  T_EQ(r.type, TUI_PULSE); T_EQ(r.idle_i, 1 + I_10S);

  //The host's timeout is stored as Edge
  s.type = TUI_TIMEOUT;
  T_EQ(tui_word0(&s) & 3, TUI_EDGE);

  //A sector written before this firmware holds 0: Edge with the defaults; a wrong marker or field likewise
  T_EQ(tui_restore(&r, 0, 0), 0);
  T_EQ(r.type, TUI_EDGE); T_EQ(r.width_i, I_2US); T_EQ(r.n, 2);
  T_EQ(tui_restore(&r, 0xC701, 5), 0);
  T_EQ(tui_restore(&r, 0xA003, 5), 0);                  //type 3 is never stored
  T_EQ(tui_restore(&r, (uint16)(0xA000 | (28 << 3)), 5), 0);
  T_EQ(tui_restore(&r, (uint16)(0xA000 | (29 << 8)), 5), 0);
  T_EQ(tui_restore(&r, 0xA000, 0), 0);                  //N 0
  T_EQ(tui_restore(&r, (uint16)(0xA000 | (28 << 8)), 1), 1);
  T_EQ(r.idle_i, 28);
}

static void test_texts(void)
{
  char b[8];

  tui_n_text(b, 1);     T_STR(b, "1");
  tui_n_text(b, 5);     T_STR(b, "5");
  tui_n_text(b, 65535); T_STR(b, "65535");
  tui_n_text(b, 1000);  T_STR(b, "1000");
  T_EQ(tui_n_text(b, 42) - b, 2);
}

//The glyphs rasterised from their rectangles against the mockups (rows.py glyph(): a 2 px stroke on whole pixels)
static void raster(uint32 kind, char img[TUI_GLYPH_SIZE][TUI_GLYPH_SIZE + 1])
{
  tui_rect r[TUI_GLYPH_RECTS];
  uint32   n = tui_glyph_rects(kind, r), i, x, y;

  T_TRUE(n <= TUI_GLYPH_RECTS);

  for(y = 0; y < TUI_GLYPH_SIZE; y++)
  {
    memset(img[y], '.', TUI_GLYPH_SIZE);
    img[y][TUI_GLYPH_SIZE] = 0;
  }

  for(i = 0; i < n; i++)
  {
    T_TRUE(r[i].x + r[i].w <= TUI_GLYPH_SIZE);
    T_TRUE(r[i].y + r[i].h <= TUI_GLYPH_SIZE);
    for(y = r[i].y; y < (uint32)(r[i].y + r[i].h); y++)
      for(x = r[i].x; x < (uint32)(r[i].x + r[i].w); x++)
        img[y][x] = '#';
  }
}

static void check_glyph(uint32 kind, const char *const want[TUI_GLYPH_SIZE], const char *name)
{
  char   img[TUI_GLYPH_SIZE][TUI_GLYPH_SIZE + 1];
  uint32 y;
  int    ok = 1;

  raster(kind, img);

  for(y = 0; y < TUI_GLYPH_SIZE; y++)
    if(strcmp(img[y], want[y]))
      ok = 0;

  if(!ok)
  {
    printf("glyph %s differs:\n", name);
    for(y = 0; y < TUI_GLYPH_SIZE; y++)
      printf("  %s   %s\n", img[y], want[y]);
  }

  T_TRUE(ok);
}

static void test_glyphs(void)
{
  static const char *const rise[14] =
  {
    "..............",
    ".....########.",
    ".....########.",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    "#######.......",
    "#######.......",
    ".............."
  };
  static const char *const fall[14] =
  {
    "..............",
    "#######.......",
    "#######.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....##.......",
    ".....########.",
    ".....########.",
    ".............."
  };
  static const char *const pulse_p[14] =
  {
    "..............",
    "..#########...",
    "..#########...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "####.....####.",
    "####.....####.",
    ".............."
  };
  static const char *const pulse_n[14] =
  {
    "..............",
    "####.....####.",
    "####.....####.",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..##.....##...",
    "..#########...",
    "..#########...",
    ".............."
  };
  //Not the mockup's rendering (a block, trig_ui.c): two short rising steps with stubs and the tall edge on one baseline
  static const char *const nth[14] =
  {
    "..............",
    ".........####.",
    ".........####.",
    ".........##...",
    ".........##...",
    ".........##...",
    ".........##...",
    ".........##...",
    ".###.###.##...",
    ".###.###.##...",
    ".##..##..##...",
    "###########...",
    "###########...",
    ".............."
  };

  check_glyph(TUI_GLYPH_RISE, rise, "rise");
  check_glyph(TUI_GLYPH_FALL, fall, "fall");
  check_glyph(TUI_GLYPH_PULSE_P, pulse_p, "pulse+");
  check_glyph(TUI_GLYPH_PULSE_N, pulse_n, "pulse-");
  check_glyph(TUI_GLYPH_NTH, nth, "nth");

  //The chip picks the glyph from the type and the edge / polarity row
  T_EQ(tui_glyph_kind(TUI_EDGE, 0), TUI_GLYPH_RISE);
  T_EQ(tui_glyph_kind(TUI_EDGE, 1), TUI_GLYPH_FALL);
  T_EQ(tui_glyph_kind(TUI_PULSE, 0), TUI_GLYPH_PULSE_P);
  T_EQ(tui_glyph_kind(TUI_PULSE, 1), TUI_GLYPH_PULSE_N);
  T_EQ(tui_glyph_kind(TUI_NTH, 0), TUI_GLYPH_NTH);
  T_EQ(tui_glyph_kind(TUI_NTH, 1), TUI_GLYPH_NTH);
  T_EQ(tui_glyph_kind(TUI_TIMEOUT, 1), TUI_GLYPH_FALL);
}

int main(void)
{
  test_ladder();
  test_tw();
  test_words_and_ranges();
  test_snap();
  test_steps();
  test_hw();
  test_from_hw();
  test_words();
  test_texts();
  test_glyphs();
  return t_report("test_trig_ui");
}
