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
  pre-write backup backups/sdcard/sd_sectors_0-1490_20261018-144808.bin. Power-cycle confirmed: boots from the card to fw 0.1.7, calibration and settings intact (input_calibration values unchanged).
- T8 volts calibration data (fw 0.1.7, CH1 1x DC, bench supply + DMM): backups/t8_CH1_1.000V.json, t8_CH1_0.010V.json, t8off_CH1_0.000V.json.
  Findings: gains match uv_per_lsb_q8 within 1%; zero model zero_core = Z_core - 0.265*dcoffset, range-independent; on this unit Z_A=132.5, Z_B=127.0
  (A/B spread 5.5 codes vs stored comps -2/+2; centre 129.75 vs assumed 128). Host per-unit calibration being built by mcp-gen2.
  Open: generator loop-back read ~75 codes low at range 3 (coupling encoding or trace position?), to be checked with a controlled square-wave test.
- Square-wave check (backups/t8sq_CH1.json): coupling 0 = DC, 1 = AC confirmed; in DC the calibration output's low level sits ~70 codes (~1.2 V)
  below the shorted-input level, and the scope's own screen agrees (owner: zero marker ~1.1 V above the wave's bottom) => property of the
  calibration output circuit, not of the measurement chain. The output is a timing reference (3.1 Vpp), not a voltage reference.
- Firmware 0.2.1 (commit d9e1ac6, sha256 ca0b6d11..., protocol 1.3 collaborative mode) loaded hands-free; caps 0x3c03; startup counters:
  3 ms to first poll, first UI pass 1516 ms. Protocol-level acceptance OK (host SET_* applied + read back, host single capture -> STOP,
  host RUN, READ_TRACE in RUN origin ui_run, settings_seq increments). Visual checks (MCP chip, redraws) pending owner's report.
  Visual/interactive acceptance on 0.2.1 (owner at the screen): MCP chip visible and pulsing (but placed below the battery indicator, not at the
  RUN chip -> fix requested); host SET_CHANNEL redraws cleanly with relay click; host SET_TIMEBASE flickers through intermediate values (fix requested);
  host single capture shows STOP, host RUN resumes; touch-screen time/div and V/div changes seen by the host within 0.25 s (settings_seq increments,
  ui_busy=1 while a menu is open, main-loop reads refused with E_BUSY meanwhile, as designed).
- Host volts calibration installed for serial 1013D-0001 (mcp/data/cal/1013D-0001.json, 45 points, residual <= 0.14 codes); new scope_calibration_info tool.
  CH2 still on the firmware formula until a shorted-input run. Owner's MCP server must be restarted to load the file.
- Calibration-output mystery closed: DMM DC reading of the calibration pin vs probe ground = 0.310 V, matching the mean of the scope's -1.2/+1.9 V
  square (0.35 V). The output is capacitively coupled with a small positive bias on this hardware; the scope's volts are correct.
- CH2 zero sweep (backups/t8off_CH2_0.000V.json, fw 0.2.1): at dcoffset 0 A 128.4 / B 128.9 (cores within 0.5 codes), slope -0.265; CH2 is close to the
  firmware model, CH1 is the outlier (A/B spread 5.5). Sent to mcp-gen2 for the calibration file.
- Firmware 0.2.2 (commit 0d4d9bb, sha256 0f74b49c..., SD sector commands, caps 0x7C03) loaded hands-free. Startup counters: first poll 2 ms,
  first pass 1515 ms, first conversion +30 ms, first capture processed ~0 ms -> the 1.5 s is in draw/touch/RTC init of the first pass.
  SD commands not yet benched (host CLI in progress).
- USB incident on 0.2.2: after ~10 min idle with the host port closed, the device stayed enumerated but in a Windows Error state (CreateFile failed);
  scope UI normal; cable replug recovered it. Reported to fw-gen2 (suspend/resume or DTR-down path suspected).
- Firmware 0.2.3 (commit baee5b5, sha256 bbcb1d7b...): MCP chip above RUN/STOP (replaces the RTC clock slot while shown), steady, SET_TIMEBASE
  single-step apply. Loaded hands-free; visual verdict pending. Owner note: generator menu SINE/TRGL options have no effect on the 1013D (no DAC) -> hide.
