# Hardware status log (owner's FNIRSI 1013D)

Living document. Update after every hardware step (CLAUDE.md). Procedures are in `docs/hardware-prep.md`.
Hash entries use sha256 of files in `backups/` (gitignored).

## Unit identification

| Item | Value | Basis |
|---|---|---|
| Model | FNIRSI 1013D | owner |
| MCU | Allwinner F1C100s | board |
| FPGA | Anlogic AL3A10, unmarked LQFP144 | inferred: external 25Q80 beside it; not yet confirmed by a bitstream header or JTAG read |
| FPGA JTAG IDCODE | 0x18006C31 expected | taken from other AL3 units (CLAUDE.md); will appear at offset 40 of the 25Q80 dump |
| FPGA config flash | Winbond 25Q80DVNIG, 1 MB | owner |
| FPGA flash header | unpopulated 2x3, owner is soldering pins | owner |
| MCU flash | W25Q32JVSIQ, 4 MB, near the F1C100s | owner |
| Charger | TP4056 | owner |
| Parts on back side | none | CLAUDE.md |
| Stock FPGA version word | 0x1432 expected | not read on this unit |
| Firmware / bootloader now | unknown | owner to determine (hardware-prep.md section 4) |
| Programmers available | generic ARM JTAG debuggers (J-Link / ST-Link / CMSIS-DAP class); CH341A can be bought; WSL Ubuntu with flashrom | owner |
| Adapters on hand (per lead) | FT232RL (no MPSSE, unusable), Sipeed USB-JTAG/TTL RISC-V debugger (genuine FT2232D, verified by the lead: D2XX type 4, serials A/B, COM8/COM9, MPSSE sync OK on channel A), J-Link V9 clone (not recommended) | lead; hardware-prep.md 2.4 |
| Programmer chosen for SPI | primary: `firmware/tools/spiflash_d2xx.py` on the Sipeed adapter (Windows, D2XX). Alternatives: pyftdi script `spiflash_ftdi.py` (WSL), pico-serprog, CH341A with 3.3 V mod | hardware-prep.md 2.4 |
| Sipeed adapter type | genuine FT2232D | lead's D2XX check |

## Checklist

- [x] Header pins soldered
- [x] Pinout verified by continuity (hardware-prep.md 1.3 table filled and photographed)
- [x] Programmer chosen and its pin mapping to the header written down
- [ ] Programmer signal levels measured (3.3 V on CLK, MOSI, CS)
- [x] `spiflash_d2xx.py probe` (or flashrom probe) returns a valid JEDEC ID
- [x] 25Q80 backed up twice, dumps identical, sha256 recorded
- [x] 25Q80 dump compared with the stock reference (sha256 `90f65f084bdee8f443830ff6c48b7a90d0600fafa7e68713686542a29fa0d253`), result noted
- [ ] W25Q32 backed up, sha256 recorded, compared with reference dumps
- [ ] SD card imaged (full image plus sectors 0-2047 and 708, 709, 710), sha256 recorded
- [ ] Backups copied to a second drive
- [x] Current firmware version noted
- [x] Current bootloader version noted (or "none, stock")
- [x] FPGA version word noted
- [x] Lead's go received for Tier 1
- [x] Tier 1 SD card prepared with `make_sd_image.py` (bootloader v0.8, updater, scope; boot mode 4), written to a spare card
- [x] Tier 1 step 5.3.3 done: open firmware booted with stock FPGA
- [x] AL3 bitstream (0x1532) written and verified by read-back
- [x] FEL path tested (usbipd attach, `sunxi-fel version`)

## Backup register

| File | Size | sha256 | Date | Notes |
|---|---|---|---|---|
| | | | | |

## Log

Newest entries at the bottom. Format: date, who, what, result.

