//----------------------------------------------------------------------------------------------------------------------------------
// Host test for pc_mmu.c (fw 0.2.48): the flat page table. Every 4 KB page of the 4 GB space is walked through the
// tables and compared with the intended attribute (docs/plans/mmu-dcache.md): no fault descriptors, flat mapping,
// framebuffer pages write-through, DDR write-back, the restart record section and everything else strongly ordered.
//----------------------------------------------------------------------------------------------------------------------------------

#include "test_util.h"
#include "pc_mmu.h"

#define L2_ADDR 0x80700400u                  //where the coarse tables would sit (any 1 KB aligned address)

static uint32_t l1[PC_MMU_L1_ENTRIES];
static uint32_t l2[PC_MMU_L2_TABLES][PC_MMU_L2_ENTRIES];

static uint32_t expect(uint32_t addr, uint32_t fb_start, uint32_t fb_len, uint32_t sram_attr)
{
  if(fb_len && (addr >= (fb_start & 0xFFFFF000u)) && (addr <= ((fb_start + fb_len - 1) | 0xFFFu)))
    return PC_MMU_WT;

  if((addr >> 20) == 0)
    return sram_attr;

  if((addr >> 20) == (PC_MMU_RST_BASE >> 20))
    return PC_MMU_SO;

  if((addr >= PC_MMU_DDR_BASE) && (addr < PC_MMU_DDR_BASE + PC_MMU_DDR_SIZE))
    return PC_MMU_WB;

  return PC_MMU_SO;
}

//Walk every page (and one address inside it) and count mismatches; one check per call keeps the totals readable
static void check_all(const char *what, uint32_t fb_start, uint32_t fb_len, uint32_t sram_attr)
{
  uint32_t page, bad = 0, wt = 0;

  for(page=0;page<(1u << 20);page++)
  {
    uint32_t a = page << 12;
    uint32_t got0 = pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, a);
    uint32_t got1 = pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, a | 0xABCu);
    uint32_t want = expect(a, fb_start, fb_len, sram_attr);

    if((got0 != want) || (got1 != want))
    {
      if(bad < 4)
        printf("%s: page 0x%08X got %X / %X want %X\n", what, a, got0, got1, want);
      bad++;
    }

    if(got0 == PC_MMU_WT)
      wt++;
  }

  T_EQ(bad, 0);

  //Exactly the pages holding the framebuffer are write-through
  if(fb_len)
    T_EQ(wt, (((fb_start + fb_len - 1) >> 12) - (fb_start >> 12)) + 1);
  else
    T_EQ(wt, 0);

  (void)what;
}

static void check_descriptors(int32_t used)
{
  uint32_t i, j, coarse = 0;

  for(i=0;i<PC_MMU_L1_ENTRIES;i++)
  {
    uint32_t d = l1[i];

    if((d & 3u) == 1u)
    {
      //Coarse pointer: 1 KB aligned into our tables, domain 0, bit 4
      T_EQ((d & 0xFFFFFC00u) - L2_ADDR, coarse * PC_MMU_L2_ENTRIES * 4);
      T_EQ(d & 0x3FCu, 1u << 4);
      coarse++;

      for(j=0;j<PC_MMU_L2_ENTRIES;j++)
      {
        uint32_t p = l2[(d & 0xFFFFFC00u) == L2_ADDR ? 0 : 1][j];
        if(((p & 3u) != 2u) || ((p & 0xFF0u) != 0xFF0u) || ((p >> 12) != ((i << 8) | j)))
        {
          T_EQ(p, ((i << 20) | (j << 12) | 0xFF2u));   //report the first bad page entry
          break;
        }
      }
    }
    else
    {
      //Section: flat, AP 11, domain 0, bit 4, SBZ bits clear, no fault descriptors
      if(((d & 3u) != 2u) || ((d >> 20) != i) || ((d & 0x000FFFF3u & ~0xCu) != ((3u << 10) | (1u << 4) | 2u)))
      {
        T_EQ(d, (i << 20) | (3u << 10) | (1u << 4) | 2u);
        break;
      }
    }
  }

  T_EQ(coarse, (uint32_t)used);
}