- Owner's verdict on 0.2.3: SET_TIMEBASE clean; MCP chip placed right and gone on disconnect, but blinks off momentarily on top-bar redraws (fix requested).
- SD persist bench (0.2.3): SD_INFO VERIFY OK; SD_READ 757 sectors 0.53 s; first `flash --persist-only` FAILED at 262144/387584 B (host WriteFile error 22,
  device dropped off USB and re-enumerated; card left with a partial image at 750), second run succeeded: 34 s (11 KiB/s), SD_READ compare identical,
  header verified; card now holds 0.2.3. Counters showed hdr_crc 660 / duplicates 658 under rx_stalls 307837 => request corruption under RX
  back-pressure + SD_STAGE bound to UI passes (~5.8 s per 64 KiB). Both with fw-gen2.
- Firmware 0.2.4 (commit b05cdbd, sha256 d99c6af7...) loaded; GET_LINK_STATS carries a leading count word (35) then the counters: init->first main-loop
  poll = 1515 ms, first UI pass = 31 ms (earlier readings were off by one). The startup delay is in the init path before the main loop, not in the UI pass.
- Firmware 0.2.5 (commit 5086f7d, sha256 faefbdb3...): EP2 OUT interrupt re-enabled after suspend/reset/disconnect; USB event counters 35..40.
  Loaded hands-free; baseline resets 1 / suspends 1 / resumes 0 / disconnects 0 / EP0 setups 40. Idle-fault reproduction (11 min port closed): no fault, reopen OK, no suspend/reset/disconnect events counted; not reproduced on 0.2.5.
- Firmware 0.2.6 (commit 68f7868, sha256 ef77935c..., pinned backups/fw-0.2.6-ef77935c.bin): unsupported menu items hidden (generator
  Wave/Ampl/Offset/Phase, Long memory, EXT trigger, Hold off, Time & Date when no DS3231 answers; depth clamped per bitstream). Loaded hands-free
  (0.2.5 -> FEL -> 0.2.6 in 6.5 s). Census 2000 PINGs 0/0, 100 x 64 KiB 0 failed, PING median 1.00 ms, LOCAL throughput 0.99 MB/s (unchanged).
  Startup counters: 1517 ms init -> first poll, first pass 31 ms, v29..v34 = 1/49/1/31/32/32. USB events at boot: 1 reset, 1 suspend, 0 resumes,
  0 disconnects, 39 EP0 setups. Card still holds 0.2.3. Visual checks of the hidden items pending the owner.
- Owner's 0.2.6 verdict: items hidden, but painting over leaves gaps (generator: empty row between Power and Freq and empty rows below; trigger: one
  empty row; system settings: empty bottom row) and the "ACQ / Long mode" box in the acquisition menu is still drawn. Compaction requested (fw 0.2.8).
- Firmware 0.2.7 (commit d88e9ea, sha256 e1f04b43..., pinned backups/fw-0.2.7-e1f04b43.bin): SD_STAGE from the 1 ms timer, RX stall counters 41-46,
  MCP chip redrawn by the UI's top-bar routine, version on screen (corner + diagnostics identity line), temperature only with a DS3231. Loaded
  hands-free (first ENTER_FEL attempt refused with E_BUSY while the owner was in a menu, as designed). Census 2000 PINGs 0/0, PING median 0.36 ms
  (fast path); 64 KiB transfers refused only while a menu is open. Startup 1.5 s gap attributed to the splash BMP load; kept (coordinator decision).
  Card still holds 0.2.3; persist and the 47-counter readout pending the owner leaving the menus.
- Owner's 0.2.7 verdict: "v0.2.7" in the main-screen corner and on the diagnostics view, no Temperature line. MCP chip check in progress (port held
  open by the coordinator while the owner opens menus).
- Repository published: https://github.com/mikljohansson/fnirsi-1013d-mcp (public, 2026-10-18, owner's decision); first CI run triggered by the push.
- Persist of 0.2.7 to the card (fw 0.2.7 SD_STAGE on the timer poll, host 32-sector SD_WRITE, 99eb034): backup of 750..end, write 3.45 s (111 KiB/s,
  no resends), SD_READ compare of 763 sectors identical, header verified; total 5.3 s (was 34 s with a mid-run USB drop on 0.2.3). Counters after
  it: rx_stalls 0, hdr_crc 0, duplicates 0, rx_isr_stalls/releases/restalls/empty_releases 0, rx_count_max 512, pass_max_ms 832 during the write.
  The back-pressure corruption regime no longer occurs on this path. **Card now boots 0.2.7.**