- 2026-10-18: Guide `docs/hardware-prep.md` and this log created. No hardware action taken. Reference hashes (computed on the `D:\src` copies): stock AL3 flash image `90f65f08...0d253`; Atlan AL3 `FPGA.bin` `0d4c829c...0fd2`; padded AL3 image `8a9d3559...2177`; on-scope backup tool `64bc81e6...1f6`.
- 2026-10-18: Guide updated after the firmware bootstrap. Bootloader v0.8, updater v0.02v and scope v1.00o5 exist in Atlan's v1.00o5 package (721,992 bytes), which `make package` in `firmware/scope` reproduces. SD cards are to be written with `firmware/tools/make_sd_image.py`, not with Atlan's `dd` line, which zeroes sectors 708 to 710. Added programmer options and `firmware/tools/spiflash_ftdi.py` (tested only against a simulated flash). No hardware action taken.
- 2026-10-18: Owner's Sipeed adapter confirmed as a genuine FT2232D. `spiflash_d2xx.py` (Windows, D2XX) is now the primary SPI route in hardware-prep.md 2.4; `spiflash_ftdi.py` (pyftdi) is the WSL alternative and is untested on hardware. No hardware action taken on the flash.

### 2026-10-18 (lead, with owner at the bench)
- Header J2 (FPGA flash) soldered. Measured pinout by continuity (owner numbering, pin 1 top-left):
  1 -> /CS (flash 1), 2 -> DO (flash 2, 34 ohm series), 3 -> CLK (flash 6), 4 -> DI (flash 5), 5 -> VCC (flash 3,7,8), 6 -> GND (flash 4).
  Differs from the schematic guess (candidate A) in physical order; electrically complete.
- Programmer: Sipeed FT2232D channel A via D2XX. Wiring: TMS->1 /CS, TDO->2 DO, TCK->3 CLK, TDI->4 DI, GND->6; header 5 and debugger 3V3 unconnected. Scope on battery, no USB.
- `spiflash_d2xx.py probe`: JEDEC ef 40 14, W25Q80DV, status 0x00 (no block protect). SPI clock 1 MHz, ~117 KiB/s reads.
- Backup: `backups/2026-10-18_fpga-25q80-stock.bin`, two reads identical, sha256 90f65f084bdee8f443830ff6c48b7a90d0600fafa7e68713686542a29fa0d253.
  Payload (282,866 bytes, header cc55aa33 f0000006 18006c31 0bb7c200) byte-identical to Atlan's zb25vq80 dump and pcprogrammer's W25Q80_FPGA.bin. Unit is confirmed stock AL3A10 bitstream (0x1432).
