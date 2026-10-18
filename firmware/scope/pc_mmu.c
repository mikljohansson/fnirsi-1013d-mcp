//----------------------------------------------------------------------------------------------------------------------------------
// MMU + D-cache (fw 0.2.48). Design and reasoning: docs/plans/mmu-dcache.md. The leaving side (clean + disable at
// _reset and before the FEL jump) is assembly in start.s (pc_mmu_off) because it must run without a stack.
//----------------------------------------------------------------------------------------------------------------------------------

#include "pc_mmu.h"

//Section: base | AP 11 | domain 0 | bit 4 (ARM926: should be 1) | C B | type 10
#define PC_MMU_SECTION(base, attr)   (((base) & 0xFFF00000u) | (3u << 10) | (1u << 4) | (attr) | 0x2u)
//Coarse table pointer: base (1 KB aligned) | domain 0 | bit 4 | type 01
#define PC_MMU_COARSE(base)          (((base) & 0xFFFFFC00u) | (1u << 4) | 0x1u)
//Small page (4 KB): base | AP3..AP0 all 11 | C B | type 10
#define PC_MMU_SMALL(base, attr)     (((base) & 0xFFFFF000u) | (0xFFu << 4) | (attr) | 0x2u)

static uint32_t pc_mmu_section_attr(uint32_t base, uint32_t sram_attr)
{
  if(base == PC_MMU_SRAM_BASE)
    return sram_attr;

  //The restart record must reach DDR at once: it outlives this run (restart, power cycle) without a cache clean
  if(base == PC_MMU_RST_BASE)
    return PC_MMU_SO;

  if((base >= PC_MMU_DDR_BASE) && (base < (PC_MMU_DDR_BASE + PC_MMU_DDR_SIZE)))
    return PC_MMU_WB;

  //Peripherals, display engine, boot ROM, the eGON header page below DDR, DDR aliases: as with the MMU off
  return PC_MMU_SO;
}

int32_t pc_mmu_build(uint32_t *l1, uint32_t (*l2)[PC_MMU_L2_ENTRIES], uint32_t l2_addr, uint32_t fb_start,
                     uint32_t fb_len, uint32_t sram_attr)
{
  uint32_t i, s, p, first, last, s0, s1;

  for(i=0;i<PC_MMU_L1_ENTRIES;i++)
    l1[i] = PC_MMU_SECTION(i << 20, pc_mmu_section_attr(i << 20, sram_attr));

  if(fb_len == 0)
    return 0;

  //Every 4 KB page holding a byte of the framebuffer is write-through
  if((fb_start + (fb_len - 1)) < fb_start)
    return -1;

  first = fb_start & 0xFFFFF000u;
  last  = (fb_start + (fb_len - 1)) | 0x00000FFFu;
  s0    = first >> 20;
  s1    = last >> 20;

  if((s1 - s0 + 1) > PC_MMU_L2_TABLES)
    return -1;

  for(s=s0;s<=s1;s++)
  {
    uint32_t  sec_attr = l1[s] & 0xCu;
    uint32_t *t        = l2[s - s0];

    for(p=0;p<PC_MMU_L2_ENTRIES;p++)
    {
      uint32_t base = (s << 20) | (p << 12);

      t[p] = PC_MMU_SMALL(base, ((base >= first) && (base <= last)) ? PC_MMU_WT : sec_attr);
    }

    l1[s] = PC_MMU_COARSE(l2_addr + (s - s0) * (PC_MMU_L2_ENTRIES * 4));
  }

  return (int32_t)(s1 - s0 + 1);
}

uint32_t pc_mmu_walk(const uint32_t *l1, const uint32_t (*l2)[PC_MMU_L2_ENTRIES], uint32_t l2_addr, uint32_t addr)
{
  uint32_t d = l1[addr >> 20];
  uint32_t t, p;

  //Domain 0 and bit 4 for both descriptor types
  if(((d >> 5) & 0xFu) || !(d & (1u << 4)))
    return 0xFFFFFFFFu;

  switch(d & 3u)
  {
    case 2:
      if(((d & 0xFFF00000u) != (addr & 0xFFF00000u)) || (((d >> 10) & 3u) != 3u) || (d & 0x000FF200u))
        return 0xFFFFFFFFu;

      return d & 0xCu;

    case 1:
      t = (d & 0xFFFFFC00u) - l2_addr;

      if((t % (PC_MMU_L2_ENTRIES * 4)) || ((t / (PC_MMU_L2_ENTRIES * 4)) >= PC_MMU_L2_TABLES))
        return 0xFFFFFFFFu;

      p = l2[t / (PC_MMU_L2_ENTRIES * 4)][(addr >> 12) & 0xFFu];

      if(((p & 3u) != 2u) || ((p & 0xFFFFF000u) != (addr & 0xFFFFF000u)) || ((p & 0xFF0u) != 0xFF0u))
        return 0xFFFFFFFFu;

      return p & 0xCu;

    default:
      return 0xFFFFFFFFu;
  }
}

//----------------------------------------------------------------------------------------------------------------------------------
// Target only

#ifndef PC_HOST_TEST

#include "types.h"
#include "fnirsi_1013d_scope.h"

extern uint32 maindisplaybuffer[SCREEN_SIZE / 2];   //variables.c: the buffer DEBE layer 0 scans (sys_init_display)

static uint32_t pc_mmu_l1[PC_MMU_L1_ENTRIES] __attribute__((aligned(16384)));
static uint32_t pc_mmu_l2[PC_MMU_L2_TABLES][PC_MMU_L2_ENTRIES] __attribute__((aligned(1024)));

uint32_t pc_mmu_enable(void)
{
  uint32_t ctrl;

  //The MMU is off here, so these stores go straight to DDR, where the table walk reads them
  if(pc_mmu_build(pc_mmu_l1, pc_mmu_l2, (uint32_t)pc_mmu_l2, (uint32_t)maindisplaybuffer, sizeof(maindisplaybuffer),
                  PC_MMU_SRAM ? PC_MMU_WB : PC_MMU_SO) < 0)
  {
    __asm__ __volatile__ ("mrc p15, 0, %0, c1, c0, 0" : "=r" (ctrl));
    return ctrl;
  }

  __asm__ __volatile__ (
    "mov  r0, #0\n"
    "mcr  p15, 0, r0, c7, c10, 4\n"   //drain the write buffer
    "mcr  p15, 0, r0, c7, c7, 0\n"    //invalidate I- and D-cache (lines from before the FEL load or a restart)
    "mcr  p15, 0, r0, c8, c7, 0\n"    //invalidate the TLBs
    "mcr  p15, 0, %1, c2, c0, 0\n"    //translation table base
    "mov  r0, #1\n"
    "mcr  p15, 0, r0, c3, c0, 0\n"    //domain 0 client, the others no access
    "mrc  p15, 0, r0, c1, c0, 0\n"
    "bic  r0, r0, #0x6300\n"          //RR, V (low vectors), R, S off
    "bic  r0, r0, #0x0002\n"          //A off: no alignment faults, as with the MMU off
    "orr  r0, r0, #0x1000\n"          //I-cache
    "orr  r0, r0, #0x0005\n"          //MMU and D-cache
    "mcr  p15, 0, r0, c1, c0, 0\n"
    "mrc  p15, 0, %0, c1, c0, 0\n"    //read back: the CP15 write has taken effect
    "mov  r0, r0\n"
    "mov  r0, r0\n"
    : "=r" (ctrl)
    : "r" ((uint32_t)pc_mmu_l1)
    : "r0", "memory");

  return ctrl;
}

#endif