- MCP chip on 0.2.7 (owner): visible, stable while a menu is open, still flickers occasionally while free-running -> periodic top-bar clear
  suspected; with fw-gen2 for 0.2.8 together with the menu compaction and the "ACQ / Long mode" box.
- FPGA external trigger bitstream 0x1533 built (fpga-gen2, commits e45f411..5bbdda5): sha256 a9656703... (282,898 B), pinned
  backups/al3_ext-trigger_0x1533_a9656703.bin. Timing met (224 MHz), 38 testbench checks pass, CH1/CH2 behaviour identical to 0x1532 in
  co-simulation. **Not flashed**: firmware must accept version 0x1533 first (fw-gen2 has the spec); flashing needs the programmer, the stock
  backup at hand and the owner present. Open question before relying on it: the circuit between the FPGA pad and the calibration connector
  (the output looks capacitively coupled: mean 0.31 V for a 3.3 V square).
- Firmware 0.2.8 (commit 0ff64c6, sha256 a9c7f446..., pinned) loaded: generator menu compacted to Power/Freq/Duty (393 -> 169 px), trigger menu to
  5 rows (336 -> 280 px). Census 1000 PINGs 0/0, 100 x 64 KiB 0 failed. "ACQ / Long mode" box is the roll-mode toggle (a real control), kept;
  relabel as a state toggle requested.
- Firmware 0.2.9 (commit bebf901, sha256 a9864b08..., pinned backups/fw-0.2.9-a9864b08.bin, protocol 1.4): EXT trigger support for FPGA 0x1533
  (AL3 family 0x1532..0x153F, caps EXT_TRIGGER, SET_TRIGGER source 2, generator interlock, GET_STATUS byte 42 EXT flags). Loaded on the current
  0x1532: #VER proto=1.4, caps 0x7C03 unchanged, census 1000 PINGs 0/0, 100 x 64 KiB 0 failed, 0.99 MB/s. Observation on 0.2.8 and 0.2.9: the
  first TEST_PATTERN (492 B) right after a FEL load timed out once (5 s) and was fine on every rerun; not seen on 0.2.6/0.2.7; watching.
  Owner's menu checks on the compacted layout pending. Card holds 0.2.7.