static void test_layout_0_2_46(void)
{
  //maindisplaybuffer in the GCC 10 0.2.46 build (main 1950e58): 0x804DD8E0, 800 x 480 x 2 bytes, crosses 0x80500000
  int32_t n = pc_mmu_build(l1, l2, L2_ADDR, 0x804DD8E0u, 768000u, PC_MMU_WB);

  T_EQ(n, 2);
  check_descriptors(n);
  check_all("0.2.46 layout", 0x804DD8E0u, 768000u, PC_MMU_WB);

  //Spot checks of the regions the firmware relies on
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x00000000u), PC_MMU_WB);  //vectors, SRAM A
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x00007C00u), PC_MMU_WB);  //FEL stack
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x01C13400u), PC_MMU_SO);  //USB PHY
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x01C20890u), PC_MMU_SO);  //Port E (FPGA bus)
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x01C0F000u), PC_MMU_SO);  //SD controller
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x01E60850u), PC_MMU_SO);  //DEBE
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x7FFFFFE0u), PC_MMU_SO);  //eGON header
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x80000000u), PC_MMU_WB);  //_reset
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x804DD000u), PC_MMU_WT);  //first FB page
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x804DCFFCu), PC_MMU_WB);  //just below it
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x805990DFu), PC_MMU_WT);  //last FB byte
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x8059A000u), PC_MMU_WB);  //next page
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x81A00000u), PC_MMU_SO);  //restart record
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x81BFFC00u), PC_MMU_WB);  //sector 710 copy
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x81FFFFFCu), PC_MMU_WB);  //SYS stack top
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x82000000u), PC_MMU_SO);  //above DDR
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0xFFFF0020u), PC_MMU_SO);  //boot ROM FEL
}

static void test_other_layouts(void)
{
  int32_t n;

  //Inside one section, page aligned, one page
  n = pc_mmu_build(l1, l2, L2_ADDR, 0x80100000u, 4096u, PC_MMU_WB);
  T_EQ(n, 1);
  check_descriptors(n);
  check_all("one page", 0x80100000u, 4096u, PC_MMU_WB);

  //Unaligned at both ends, ending exactly at a section boundary
  n = pc_mmu_build(l1, l2, L2_ADDR, 0x80F00002u, 0x000FFFFEu, PC_MMU_WB);
  T_EQ(n, 1);
  check_all("to boundary", 0x80F00002u, 0x000FFFFEu, PC_MMU_WB);

  //One byte over the boundary needs the second table
  n = pc_mmu_build(l1, l2, L2_ADDR, 0x80F00002u, 0x000FFFFFu, PC_MMU_WB);
  T_EQ(n, 2);
  check_all("over boundary", 0x80F00002u, 0x000FFFFFu, PC_MMU_WB);

  //SRAM A strongly ordered (PC_MMU_SRAM=0)
  n = pc_mmu_build(l1, l2, L2_ADDR, 0x804DD8E0u, 768000u, PC_MMU_SO);
  T_EQ(n, 2);
  check_all("SRAM SO", 0x804DD8E0u, 768000u, PC_MMU_SO);

  //No framebuffer: sections only
  n = pc_mmu_build(l1, l2, L2_ADDR, 0x804DD8E0u, 0u, PC_MMU_WB);
  T_EQ(n, 0);
  check_descriptors(n);
  check_all("no fb", 0u, 0u, PC_MMU_WB);

  //More than two sections, or a range that wraps: refused (the caller keeps the MMU off)
  T_EQ(pc_mmu_build(l1, l2, L2_ADDR, 0x80000800u, 0x00200000u, PC_MMU_WB), -1);
  T_EQ(pc_mmu_build(l1, l2, L2_ADDR, 0xFFFFF000u, 0x00002000u, PC_MMU_WB), -1);
}

static void test_walk_rejects(void)
{
  uint32_t save;

  pc_mmu_build(l1, l2, L2_ADDR, 0x804DD8E0u, 768000u, PC_MMU_WB);

  //The walk is the checker: make sure it actually catches broken descriptors
  save = l1[0x801];
  l1[0x801] = 0;                                                    //fault descriptor
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x80100000u), 0xFFFFFFFFu);
  l1[0x801] = save + (1u << 20);                                    //not flat
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x80100000u), 0xFFFFFFFFu);
  l1[0x801] = save & ~(3u << 10);                                   //AP 00
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x80100000u), 0xFFFFFFFFu);
  l1[0x801] = save | (1u << 5);                                     //domain 1
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x80100000u), 0xFFFFFFFFu);
  l1[0x801] = save;

  save = l2[0][0xDD];
  l2[0][0xDD] = save & ~0xF0u;                                      //AP0 00 in a small page
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x804DD000u), 0xFFFFFFFFu);
  l2[0][0xDD] = save;
  T_EQ(pc_mmu_walk(l1, (const uint32_t (*)[PC_MMU_L2_ENTRIES])l2, L2_ADDR, 0x804DD000u), PC_MMU_WT);
}

int main(void)
{
  test_layout_0_2_46();
  test_other_layouts();
  test_walk_rejects();

  return t_report("test_mmu");
}
