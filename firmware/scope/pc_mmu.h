//----------------------------------------------------------------------------------------------------------------------------------
// MMU + D-cache (fw 0.2.48, docs/plans/mmu-dcache.md): flat 1:1 map, DDR and SRAM A write-back, the scanned framebuffer
// write-through, the restart record and everything else strongly ordered. The table builder and the walk are pure C
// (host_tests/test_mmu.c); pc_mmu_enable is target only.
//----------------------------------------------------------------------------------------------------------------------------------

#ifndef PC_MMU_H
#define PC_MMU_H

#include <stdint.h>

#ifndef PC_MMU_SRAM                   //1: SRAM A write-back, 0: strongly ordered (A/B for the FFT's SRAM plan)
#define PC_MMU_SRAM 1
#endif

//ARMv5 C and B bits as they sit in section and small-page descriptors
#define PC_MMU_SO        0x0u         //strongly ordered: non-cacheable, non-bufferable (what MMU-off gives)
#define PC_MMU_BUF       0x4u         //bufferable only
#define PC_MMU_WT        0x8u         //write-through, read-allocate
#define PC_MMU_WB        0xCu         //write-back, read-allocate

#define PC_MMU_L1_ENTRIES 4096u       //one per MB
#define PC_MMU_L2_ENTRIES 256u        //coarse table: 4 KB small pages
#define PC_MMU_L2_TABLES  2u          //framebuffer < 1 MB spans at most two sections

#define PC_MMU_SRAM_BASE  0x00000000u
#define PC_MMU_DDR_BASE   0x80000000u
#define PC_MMU_DDR_SIZE   0x02000000u
#define PC_MMU_RST_BASE   0x81A00000u //restart record section (pc_hal_scope.c PC_RST_ADDR)

//Fill l1 (4096 words, 16 KB aligned on the target) and up to PC_MMU_L2_TABLES coarse tables (256 words each, 1 KB
//aligned) for the framebuffer [fb_start, fb_start + fb_len). l2_addr = the address the MMU sees for l2[0] (flat map:
//the same as the pointer on the target). Returns the number of coarse tables used, or -1 if the framebuffer needs more
//than PC_MMU_L2_TABLES (the caller then must not enable the MMU).
int32_t  pc_mmu_build(uint32_t *l1, uint32_t (*l2)[PC_MMU_L2_ENTRIES], uint32_t l2_addr, uint32_t fb_start,
                      uint32_t fb_len, uint32_t sram_attr);

//Walk the tables for addr: returns the C/B attribute (PC_MMU_SO..PC_MMU_WB), or 0xFFFFFFFF for a fault descriptor or
//a mapping that is not flat. l2_addr as given to pc_mmu_build.
uint32_t pc_mmu_walk(const uint32_t *l1, const uint32_t (*l2)[PC_MMU_L2_ENTRIES], uint32_t l2_addr, uint32_t addr);

#ifndef PC_HOST_TEST
//Build the map for maindisplaybuffer and switch the MMU, D-cache and I-cache on. Called once from main, after the BSS
//clear and before interrupts are enabled. Returns the CP15 control word as read back.
uint32_t pc_mmu_enable(void);
#endif

#endif