- Spare SD card: 480 MB USB mass-storage card, originally FAT32 from sector 32 (would collide with firmware sectors); repartitioned with diskpart, FAT32 from 4 MiB (sector 8192).
- Tier-1 SD card written (WSL /dev/sdg via usbipd busid 8-4): bootloader v0.8 @16, updater v0.02v @80, scope v1.00o5 (GCC 10 build, byte-identical to Atlan's reference) @750,
  standard display config @710 with boot mode 4 (menu, scope preselected); sectors 708/709 blank (calibrate on first boot). Image: `backups/sdcard/card-1.00o5-stockfpga.img`
  sha256 83a1ff773c766f1c04586996b6a22dd601828162e9806f573a912ab62442f012. Read-back identical. FAT32 partition from sector 8192, label FNIRSI.
  Not yet booted on the scope.
- First boot of the tier-1 card OK: BOOT fw v0.8 banner, boot menu, scope v1.00o5 runs with stock FPGA; diagnostics shows FPGA 0x1432.
  Standard display config correct (no shift, touch OK). Base calibration done; input-divider calibration done against bench supply + DMM
  (first attempt failed with probes on 10x: the routine needs 1x). Updater's USB mass-storage mode gave "Cannot recognize device" on Windows (unresolved, cable suspected).
- FEL verified: boot menu -> FEL, usbipd bind --hardware-id 1f3a:efe8, attach busid 9-3 to WSL, `sunxi-fel version` -> AWUSBFEX soc=00001663(F1C100s) ver=0001.
- FEL RAM load proven: `sunxi-fel -p write 0x7FFFFFE0 scope.bin exe 0x80000000` (346 kB in 1.7 s) started the scope UI from RAM.
- FPGA flash written with Atlan's AL3 bitstream (0x1532): `backups/al3_atlan_0x1532_padded.bin` sha256 8a9d3559566fbe9653ad52612818d336ce7f905498f422f228bb165335c42177;
  pre-write backup `backups/20261018-131536-pre-write.bin` (= stock, sha256 90f65f08...); 68 sectors erased, 1080 pages programmed, read-back verified. Board powered from battery during write.
  Power-cycled: diagnostics screen shows FPGA 0x1532. Tier 1 complete.
- Protocol firmware bring-up (image build-gcc10-pc/fnirsi_1013d.bin sha256 da53cc74..., GCC 10, PC_PROTOCOL=1, loaded over FEL):
  step 2 enumeration OK (Windows COM10, USB\VID_1209&PID_0001A3D-0001, usbser, no driver install);
  step 3 text probe OK: `#VER` -> `#OK,FNIRSI-1013D-AI,fw=0.1.0,proto=1.0,fpga=0x1532`, `#FOO` -> `#ERR,UNKNOWN`.
  step 4 identify (LOCAL) OK: GET_INFO variant 2, ring 8192, max_total 16382, caps 0x1803, read_lag 0 (unverified), fw 0.1.0;
  PING x500 random 0..492 B: 0 mismatches, 52.8 ms avg round trip (slow: main-loop poll cadence suspected);
  GET_STATUS/BATTERY/CHANNEL/CAL/TIMEBASE/TRIGGER/GEN all answer; CH1 adc comp -2/+2, CH2 0/0; input_calibration values from the bench calibration read back.
  step 5 T1 OK (492/495/496/1008/4076 B x100, 0 failures; 495/496 B cost 90-140 ms each, others ~40 ms);
  T2 FAILED: 65536 B response -> Windows error 31 (device not functioning), device re-enumerated, stale pattern bytes drained afterwards;
  threshold: <=5120 B payload ok (33 ms), >=6144 B -> 40 s timeout then latched E_BUSY/ui_busy=1 (main loop alive, capture_id advancing). Handed to fw-usb agent.
  PING round trip 33-53 ms = one UI main-loop pass per request; too slow for readouts, raised with the agent.
  Re-test with scope untouched: 4096/6144/8192/16384 B OK (~60 ms each); 40..65-packet responses (20460..33260 B) never start (0 bytes in 15 s),
  a subsequent PING releases 4096 B once; 66 packets OK after 5 s, 70 packets OK in 66 ms. Earlier 6144 B failures were caused by the owner using menus
  (modal loops defer non-safe commands: expected). Firmware agent instrumenting the TX pump.
  step 6 REMOTE OK: SET_REMOTE -> control=1, RELEASE -> control=0 exit_reason=1, UI trigger settings identical before/after;
  step 7 readout OK: ARM/WAIT -> acq_state 3 (DONE) in ~15 ms auto mode, trigger_addr ~757-836, READ_RAW all 4 cores x3 byte-identical;
  CH2 idle: mean 127.5-127.9, std 0.36-0.50 LSB; CH1 (probe attached): cores A/B means 134.6/129.1 (A/B offset mismatch visible), std ~9.
  MCP server + simulator delivered (mcp/, 127 tests); client used for these bench steps.
  step 8 front end OK: SET_CHANNEL all 7 ranges x 2 couplings x 2 channels read back correctly (range 6 -> hw_range 5 software zoom), relays audible;
  SET_TIMEBASE raw dividers 0..19999 (200 MS/s..10 kS/s) OK; ui_index roll timebases (<=10) correctly E_UNSUPPORTED; ui_index 14/18/22/26 OK;
  SET_TRIGGER geometry 80..16382 samples OK; full-depth capture (16382 samples x 4 cores, 16 KiB chunks) arm+wait+read in 225 ms;
  UI settings restored after release and after an aborted session (exit_reason 1).
  Linux re-test (scope attached to WSL via usbipd, /dev/ttyACM0): ALL sizes 16384..65536 B arrive complete; 65536 x20 0 fails median 70 ms (0.93 MB/s),
  32768 x20 0 fails median 41 ms, 6000 x50 1 fail (sporadic). => TX stall is a Windows host-side (usbser/pyserial) issue; handed to the MCP agent.
  usbipd bind 1209:0001 done by owner; attach/detach between Windows COM10 and WSL works.
  step 9 generator OK (CH1 1x on the calibration output, REMOTE, range 3 DC, normal trigger rising, 16000 samples): requested/measured
  1 kHz 50% -> 1000.0 Hz 50.0%; 5 kHz 50% -> 5000.0 Hz 50.0%; 10 kHz 25% -> 10000.0 Hz 26.0%; 2 kHz 75% -> 2000.0 Hz 74.9%; 100 Hz 50% -> 100.0 Hz 50.0%;
  swing low -1.203 V / high 1.901 V (3.1 Vpp, no clipping; absolute offset per T8 unverified). 20 kHz/100 kHz readouts hit the Windows transport stall (16 KiB chunks).
  Owner confirms the red REMOTE banner is shown. Also seen: one E_BAD_CRC on a WAIT request right after another host session had used the port (stale RX bytes).
  Bring-up checklist: steps 1-9 all exercised; remaining bench items are T3/T4/T5/T7/T8 (PC_DEBUG build) and the Windows transport fix.
- FPGA toolchain: Anlogic TD 4.6.8 SP1 116866 (Linux build, owner-downloaded) installed headless in WSL (~/anlogic/, licence to 2027-09-30);
  `fpga/al3/build.sh` rebuilds Atlan's AL3 project byte-identical to the flashed 0x1532 bitstream (sha256 0d4c829c...) in 11-16 s, deterministic;
  timing of the shipped design: clk_200MHz 223.0 MHz Fmax, TNS 0 (the 160 MHz figure in the research notes came from a stale report).
- Firmware 0.1.1 (commit 5a8432b): USB FIFO layout fixed (2 KiB FIFO RAM; EP2 TX was aliased at byte 4096), TX pump fallback, recovery (#RESET, DTR flush),
  GET_STATUS pass_ms, ENTER_FEL. Image build-gcc10-pc/fnirsi_1013d.bin sha256 4aa16e0b... awaiting FEL load.
- Windows transport root cause (mcp commit c265992): pyserial timeout changes before each read made usbser.sys send CDC SET_LINE_CODING control
  requests; firmware 0.1.0 wrote the 8 setup bytes into the bulk IN payload at 1 KiB boundaries (CRC failures), and a large pending read sometimes
  never completed. Host fixed (settings set once, in_waiting-driven reads). On 0.1.0 + fixed host: 65536 B x50 0 fails median 65 ms on Windows.
  Residual ~1% request-side glitches (E_BAD_CRC / no response) absorbed by retries; to re-check on 0.1.2.
- Firmware 0.1.2 (commits 5a8432b, afaee38, 83ddff4; image sha256 5b0fe314...) FEL-loaded and benched with the fixed MCP transport:
  LOCAL: PING median 1.00 ms (was 45); TEST_PATTERN all sizes to 65536 OK; 1.01 MB/s; pass_ms 35-59, pass_max_ms 1515 once after boot, later 74.
  REMOTE: PING median 0.19 ms; 64 KiB responses 2.35 MB/s (28 ms); full-depth 16382-sample dual-channel capture incl. readout 110 ms median (x10).
  Residual: ~2% of requests get no response on first attempt (19 retries / 1023 frames, no CRC/garbage); handed to fw-usb.
- ENTER_FEL verified: client enter_fel() -> scope re-enumerates as 1f3a:efe8 within ~4 s -> usbipd attach -> sunxi-fel version OK -> image reloaded
  -> COM10 back. Hands-free dev loop established (no boot menu needed while a protocol firmware is running).
- Firmware 0.1.3 (commit d0ae338, EP0 FIFO overlap fix; sha256 b743e149...) loaded via ENTER_FEL. Census with client retries=0:
  LOCAL PING x1000: 42 E_BAD_CRC (request corrupted on arrival) + 4 no response; REMOTE PING x1000: 40 + 5; REMOTE 64 KiB x100: 8 no response.
  0.1.2 had 0 CRC errors but ~2% silent drops. Responses are clean (0 bad payload CRC host-side). RX path (two polling contexts) suspected; fw-usb investigating.
  Owner request: persist firmware to the SD card over the protocol (SD_WRITE_SECTORS) so updates need no card handling; assigned to fw-usb.
- Firmware 0.1.4 (commit aabe64a, sha256 1dcc5374..., pinned copy in backups/): census retries=0 LOCAL PING x2000: 83 E_BAD_CRC + 9 no response;
  REMOTE x2000: 72 + 14; 64 KiB x100: LOCAL 0 errors, REMOTE 8 no response. GET_LINK_STATS: rx_packets 4440 ~= frames 4258 + hdr_crc 23 + payload_crc 155,
  rx_swept 0 (lost-IRQ theory not confirmed), host garbage_bytes 25212. Hypothesis sent to fw-usb: MUSB FIFO addresses must be size-aligned
  (0.1.3 map puts EP2 OUT at 64 and IN at 576); proposed EP1@64, OUT@512, IN@1024 double.
- Firmware 0.1.5 map 0 (commit c31bd01, sha256 43aee1b6..., loaded with fnirsi1013d-flash after one lost ENTER_FEL): census 2000 PINGs -> 77 E_BAD_CRC + 9 no response,
  100 x 64 KiB -> 1 fail. GET_BAD_FRAME diffs: each bad request is intact up to the byte where cumulative rx_bytes crosses a multiple of 2048, then the
  rest is a repeated constant or 2-byte pattern (stale reads) => RX ring (2 KiB) wrap bug, independent of the FIFO map. Agents rotated: fw-gen2 fixing it.
  All first-generation agents retired with AGENTS.md handoffs (firmware/, mcp/, fpga/al3/).
- Firmware 0.1.6 (commit 697e1f6, sha256 387491f1..., pinned in backups/): root cause = pc_rx_drain_fifo split a ring-straddling packet into two FIFO reads
  with mixed widths (forbidden by the MUSB guide); now one read per packet into an aligned buffer. Census 2000 PINGs + 100 x 64 KiB, retries off:
  0 E_BAD_CRC, 0 no response, 0 failed; device counters hdr_crc 0 / payload_crc 0. PING 1.00 ms median. Request-loss bug CLOSED.
  Note: ~1 s after a FEL load the main loop is not yet running (ui_busy=1, pass_ms 0); first main-loop command waited 5 s.
  fnirsi1013d-flash validated end to end (6.6 s hands-free reload).
- Firmware 0.1.7 (commit 5de512e, sha256 fc3984b3..., ring refactor + exhaustive host test): full bench 0 failures, census 0/0/0, counters clean.
  Current RAM image on the unit. Card still holds Atlan v1.00o5 (promotion pending owner's choice: MSC-mode write now or SD commands later).
- Firmware 0.1.7 PROMOTED to the SD card (sector 750..1490) via the scope's USB mass-storage mode (0483:5720, bound for usbipd, /dev/sdg in WSL)
  with make_sd_image.py write --scope; sectors 0..749 and the tail verified unchanged (calibration 708 / settings 709 / boot byte 710 intact);
  pre-write backup backups/sdcard/sd_sectors_0-1490_20261018-144808.bin. Awaiting power-cycle confirmation (#VER 0.1.7 from the card).