- Firmware 0.2.10 (commit 6d4adfe, sha256 9d332251..., pinned backups/fw-0.2.10-9d332251.bin): MCP chip rendered once and blitted idempotently
  (the 0.2.7 flicker was the refresh's own clear-then-draw every 100 ms), "Roll mode on/off" label on the acquisition-menu toggle, back-pressure
  issue parked in firmware/AGENTS.md. Loaded hands-free; census 1000 PINGs 0/0, 100 x 64 KiB 0 failed, 1.00 MB/s; first TEST_PATTERN after boot
  fine this time (26 ms). Owner's chip/menu checks pending; card holds 0.2.7.
- Firmware 0.2.11 (commit 1594918, sha256 af4b7f30..., pinned backups/fw-0.2.11-af4b7f30.bin): fixes the first-request-after-open drop (DTR-up
  handled in the main-loop poll reset the parser and discarded a pending main-loop request already parsed by the timer poll; present since 0.1.1,
  no counter showed it). Includes 0.2.10. Loaded hands-free; census 1000 PINGs 0/0, 100 x 64 KiB 0 failed, 1.00 MB/s. Open -> TEST_PATTERN as
  the very first request -> close, 20x: 0 failures on 0.2.11 (also 0/20 on 0.2.10 with this host transport, whose open sequence avoids the race;
  bench_link's minimal link hit it). Owner's visual checks (chip, Roll mode box, compacted menus) pending; card holds 0.2.7.
- USB drop on 0.2.11 during an active 10 Hz GET_STATUS poll (host held the port open for the chip check, 16:5x): host WriteFile failed with
  Windows error 22 (ERROR_BAD_COMMAND, same as the 0.2.3 persist drop); the device re-enumerated by itself (COM10 back, firmware still 0.2.11, no
  reboot). Device counters afterwards: usb_bus_resets 2, usb_suspends 2 (both 1 at boot, i.e. one host-initiated re-enumeration), usb_disconnects 0,
  link_resets 4, ep0_setups 308, rx_stalls 0, hdr_crc 0, resp_dropped 0. Third occurrence of this fault class (0.2.2 idle, 0.2.3 persist, 0.2.11
  poll); not tied to traffic pattern. Needs a soak tool with timestamps and a host-side USB trace.
- Owner's verdict on 0.2.11: MCP chip stable while free-running (fixed), generator/trigger menus and the Roll mode box good; System settings
  bottom row still empty (compaction queued for 0.2.12). 0.2.11 persisted to the card: 5.7 s total, no resends, read-back identical, header
  verified. **Card now boots 0.2.11.** Owner turned Windows USB selective suspend off (powercfg, was on for AC and DC) for the next soak.
- Calibration-pin coupling test (fw 0.2.11, FPGA 0x1532, CH1 at 1x on the post, DC, range_index 1; backups/calcap_CH1*.json): generator at
  10 Hz, 1 Hz and 0.1 Hz. At 0.1 Hz (9 s window, 333 S/s) both levels hold for >4 s with 0.002 V droop: low -1.09 V, high +2.01 V, swing 3.10 V,
  noise 0.05 V rms. => No series coupling capacitor (any C < ~1000 uF would droop visibly); the output is DC-coupled but sits ~1.1 V below a
  0/3.1 V logic swing relative to the probe ground. Candidates: the probe/BNC ground is ~1.1 V above the FPGA's digital ground, or a resistive
  level shift to a negative rail. Owner DMM tests requested: (1) BNC shell vs USB shell DC with USB unplugged, (2) post vs BNC shell with the
  generator at 0.5 Hz. Matters for EXT: a 3.3 V logic input referenced to the probe ground might sit 1.1 V higher at the pad.
- Firmware 0.2.12 (commit e797f25, sha256 c9270dd3..., pinned backups/fw-0.2.12-c9270dd3.bin, protocol 1.5): USB CDC handler also run from
  the 1 ms tick (lost-interrupt fallback, prime suspect for the host-side port resets: an unanswered EP0 control transfer), counter 47
  usb_irq_polled, GET_EVENT_LOG 0x0009 (last 64 USB/link events). Loaded in RAM only. First bench after boot: one 64 KiB response lost
  (0.20 MB/s run, tx_aborts 1, first ever), two reruns clean at 1.00 MB/s; usb_irq_polled 46033 in ~3 min (the tick races the real ISR, so the
  counter does not isolate lost interrupts). Both with fw-gen2. 20-minute 10 Hz GET_STATUS soak started (USB selective suspend now off).
  Card keeps 0.2.11.
- Offset explained (fpga-gen2, schematic trace, commit 28126aa): the BNC shells are not at digital ground. They sit on VRF1 = TL432 reference
  1.24 V (via R71 100 Ohm; fed from 3V3 through 200 Ohm + 1 Ohm), whose anode is the common GND of FPGA, USB and SD. The cal path is FPGA pin 133 ->
  R21 1 kOhm -> post, DC. Predicted -1.24/+1.9 V vs measured -1.09/+2.01 V (scope DC error ~0.15 V, inference). EXT consequences: a 3.3 V source
  referenced to USB/digital ground works on levels and edges; a source referenced to the probe ground needs a series red LED (or 3x 1N4148),
  rising edge, else its low sits at ~1.24 V between VIL and VIH. General measurement caveat (new): tying probe ground to USB ground (earthed DUT
  + earthed PC) pulls ~12 mA through R71, sags VRF1 to ~1.1 V and shifts every trace -> goes into docs/agent-measurement-guide.md.
  Owner's DMM checks (optional confirmation): BNC shell to USB shell = +1.2..1.25 V; post to shell at 0.5 Hz alternates ~-1.24 / +1.9..2.0 V.
- Soak on 0.2.12 (10 Hz GET_STATUS, 17:08-17:12, selective suspend off): two drops, 17:11:46 and 17:11:53 (Windows error 22; port back in 2.1 s
  and 1.1 s; firmware kept running). Counters: usb_bus_resets 1->4->5, usb_suspends 1->4->5, link_resets 2->10, ep0_setups 300->377. The
  GET_EVENT_LOG ring was flooded with irq_polled entries (INTUSB SOF flag every ms, ~1000 events/s), so nothing from before the resets survived.
- Owner's DMM: 1.46 V between a second cable's USB ground on the same hub and a BNC shell with the scope's USB connected (predicted 1.24 V VRF1;
  the extra ~0.2 V is probably the IR drop in the scope's cable ground from the charging current, inference).
- Firmware 0.2.13 (7ebda04, sha256 691d0d28..., pinned): System settings compacted without DS3231, battery level averaged with hysteresis. Not
  benched separately. Firmware 0.2.14 (d6a6b37, sha256 aa4a142c..., pinned): IRQ fallback gated on quiet ticks, EXT ground note. Loaded: census
  1000 PINGs 0/0, 0.99 MB/s, but 1 of 100 64 KiB transfers lost again (second build with the tick-run USB handler; 0 in ~800 on 0.2.6-0.2.11);
  usb_irq_polled 11798 in ~2 min (SOF flag defeats the gate). Both back with fw-gen2 for 0.2.15. Card keeps 0.2.11.
- Owner's DMM on the calibration post vs BNC shell, generator at 0.047 Hz: +1.87 V / -1.23 V. Matches the VRF1 prediction (-1.24/+1.9 V) and
  confirms the DC-coupled, probe-ground-offset picture. The scope read the same signal as -1.09/+2.01 V at range_index 1, i.e. ~0.14 V high
  (inference: scope DC error at that range; worth a look when firmware calibration is done, roadmap item 5).
- Firmware 0.2.15 (c4989e5, sha256 b0cec01f...) superseded unbenched by 0.2.16 (079f648, sha256 5f25b9e7..., pinned): tick path runs the CDC
  handler body only for non-SOF flags; event ring 128 + frozen pre-reset snapshot. Loaded: 300 x 64 KiB in three censuses 0 failed, tx_aborts 0
  (the tick-run-handler regression of 0.2.12/0.2.14 is gone). BUT: three bench runs died right after #VER, and GET_STATUS uptime_ms later read
  175 s while the image had run 7+ min with #VER still 0.2.16 => the scope restarted itself from RAM (CPU exception -> reset vector suspected),
  re-enumerated (usb_bus_resets 12 / suspends 10 are the enumeration retries, not drops; event log shows suspend/reset pairs at 8.3-12.2 s after
  the restart). 0.2.16 crashes intermittently under traffic. Reported to fw-gen2 with a request for a reboot-reason record. Reloaded 0.2.11
  (card version) for the 30-minute baseline soak. Event log on 0.2.16 otherwise showed 16 irq_polled hits, all INTRX EP2 with SOF-only INTUSB.
- Owner (before leaving for a few hours): battery indicator stable now on 0.2.11 (battery likely full; the hysteresis fix is in 0.2.13+ and
  still to be checked while charging from a lower level); System settings empty row still present on 0.2.11 as expected (fixed in 0.2.13+).
  Agent rotation: fw-gen2 retiring at ~940k tokens with a firmware/AGENTS.md handoff; fw-gen3 takes the 0.2.16+ self-restart.
- Baseline soak on 0.2.11 (fnirsi1013d-soak, status mode 10 Hz, 17:25-17:55, USB selective suspend off on AC): 17,890 requests, 0 failures,
  0 drops, latency median 1.1 ms / p99 2.1 ms / max 3.1 ms, every counter delta none (no bus reset, no suspend, no restart). The earlier
  0.2.11 drop (16:5x) happened with selective suspend still on; the 0.2.12/0.2.16 "drops" were self-restarts. fw-gen2 retired (handoff
  0e19102), fpga-gen2 retired (d30cff8), mcp-gen2 retired (7de3a41); fw-gen3 active with the 0.2.19 / 0.2.19-nofb A/B pair (02288b8).
- Firmware 0.2.19 A/B pair (fw-gen3, commit 02288b8): backups/fw-0.2.19-c30ff516.bin (with tick fallback) and backups/fw-0.2.19-nofb-3d1f3c88.bin
  (PC_USB_TICK_FALLBACK=0), ELFs pinned alongside. 0.2.19 (c30ff516) loaded: 8 bench passes = 800 x 64 KiB, 0 failed, uptime monotonic,
  restarts 0, usb_irq_polled 54 in 90 s (lost interrupts are frequent under traffic and all recovered). The 0.2.16 restart did not reproduce
  in 8 passes; 30-minute capture-mode soak (4 KiB every 50 ms) started on 0.2.19 at 17:58 to catch one with the restart record.
- Correction: the "0.2.12 also restarted" inference is withdrawn (the 0.2.12 load at ~17:06 fits the log clock without a restart). Exactly one
  confirmed self-restart: 0.2.16, ~17:25:35, within 4 minutes of its FEL load, under bench traffic. fw-gen3's reading of counter 47: most
  "lost interrupt" hits are probably the tick racing the real USB ISR after an idle gap (only EP2 RX ever shows); the dispatcher's write-0
  to INTC pending is unverified as a loss mechanism. 0.2.20 (endpoint-state fallback without flag clearing + race-avoided counter) in work.
- 0.2.19 (c30ff516) capture soak 17:58-18:28 (4 KiB TEST_PATTERN every 50 ms): 35,579 requests, 0 failures, 0 drops, 0 restarts; latency median
  22.9 ms, p99 50 ms. Only usb_irq_polled moved (+2011, 1.1/s); type-14 entries: 128 EP2 TX, 10 EP2 RX, 1 EP0 (SOF-only INTUSB).
- Firmware 0.2.20 (fw-gen3, commit 7f1c0c0, sha256 e1517803..., pinned with ELF; nofb variant c7210b74...): tick fallback acts on endpoint state
  seen on two consecutive ticks and never when the USB IRQ is pending at the INTC; counter 57 usb_irq_races. Loaded: 8 bench passes = 800 x
  64 KiB, 0 failed, no restart; usb_irq_polled 0, usb_irq_races 59 in ~90 s. => The 1.00o5 dispatcher does not lose interrupts; every
  0.2.12-0.2.19 "lost interrupt" was the tick racing the real ISR, and 0.2.16's tick body racing on live traffic is the likely restart cause
  (inference; no restart record captured). 30-minute capture soak on 0.2.20 started 18:31.
- 0.2.20 (e1517803) capture soak 18:30-19:00 (4 KiB every 50 ms): 35,528 requests, 0 failures, 0 drops, 0 restarts, latency median 22.9 ms /
  p99 50.1 ms, counters delta none (usb_irq_polled stayed 0 for the whole soak). Together with the 8 bench passes: 0.2.20 is clean.
  **0.2.20 persisted to the card** (backup of 750.. taken by the tool first; restore with `fnirsi1013d-flash --restore <backup>` if the owner's
  visual checks of the 0.2.13+ UI changes fail). Pending owner checks on 0.2.20: System settings menu without the empty bottom row, battery
  indicator steady while charging, no boot message, generator/trigger menus and chip unchanged. fw-gen3 retired (handoff 47b2387); no agent active.
- **FPGA flash written with the external-trigger bitstream 0x1533** (19:16-19:17): probe JEDEC ef 40 14; pre-write dump
  backups/20261018-191620-pre-ext-trigger.bin and the tool's own backups/20261018-191640-pre-write.bin both sha256 8a9d3559... (= Atlan
  0x1532 padded, so the rollback image is confirmed); wrote backups/al3_ext-trigger_0x1533_padded_96159a87.bin (0xFF-padded 1 MiB of
  fpga/al3/out/FPGA-ext-trigger.bin a9656703...), 68 sectors erased, 1080 pages programmed, read-back verified, independent verify OK
  (sha256 96159a87...). Scope on battery, USB unplugged, Sipeed FT2232D on J2. Power cycle and #VER fpga=0x1533 check pending.
- Power cycle on 0x1533: #VER fw=0.2.20 fpga=0x1533, caps 0x7C0B (EXT_TRIGGER), diagnostics show 0x1533 (owner). But: four host port resets within
  the first minute of bench traffic (event log: DTR toggles, CLEAR_FEATURE on EP2 IN 0x82, suspend, bus_reset, set_config at 40.4/40.9/41.1/49.5 s),
  Windows error 22 on the host. Restart record showed restarts=1 with reset LR 0x2a14 / SP 0x7bd8 / CPSR SVC = the SD bootloader in SRAM after the
  owner's power cycle (DDR kept the magic): the predicted false positive; uptime matched the power cycle, no self-restart.
- A/B on 0x1533: fw 0.2.11 (no EXT code) in RAM: 3 bench passes, 300 x 64 KiB, 0 failed, no resets. => the bitstream is fine; the drops come
  from firmware code gated on fpga >= 0x1533 (EXT support since 0.2.9); suspect: GET_STATUS reading FPGA cmd 0x28 from the fast path (ISR) while
  the main loop uses the Port E bus. With fw-gen4 (0.2.22). 0.2.21 (fallback removed, 6be6f9e8) being checked on 0x1533 next.
- 0.2.21 (fw-gen4, commit 1a893a9, sha256 6be6f9e8..., pinned with ELF; tick fallback removed, counters 47/57 retired) FEL-loaded on 0x1533:
  4 bench passes, 400 x 64 KiB, 0 failed, no resets, event log only DTR edges. Weakens the EXT/0x28 theory: the 0.2.20 drops came after a
  COLD boot from the card with USB plugged in later (six suspend events in one ms at plug-in), every clean run was a FEL load with USB attached.
  **0.2.21 persisted to the card** (backup sd_750-1522_20261018-192445.bin = 0.2.20 image) for a cold-boot test with the owner.
- **Cold-boot test, 0.2.21 from the card on 0x1533** (power cycle, USB plugged ~10 s later, bench immediately): passes 1-2 clean, pass 3
  "100 x 64 KiB: 91 failed", pass 4 died; no restart (uptime 137 s). Event log: repeating tx_abort (2 s no progress, 6-64 KiB left) +
  tx_flush every ~3 s from 117.8 s, rx_full/rx_release near the end => the bulk IN pipe stopped being serviced by the host mid-response.
  With 0.2.20 after the previous cold boot the host cleared a HALT on EP 0x82 and reset the port. Every FEL-loaded run today was clean
  (0.2.11/0.2.19/0.2.20/0.2.21, two 30-min soaks, >1500 x 64 KiB). Hypothesis (fw-gen4 task, 0.2.22): the SD-boot path inherits USB
  controller state from Atlan's bootloader and 1.00o5's usb_device_init never soft-resets the controller / data toggles; the FEL path does
  not. This may also explain today's early drops (0.2.2 idle, 0.2.3 persist were cold-booted card images).
- **EXT trigger functional test (0x1533, fw 0.2.21, loopback via the generator, owner switched the generator on in the UI while EXT was selected
  over the protocol):** trigger menu shows EXT (owner); GET_STATUS ext flags 6 (generator on, edge seen); 10/10 captures in normal mode
  triggered. Trigger index vs the CH1 rising edge of the 1 kHz square: at 20 MS/s -50 ns (one sample early, consistent over 6 captures;
  fpga-gen2 predicted 20-25 ns FPGA latency plus the ADC pipeline, so the sign/size need a cleaner edge source to pin down); at 1 MS/s -1.0 us
  in 17 later captures, but +11/+12 us in the first 10 captures and +2/+3 us for the CH1 source in one run, i.e. the sample-domain trigger
  position of host captures varies by run at decimated rates for BOTH sources (geometry/alignment item, not EXT-specific; data in
  backups/ext_loopback_CH1_fast.json). Trigger set back to CH1 afterwards. Card boots 0.2.21.
- Firmware 0.2.22 (fw-gen4, commit 7b6d855, sha256 6615bab6..., pinned with ELF): usb_device_init asserts the OTG bus soft reset and the PHY
  reset for 1 ms before 1.00o5's init, and logs the controller state it found (event type 16, five fields). FEL-loaded on 0x1533: 2 bench
  passes, 200 x 64 KiB, 0 failed. Init state after a FEL load: POWER/FADDR 0x4000, INTUSBE/CCU 0x070f, EP2 TXCSR 0x2000, EP2 RXCSR 0x0000,
  ISCR 0x2fc0. Cold-boot comparison pending. fw-gen4's caveat: the pass-3 failure after 200 clean transfers argues against a stale init state;
  the cable going in after boot (charging start, contact bounce = six suspends in 1 ms) is the other difference. Tests: FEL-loaded + cable
  replug, and cold boot with the cable already in.
- Firmware 0.2.23 (fw-gen4, commit 25e90fc, sha256 545126ff..., pinned with ELF; = 0.2.22 + TX-abort diagnostics types 17/18). FEL-loaded on
  0x1533: 1 pass clean. **Test 1 (cable unplug/replug with the FEL-loaded image running):** 4 bench passes after the replug, 400 x 64 KiB,
  0 failed, tx_aborts 0; the replug itself produced 15 bus resets / 18 suspends (enumeration storm, like after the cold boots) but no
  functional failure. The 128-entry ring had already been pushed out by the benches' EP0 setups and DTR edges, and the snapshot was still the
  boot-time one (never re-armed) -> lesson: re-arm before each test, drain the ring after. **0.2.23 persisted to the card** (backup
  sd_750-1522_20261018-194839.bin = 0.2.21) for test 2: cold boot with the cable already plugged in.
- **Test 2 (0.2.23 from the card, cold boot with the cable plugged in):** passes 1-4 clean, pass 5: 1 of 100 64 KiB failed, tx_aborts 1,
  link recovered by itself (no bus reset, no suspend). Abort diagnostics: bytes left 0, POWER 0x70 (HS, soft-connected, not suspended), EP2
  TXCSR 0x0003 (TxPktRdy + FIFONotEmpty: a packet waiting that the host did not take for 2 s), TX IRQ enabled, frame counter alive. Init
  state at boot (type 16): cold SD boot = all five fields 0x0000 (OTG/PHY clocks off, nothing configured); FEL load = 0x4000/0x070f/0x2000/
  0x0000/0x2fc0 (boot ROM had already brought the controller and PHY up). Pattern of the day: every cold-boot run failed within minutes
  (0.2.20 port resets, 0.2.21 20 s stall, 0.2.23 one abort), every FEL-loaded run was clean (two 30-min soaks, >2000 x 64 KiB). Working
  theory (inference, with fw-gen4 for 0.2.24): 1.00o5's USB init lacks the PHY calibration/tuning the boot ROM performs, leaving a marginal
  high-speed link on the cold path; Windows' packet-error recovery then shows as stalled TxPktRdy / pipe resets. Card boots 0.2.23.
- Firmware 0.2.25 (fw-gen4, commit 85a113d, sha256 c71935f9..., pinned with ELF; includes 0.2.24's bus-event ring): **1.00o5 wrote its three
  USB PHY tuning values (RES45 calibration, TX amplitude, disconnect threshold) to 0x01C13410, the PHYCTL offset of later Allwinner chips;
  on the F1C100s PHYCTL is 0x01C13404 (Linux phy-sun4i-usb suniv config + suniv-f1c100s.dtsi).** Protocol builds now write 0x404. FEL-loaded
  on 0x1533: 3 bench passes, 300 x 64 KiB, 0 failed. Post-init log (type 19): POWER/FADDR 0x700e, INTUSBE/CCU 0x270f, ISCR 0x3f00,
  USBPHY_CFG 0x0003, PHY tuning read-back after the writes 0x00e9 (= the packed written value), before the writes 0x0068 (defaults), PHYCTL
  0x2b80. So the tuning writes now reach the PHY; in every earlier image they did not. Persisted to the card for the cold-boot soak.
- **Cold boot with the cable in, 0.2.25 from the card** (power cycle 20:08): type 16 pre-init state all 0x0000 (as on every cold boot), type 19
  post-init state identical to the FEL path (0x700e/0x270f/0x3f00/0x0003/0x00e9/0x0068/0x2b80): the PHY tuning now reaches the PHY on the
  cold path (RES45 calibration on, disconnect threshold 3; before 0.2.25 every cold boot ran the PHY at its defaults). First bench pass clean.
  30-minute capture-mode soak started 20:09 on the cold-booted scope (decisive comparison: 0.2.21/0.2.23 cold boots failed within 5 min).
