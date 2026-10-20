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
- **Cold-boot soak, 0.2.25 from the card (20:07-20:37, capture mode, 4 KiB every 50 ms): 35,728 requests, 0 failures, 0 drops, 0 restarts,
  counters delta none.** Every earlier cold boot (0.2.20/0.2.21/0.2.23) failed within 5 minutes; the PHYCTL register fix (0x01C13404 instead
  of 0x01C13410, RES45 calibration on, disconnect threshold 3) is accepted as the cause and the fix of the day-long USB drop story.
- Owner decision: the byte-identical reference-build rule is dropped (lineage stays in git history and the credits); CI step removed; a
  dead-code purge is scheduled after the spectrum (fw-gen5) and decimation (fw-decim) work lands.
- Firmware 0.2.27 (fw-gen5, commits efc5170/13d4185, sha256 29db9cd4..., pinned; includes 0.2.26 = channel-menu FFT switch removed, rows
  moved up) FEL-loaded on 0x1533: bench clean. FFT_BENCH (Hann, CH1 buffer, CP15 0x0005107c: MMU off, so the D-cache is ineffective):
  DDR 1024/2048/4096/16384 points = 5.3 / 11.4 / 24.3 / 108.7 ms (+0.6/1.3/2.5/10.1 ms dB conversion); on-chip SRAM 1024/2048/4096 =
  4.0 / 8.5 / 18.1 ms. About 4x slower than the uncached estimate; 2048-4096 points still fit a UI pass. Enabling the MMU with a flat map
  (framebuffer uncached) would turn the D-cache on and is the obvious speed-up for everything (later item). Card keeps 0.2.25.
- Owner's verdict on 0.2.27 channel menus: Open CH, Invert, Coupling AC/DC, V/A mode, Probe mode 1x..1000x on both channels; the dead FFT
  switch is gone. XY mode already exists in System settings (Atlan's); phosphor grading of it is a later nicety.
- FPGA 0x1534 (fpga-gen3, decimation filter: K = 0 legacy / 1 boxcar / 2 CIC2 / 3 CIC3, 16-bit samples, cmds 0x53/0x54/0x55, read 0x29;
  fixed latency 120 ns + K*(N-1)*2.5 ns): bitstream sha256 83140510... (282,898 B) pinned as backups/al3_decim_0x1534_83140510.bin, padded
  1 MiB image backups/al3_decim_0x1534_padded.bin sha256 c387a15e.... **Not flashed**; waits for the firmware support (fw-decim) to be benched
  on 0x1533 first. Firmware 0.2.28 (fw-gen5, e6a7c4c, protocol 1.7): on-scope spectrum view as a main-loop display mode; not yet benched.
- fw-decim branch merged into main (merge a45e500; commits b770356/5702f7a/66bd6b4): decimation support, fw 0.2.30 / protocol 1.9 / row 47,
  GET_DECIM 0x020C / SET_DECIM 0x020D, caps bit 15, u16 READ_TRACE/READ_RAW, GET_CAPTURE_INFO 76 bytes, Filter box in the acquisition menu
  (0x1534 only). Not benched; the first clean bench image comes as 0.2.31 together with fw-gen5's FFT speed-up (its work-in-progress edits
  were in the shared tree, so no pinned image was taken from this merge).
- **Spectrum view (fw 0.2.28) works on the scope (owner: "wow, the spectrum analyser works").** READ_SPECTRUM while the view was on: CH1,
  Hann, N=2048 at 1 MS/s (488 Hz bins), generator 1 kHz square: fundamental 977 Hz at +2.71 dBV (theory for a 3.1 Vpp square: +2.9 dBV),
  3rd harmonic -9.7 dB (theory -9.5), 5th -14.4 dB (theory -14.0), 7th/9th/11th present; GET_STATUS byte 43 = 1 in the view. Levels and
  harmonic ratios within 0.4 dB of theory.
- Firmware 0.2.31 (fw-gen5, commits 0930c72/2af662c, sha256 b5685268..., pinned with ELF; = 0.2.30 decimation support + FFT hot loops in
  ARM assembly). Loaded on 0x1533: census clean, caps 0x7C0B (no DECIM, as expected), GET_DECIM refused with E_NOCAP. FFT_BENCH vs 0.2.27:
  DDR 1024 2.2 ms (was 5.3), 2048 4.9 (11.4), 4096 10.9 (24.3), 16384 51.4 (108.7); SRAM 1024 1.0 (4.0), 2048 2.3 (8.5); peak levels
  unchanged. 2.2-2.5x faster; 4096-point spectra now cost ~11 ms per channel per pass. Owner: keep memory depth default at 3000 (UI slows
  down at long memory).
- Owner on 0.2.31 spectrum view: harmonic comb looks as before. READ_SPECTRUM cross-check logged below.
- Spectrum view, Nyquist-bin spur: the last bin (500 kHz at 1 MS/s) reads -32 dBV against a -50 dBV floor. Live capture: CH1 core A mean
  130.93 vs core B 125.58 codes (+5.35, the known A/B zero spread); the sample-skipping decimation alternates cores, so the A/B offset
  appears as an fs/2 tone. Fix: per-core zero correction in firmware (roadmap item 5) and/or the 0x1534 boxcar/CIC modes.
- Firmware 0.2.33 (fw-gen5, commits 83aa5b5/d125a41/25a8f84, sha256 e002ab21..., pinned with ELF; 0.2.32 = waterfall + density views, 0.2.33
  = FFTs up to 2048 points from on-chip SRAM, memset trace clear). Loaded on 0x1533: census clean, pass_ms 52 in scope view. Owner:
  "Looking at the waterfall, very nice!" with the generator stepped 1/2/3 kHz per second. Density view and the Ref-tap history clear
  still to be confirmed. The gesture/Auto/Reset UX request is next for fw-gen5. Card keeps 0.2.25.
- FPGA 0x1534 final (fpga-gen3, commits 6d4c8ad/65417a8/ca12aea/8445686): 2304 LUT / 1790 reg (0x1533: 635/486), BRAM 32/48, clk_200MHz
  slack +0.10 ns at the SDC's 40 % duty (~+0.6 ns at the PLL's 50 %), deterministic build, tb seeds all pass (K=0 cycle-identical to
  0x1533 incl. EXT; decimated samples equal the reference at latency 12; 9 mutations caught). Model: alias rejection at fo/4 10.5 / 20.9 /
  31.4 dB for K=1/2/3 (skipping: 0 dB), droop 0.9 / 1.8 / 2.7 dB at fo/4, resolution +3.3 / +3.6 / +3.75 bits at N=100. 0x1535 FIR
  study: a FIR at the output rate cannot fix aliases, so the plan is CIC by N/2 followed by a 15-tap symmetric FIR decimating by 2
  (flat to fo/4 within 0.08 dB, alias rejection 17 / 34 / 51 dB), one M18x18 per channel, engages at N >= 16, N multiple of 4.
- **FPGA flash written with the decimation bitstream 0x1534** (21:46): pre-write dumps backups/20261018-214611-pre-decim.bin and
  backups/20261018-214621-pre-write.bin both sha256 96159a87... (= the 0x1533 image, rollback confirmed); wrote
  backups/al3_decim_0x1534_padded.bin (c387a15e..., 0xFF-padded fpga/al3/out/FPGA-decim.bin 83140510...), 69 sectors erased, 1104 pages
  programmed, read-back verified, independent verify OK. Scope on battery, USB unplugged, Sipeed FT2232D on J2. Power cycle pending.
- Owner decision: the spectrum view's frequency axis is capped at 25 MHz (front end rolls off ~30 MHz); fw-gen5 implements it.
- **Decimation bench, FPGA 0x1534 + fw 0.2.34 (card 0.2.25 booted the new bitstream in legacy mode fine; 0.2.34 FEL-loaded: caps 0xFC0B
  with DECIM).** At 1 MS/s (N = 200) via host captures (8-bit READ_TRACE path):
  - Read-back: GET_DECIM mode/S/N and GET_CAPTURE_INFO agree with the spec (boxcar S=0, CIC2 S=8, CIC3 S=15; 0x29 = 0x81/0x82/0x83).
  - DC level (generator off, pin pulled down): mean codes legacy 84.40 (std 0.86), boxcar 86.22 (0.19), CIC2 86.20 (0.11), CIC3 86.56
    (0.12). Noise drops 4-8x as expected; the ~1.8-code mean step between legacy and filtered needs an explanation (legacy sampling
    one core vs both cores summed, and the (adc_a_comp + adc_b_comp)/2 conversion; handed to fw-cal).
  - 1 kHz square, normal trigger: triggered in every mode; fundamental 5.71 dBV legacy vs 5.63 (boxcar/CIC2/CIC3), within 0.1 dB;
    **Nyquist A/B spur -27 dBV legacy -> -88 dBV filtered (gone)**; noise floor -55 -> -58 / -64 / -72 dBV.
  - Alias test, 900 kHz square at 1 MS/s (fundamental folds to 100 kHz): +2.4 dBV legacy, -17.5 boxcar (-20 dB), -30.4 CIC2 (-33 dB),
    -35.7 CIC3 (-38 dB); the residual for CIC2/3 sits at the 8-bit path's quantisation floor, so the true rejection is likely higher
    (inference; to be measured with the 16-bit WIDE path once the host has it). Data backups/decim_bench_0x1534.json.
- Firmware 0.2.35 (fw-gen5, commits 36149d1/0ef8230, sha256 b1be459d..., pinned): spectrum gestures (tap left/right of the plot = span
  x2 / /2, drag = pan), Span/Start fields replaced by Auto and Reset, FFT length capped at 4096 (default 2048), density view without a fade
  pass (per-cell count + last-hit index, exp(-age/16) decay, black-blue-red-yellow-white ramp). Loaded on 0x1534: census clean, pass_ms 33.
  Owner checks pending. mcp 0de32dd: protocol 1.9 host support (16-bit captures, scope_set_decimation, simulator al3d), CI green.
  FPGA 0x1535 interface fixed by fpga-gen4 (0x56 FIR enable; latency 25 write clocks predicted), build/testbench pending.
- Firmware 0.2.36 (fw-gen5, commits 8e1a946/67c844e, sha256 6060514b..., pinned): 25 MHz axis cap (PC_SPECTRUM_FMAX_HZ). Loaded on
  0x1534, census clean. Alias test repeated through the 16-bit host path (mcp 0de32dd): 900 kHz square at 1 MS/s -> 100 kHz alias +2.6 dBV
  legacy, -17.6 boxcar, -30.1 CIC2, -35.8 CIC3 (same as the 8-bit path, so the residual is real, not quantisation; cause open).
- **Per-core zero vs sample rate (fw-cal's hypothesis confirmed):** legacy K=0, steady level, CH1: at 200 MS/s A 87.80 / B 85.09 (spread
  2.7, mean 86.44); at 20 MS/s and every slower rate A ~87.0 / B ~81.5 (spread 5.5, mean 84.2). Filtered modes (ADCs always at full rate)
  read 86.3-86.6 = the full-rate mean. Legacy mode gates the ADC encode clocks to the sample rate (zaklad.v), so below 200 MS/s the AD9288
  cores run far below their rated minimum and core B's zero shifts by -3.6 codes. Two zero sets per core (full-rate, gated) cover it.
- Firmware 0.2.37 (fw-gen5, commits 0f3e743/fd8c7db, sha256 9fde228a..., pinned): dB conversion in ARM assembly. Loaded on 0x1534, census
  clean. FFT_BENCH dB column: 1024 0.32 ms (was 0.70), 2048 0.72 (1.33), 4096 1.44 (2.72), 16384 5.63 (11.06); run columns unchanged
  (4096 points 10.96 ms DDR, 2048 2.30 ms SRAM). Owner checks of 0.2.35-0.2.37 features pending (gestures, Auto/Reset, density, Filter box,
  25 MHz stop label).
- Owner: with CIC3 the spectrum's upper tail "wags" up and down, seemingly building up after a mode change. Measured 12 consecutive 16-bit
  captures per mode (1 MS/s): no drift in any mode (CIC3 DC mean16 20983-21048, std 14-28 LSB16; CIC2/boxcar likewise); the upper-band
  floors vary -61..-76 dBV between captures in every mode. => window-leakage variation of the square's harmonics, now visible because the
  filtered floor is 20-30 dB lower; not a filter fault. Fix: spectrum trace averaging / max-hold in the view (fw-gen5). Other owner feedback:
  drop the "decimation filter CIC3, N = 200" info text; span zoom should drive the time base at full/minimum span; Filter setting persists
  (settings word 207), spectrum view settings to be saved except the view mode.
- Firmware 0.2.38 (fw-gen5, commits 6ce82d9/348e80b, sha256 a6f50f6c..., pinned): "pass NN ms" shown under the acquisition menu's Memory
  header (value = last main-loop pass before the menu opened). Loaded on 0x1534, census clean. Owner check of the label placement pending.
- Firmware 0.2.39 (fw-gen5, commits 11b1729/4acb971, sha256 267e80fe..., pinned): span zoom drives the time base (zoom-out at full span ->
  faster rate up to 50 MS/s; zoom-in below 64 bins -> slower rate), Auto picks the rate, "CIC3" tag instead of the filter line, spectrum
  FFT length/window/scale/ref saved in settings words 208/209 (1.00o5 writes sector 709 only on power-key shutdown). Loaded on 0x1534,
  census clean. Owner checks pending.
- Owner on 0.2.39: zoom-out steps the rate up (OK); zoom-in never steps it down (the 64-bin rule is too strict at 2048 points); at 10 MS/s
  zoomed to 0-312 kHz the spectrum flickers bimodally (noise floor vs comb). Measured: in AUTO mode at 10 MS/s (0.30 ms record, 1 kHz
  square) 2 of 6 host captures are flat (no edge, p-p 0 V) yet flagged triggered with auto_timeout clear, 1 is garbage, 3 proper; NORMAL
  mode 6/6 proper; at 5 MS/s both modes 6/6 proper. The FPGA auto time-out (0x0E, words, 13-bit) expires before the next edge at fast rates
  and the firmware reports those records as triggered. Firmware fix requested (truthful flags, time-based auto hold-off, view skips
  untriggered records); the view's trace averaging masks the rest.
- Firmware 0.2.40 (fw-gen5, commits e595a0f/9a02173, sha256 3907e7ac..., pinned): spectrum trace averaging (Off/4/16/64/Max hold, default 4,
  dB-domain, restarts on bin-meaning changes, reported in the READ_SPECTRUM header), one-line header (peak marker per channel, RBW, Avg,
  filter tag), warnings centred over the plot, taller plot, Roll box without on/off text. Loaded on 0x1534 with CIC3 selected. Owner checks
  pending. Not yet in: the auto-mode time-out fix and the wider zoom-in threshold (0.2.41).
- Firmware 0.2.41 (fw-gen5, commits 0fd6a9c/f041300, sha256 08aeaa63..., pinned; fw-gen5 retired, handoff in firmware/AGENTS.md section 20):
  firmware auto trigger mode (FPGA armed in normal mode, main loop forces after max(100 ms, 2 records)), truthful untriggered flags, zoom-in
  rate step at span <= fs/16. **Verified:** 1 kHz square, 3000 samples, AUTO mode: 10 MS/s 6/6 single-edge records (was 2 of 6 flat),
  5 MS/s 6/6, ~120 ms per host capture; no signal at 10 MS/s: 4/4 reported triggered=0 auto_timeout=1, ~200 ms per capture. The earlier
  E_BUSY refusals were the owner's open menu (30 SET_TRIGGER + 10 SET_TIMEBASE later: 0 refused).
- fw-cal delivered calibration v2 on branch fw-cal (per-core zeros in two clock classes, full-rate and gated, per range; dithered sub-code
  correction; versioned CRC block in sector 708; GET_CAL 248 bytes, protocol 1.10 row 48; diagnostics show v1/v2 and per-core zeros).
  Rebase onto main and renumber to 0.2.42 requested before merging; bench steps in firmware/README.md 6.2 (owner runs base calibration).
- Firmware 0.2.42 (fw-gen5, commits cebb982/7d20866, sha256 b23f5fc8..., pinned; fw-gen5 retired): deterministic Auto (probe at 50 MSa/s,
  period search, f0/f_hi rules, RBW <= f0/4), actionable warnings with MSa/s units, header without Avg. Loaded on 0x1534, census clean.
  Owner checks pending (three identical Auto presses -> identical settings; expected 200 kSa/s at depth 3000 for the 1 kHz square).
- FPGA 0x1535 (fpga-gen4, commits e2aec2e/74fb8a3): CIC by N/2 + 15-tap FIR by 2, FIR enable 0x56; built: 3082 LUT / 2716 reg, 32 BRAM9K,
  2 of 3 M18x18 inferred; clk_200MHz slack +0.324 ns; tb passes (latency 25 write clocks verified, EXT lag formula within 1-4 ns); FIR engages
  at 10 MSa/s and slower. Bitstream sha256 5040a2ae... pinned, padded image 5ade9e75.... **Not flashed**; waits for mutation runs, docs, and
  the firmware support (queue: FPGA 0x1535 firmware support, now unblocked for a fresh firmware agent).
- fw-cal merged into main (merge b27c1ee): calibration v2, fw 0.2.43, protocol 1.10, row 48. Rebuilt on main: sha256 b8efcb04..., pinned with
  ELF. SD sectors 700-710 backed up before the bench (backups/sdcard/sd_700-710_20261018-224105_pre-calv2.bin). Loaded: census clean,
  GET_CAL both channels: 248 bytes, version 1, flags 4 (fw-cal expected flags 0 before calibration; to be explained). Pass times before
  calibration: 44 ms at 20 MSa/s, 45 ms at 1 MSa/s, 58 ms at ~10 kSa/s (max 79-80). Next: owner runs Base calibration with probes and USB
  unplugged. fw-gen6 spawned (worktree) on the 0.2.44 batch: 0x1535 FIR support, spectrum strip overdraw fix, CIC2 default.
- **Base calibration v2 run by the owner ("Calibration successful").** GET_CAL: version 2, flags 0x07 on both channels. CH1 with the
  generator off (pin pulled down, i.e. ~-1.24 V vs probe ground, NOT a shorted input): corrected trace odd/even (core A/B) means differ by
  -0.05 at 1 MSa/s and -0.06 at 10 kSa/s (raw spread +5.95), -0.23 at 20 MSa/s, but -1.49 at 200 MSa/s (raw +4.51); filtered modes read
  94.65 vs gated legacy 93.0 (1.65-code step remains at this level). Legacy 1 MSa/s with the 1 kHz square: Nyquist spur -34.7 dBV against a
  -61 dBV floor (was -27 vs -50): 8 dB better but still 26 dB above the floor. Interpretation (inference): the zero correction works where
  the level is near the calibration point; the residual spur with a +/-1.5 V square and the full-rate/filtered offsets at -1.24 V point at a
  per-core GAIN mismatch (and a class-dependent gain), which a zero-only model cannot remove. Pass time 30 ms at every rate (was 44-58):
  the readout loop is faster. Next: shorted-input measurements (true 0 V) and a per-core gain term.
- **Shorted-input check of calibration v2 (both probes shorted, DC):** CH1 corrected A/B difference +0.03 at 200 MSa/s, -0.14 at 20 MSa/s,
  +0.05 at 1 MSa/s, -0.04 at 10 kSa/s (raw spread 3.5-5.7 codes); corrected means 127.9 (target 128); filtered boxcar/CIC2/CIC3 127.9-128.0,
  equal to legacy (the 1.65-code step at -1.24 V is gone at 0 V, so it was gain). CH2 (range 6): differences +0.44/-0.20/-0.17/-0.39,
  filtered 128.35 vs legacy 128.6-128.9. Zero model verified in both clock classes; a per-core gain term is the remaining item (bench with
  1.000 V from the supply next).
- USB drop at 22:48 on 0.2.43 (FEL-loaded, PHY fix present) when the host opened the port after ~5 min idle: bus ring shows suspend ->
  bus_reset -> set_config right then, no tx_abort/clear_feature; usb_resumes still 0 after 18 suspends => the device never handles the
  resume; Windows resets the port. Queued: USB suspend/resume handling. The earlier 150-155 s storm of six resets = the owner's unplug/
  replug for the calibration.
- Correction (fw-cal): the "corrected" means logged above came from the host's own compensation of READ_RAW, not the firmware. Firmware
  path (READ_TRACE flags 0), shorted inputs, 4 captures averaged: CH1 A/B difference +0.42 at 200 MSa/s (full-rate class, noisier data),
  +0.07 at 1 MSa/s, +0.02 at 10 kSa/s, means 127.9 (target 128), CIC2 127.94 = legacy: **CH1 zero criterion met at the gated rates and in
  the filtered path.** CH2 at range 6: firmware codes equal raw codes (128.6/128.9), i.e. no correction applied there; +0.53 at 200 MSa/s.
  Reported to fw-cal (range-6-uses-5 binding suspected). Pass time 30 ms and the spur improvement are firmware-path facts and stand.
- **Gain bench, CH1 at 1.0045 V DC (DMM), range 2, dcoffset 0, 4 captures averaged (backups/gain_bench_1V.json incl. the GET_CAL dumps):**
  firmware READ_TRACE A/B: 200 MSa/s 157.01/155.76 (+1.25), 1 MSa/s 156.11/156.53 (-0.42), 10 kSa/s 156.14/156.56 (-0.43), CIC2 156.20;
  raw A/B: 200 MSa/s 160.57/155.82 (+4.75), gated 159.24/153.97 (+5.27). Against the 0 V raw values (131.73/127.86 full, 131.09/125.33 gated):
  per 1.0045 V core A gains 28.84 codes full-rate vs B 27.96 (A 3.1 % higher), gated A 28.15 vs B 28.64 (B 1.7 % higher): the core gain
  mismatch is real and its sign depends on the clock class. Host volts model read 0.977-0.979 V (2.6 % low at range 2).
- fw-cal on the CH2 range-6 question: the correction is applied but tiny by construction; CH2's range-5 zero drifted ~0.7 codes since the
  calibration (warm-up/drift suspected; Atlan recommends 5 minutes warm-up); the CH1 full-rate residual is 25 kHz offset-PWM ripple
  (fixed by averaging 64 conversions at full rate, commit 3bfc892 on fw-cal, unbuilt).
- Owner: the diagnostics view shows the v2 calibration data. fw-cal (~500k tokens) is writing its handoff; fw-cal2 will build v3 (per-core,
  per-class gain from the voltage-calibration steps; the PWM-slope ratio predicted the sign but only half the magnitude of the measured gated
  gain mismatch).
- **Gain bench, CH1 at -1.0045 V DC (DMM -1.005/-1.004), range 2, dcoffset 0, 4 captures averaged (backups/gain_bench_neg1V.json
  incl. the GET_CAL dumps, script scratchpad gain_bench.py), fw 0.2.43, scope on all day:** firmware READ_TRACE A/B: 200 MSa/s 99.49/100.07
  (-0.58), 1 MSa/s 98.76/99.41 (-0.65), 10 kSa/s 98.73/99.46 (-0.73), CIC2 99.88; raw A/B: 200 MSa/s 103.06/100.14, gated 101.88/96.85.
  Host volts: 200 MSa/s -0.973, 1 MSa/s -0.997, 10 kSa/s -0.997, CIC2 -0.970.
  **Combined with the +1.0045 V point (endpoint gains over 2.009 V):**
  - Full-rate class: A 28.62 codes/V, B 27.71 codes/V, **A/B = 1.033** (same 3.1-3.4 % at both polarities): a genuine core gain
    mismatch. Both cores' +/-1 V midpoints land within 0.12 code of the shorted-input raw zero, so the full-rate transfer is linear.
  - Gated class: A 28.54, B 28.42 codes/V, **A/B = 1.004**: the cores match in gain. The "1.7 % B higher" from the +1 V point alone was a
    zero error: core A's +/-1 V midpoint sits 0.53 code below its shorted-input raw zero (B: +0.08), i.e. about 0.5 LSB of relative
    INL/bow in core A at the slowed clock (AD9288 INL spec order), which a linear gain cannot remove. The firmware trace A-B difference is
    -0.4 at +1 V and -0.65 at -1 V (same sign both ends), consistent with that bow and not with a gain term.
  - Class gain: full-rate mean 28.17 codes/V vs gated 28.48 (full 1.1 % lower). CIC2 spans 28.02 codes/V, i.e. the CIC path carries the
    full-rate class gain (its 8-bit mean of both cores) as expected from its full-speed encode clock.
  - Host volts model (single uv_per_lsb, calibrated at the gated rate): 0.3-0.8 % low at gated rates, 2.7-3.5 % low at 200 MSa/s and in
    CIC2. **Conclusion for v3:** one gain per core and class is enough (no split above/below zero); the full-rate class needs it
    (3.3 % A/B, 1.1 % class), the gated class barely (0.4 %); the residual 0.5-code bow at gated rates is INL and stays.
- mcp-gen3's GET_CAL v2 host decoder verified against the live scope: both channels report version 2, flags block_loaded, bound,
  current_divider_gated; stored full-class zeros 129.3-130.8 codes, gated 127.9-128.1, slopes -0.259..-0.266 codes/count; a shared-mode
  capture through the v2 volts path read CH2 (shorted, range 6) at +1.4 mV and CH1 at the -1 V reference.
- **Owner recalibrated (base calibration) after the scope had been on all day, fw 0.2.43** (GET_CAL dumps before: backups/gain_bench_neg1V.json,
  after: backups/getcal_after_recal_20261018.json). Zero changes per range vs the morning calibration: CH1 gated A/B within +-0.44 code
  (mostly <= 0.19), CH1 full-rate +-0.1..1.25 (8-conversion ripple noise; 64 conversions come with 0.2.44); CH2 gated A +0.44..+0.75,
  B -0.75..-1.06 on every range, PWM centres +2..+3 counts: **CH2's core-to-core zero moved about 1.6 codes between the two calibrations**,
  CH1's did not. Right after the recalibration (CH1 still on the -1 V reference, CH2 probe state not confirmed), 1 MSa/s, dcoffset 0, fw
  trace means: CH2 range 6 A/B 128.60/128.80, then ranges 6/5/4/3/6 in sequence 129.48, 129.18, 129.17, 129.15, 128.23 (per-core agreement
  within 0.2 at every step): the CH2 mean wanders about 1.3 codes within a minute while the cores stay together. Reads as a slow input /
  relay settling or pickup effect, not a per-core calibration error; the gated zero model itself holds (A-B <= 0.21). Overnight plan:
  log both channels' per-core raw and corrected means with shorted inputs at 1 MSa/s and 200 MSa/s every 10 minutes.

### 2026-10-18 late evening: fw 0.2.44 loaded, overnight log

- fw-gen6's 0.2.44 batch merged into main (463931d: FIR 0x1535 support, spectrum strip repaint, CIC2 default, USB suspend/resume rework,
  generator menu hit-area fix) plus fw-cal's 64 full-rate calibration conversions. Built on main with GCC 10.3.1, pinned as
  backups/fw-0.2.44_d4c0b716.bin (sha256 d4c0b716dfaf79a7a7a7b8932a83cfa0dd63f6ebcef075932823e951f7aeccdb), loaded into RAM hands-free
  with fnirsi1013d-flash (ENTER_FEL -> usbipd -> sunxi-fel, 7.3 s). fw-gen6's own image (08f9a5ed, same source, same GCC 10.3.1) differs
  in 172,941 bytes: the build is not reproducible across checkout paths (inference: __FILE__ strings from the worktree path); queued.
  The SD card still boots 0.2.25; a power cycle returns to it.
- 0.2.44 bench, inputs shorted (owner), CH1 range 2, CH2 range 6: census 500 PINGs 0 failures, 0.96 ms each; GET_DECIM setting off
  (owner's saved word), caps bit 16 (FIR) clear on 0x1534 and SET_DECIM with the FIR flag refused with E_BAD_ARG detail 1 as designed.
  Zeros (4 captures, fw trace per core): CH1 200 MSa/s 127.61/127.96, 1 MSa/s 128.02/128.00, CIC2 127.72; CH2 200 MSa/s 128.89/129.86,
  1 MSa/s 129.30/129.04, CIC2 129.39. CH1 is on target; CH2 reads 1.0-1.5 codes (3-5 mV at range 6) above 128 at every rate with the
  cores agreeing, within an hour of the owner's recalibration: a channel offset, not a per-core error. Candidates: CH2's zero is set at
  hw range 5 and the PWM/relay state drifts, or the shorted probe adds an offset. The overnight log decides whether it is time-dependent.
  The full-rate 0.2.43 calibration (8 conversions) is still in sector 708; the 64-conversion zeros need a recalibration on 0.2.44.
- Still to bench with the owner (needs eyes/hands): spectrum strip repaint on SET_TIMEBASE / SET_TRIGGER / SET_GEN loops and span taps,
  generator menu rows (Freq value, 1 kHz, Duty), Filter box steps, DEFAULT button gives CIC2.
- **Overnight log started 23:16** (scratchpad night_log.py -> backups/night_log_20261018.jsonl): every 10 minutes open COM10, read
  GET_LINK_STATS + GET_EVENT_LOG (this doubles as fw-gen6's USB reopen bench: suspend/resume events after 10 minutes idle with the port
  closed), capture both channels' per-core raw and corrected means at 200 MSa/s, 1 MSa/s and CIC2 with dcoffset 0, restore the owner's
  settings, close. 60 rounds. Round 0: gated CH1 128.01/127.89, CH2 129.22/129.06; full CH1 127.70/127.96, CH2 128.81/129.85.
- fpga-gen4 on the decimation bench's unexplained 100 kHz residual (CIC2 -30, CIC3 -36 dBV with the 900 kHz square): it is the DDS
  generator's own 100 kHz spur (50 MHz DDS clock, 20 ns edge grid, pattern repeats every 9 periods = 10 us), about -37.5 dBV at 3.1 Vpp,
  reproduced by the model (CIC2 -31.1, CIC3 -39.0 dBV). The filters perform as modelled. Future alias tests use 50 MHz / M, M even.

### 2026-10-19 night: 0.2.45 and 0.2.46 built, boot-tested between logger rounds

- fw-cal2 delivered calibration v3 (fw 0.2.45, protocol 1.12, GET_CAL 256 bytes, block v3 at sector 708 bytes 128..431, gain step at the
  calibration post after the zero step). Merged c20957a, built on main with GCC 10, pinned backups/fw-0.2.45_5b4cc268.bin. Boot test
  00:17 (between logger rounds, 0.2.44 reloaded afterwards): #VER fw=0.2.45 proto=1.12, census 300 PINGs 0 failures 0.95 ms, GET_CAL
  256 bytes with version 1 and flags 0x04 on both channels (the card's v2 block is no longer read, as designed: the owner re-runs the
  base calibration in the morning), GET_DECIM 32 bytes, pass_ms 31.
- fw-phosphor delivered the digital phosphor view (fw 0.2.46, protocol 1.13, GET_STATUS display_mode 4, settings word 210). Merged
  2e73d28 (conflicts: version numbers, CHANGELOG, protocol rows 51/52, AGENTS.md; both kept), built on main, pinned
  backups/fw-0.2.46_49a7989b.bin (fw-phosphor's branch image cdba5b51 unpinned). Host tests 7/7 files 0 failures, vectors OK. Boot test
  00:27: #VER fw=0.2.46 proto=1.13, census 300/0 at 0.94 ms, GET_STATUS 44 bytes with display_mode 0, GET_CAL version 1, pass_ms 31, a
  host capture of both channels (3000 samples) worked; then 0.2.44 reloaded.
- The overnight logger stays on 0.2.44 (its zero readings depend on the v2 block, which 0.2.45+ ignore). Rounds 0-7 (23:16-00:27):
  every reopen after 10 minutes with the port closed succeeded; gated CH1 128.0/127.9-128.2 steady, CH2 129.1-129.3 steady; full-rate
  CH1 127.4-127.7 / 128.0-128.4 (ripple noise of the 8-conversion zeros), CH2 128.8-129.1 / 129.6-129.9.
- fw-mmu delivered fw 0.2.48 (MMU on with a flat 1:1 map, D-cache write-back on DDR and SRAM A, framebuffer pages write-through,
  peripherals and the restart-record MB uncached, cache clean + MMU off before FEL/reset, fault status/address in GET_LINK_STATS counters
  58/59; design docs/plans/mmu-dcache.md, bench sequence firmware/AGENTS.md section 26). Merged 1cdded0, built on main: pinned
  backups/fw-0.2.48_2d067686.bin (MMU on) and backups/fw-0.2.48-nommu_9a2152d3.bin (A/B image, #VER says 0.2.48-nommu). Host tests 8/8
  files 0 failures, vectors OK, mcp 310 passed. **Not loaded tonight**: a hang would need a power cycle; the first MMU boot is the morning's
  first step with the owner at the scope (expected FFT_BENCH CP15 word 0x0005107D and DDR FFT rows 2-4x faster).
- fw-gen7 delivered the purge batch (PC_PROTOCOL=0 build, check-reference/package, FFT switch, generator Wave/Ampl/Offset/Phase rows,
  Long memory, hold-off and the v2 calibration vectors removed; reproducible images: identity data in build_id.c linked last, read from
  the commit, -ffile-prefix-map; 16-bit trigger level now corrected like the trace). Merged d3a2247 on top of fw-mmu; the merged main
  is fw 0.2.48 (both batches). Clean rebuild on main: backups/fw-0.2.48_14b2dc0f.bin (MMU on) and backups/fw-0.2.48-nommu_1cce7858.bin
  (the earlier 2d067686 / 9a2152d3 pins, without the purge, deleted). Host tests 8/8 files 0 failures, vectors OK. The identity line now
  reads "git d3a22472 of 2026-10-18 23:12:47 UTC"; an image built elsewhere from the same commit must hash identically (CI prints it).
- Boot test of fw-0.2.48-nommu_1cce7858 at 01:17 (between logger rounds, 0.2.44 reloaded after): #VER fw=0.2.48-nommu proto=1.13,
  census 300 PINGs 0 failures 0.95 ms, GET_INFO 76 bytes with the commit hash d3a22472 at byte 52, GET_LINK_STATS 244 bytes (60 counters),
  pass_ms 35, host capture of both channels OK. The purge batch boots and answers; the MMU image is still untested (morning, owner present).

### 2026-10-19 04:00: overnight log, rounds 0-28 (fw 0.2.44, inputs shorted, scope on charger)

- **USB reopen after 10 minutes idle with the port closed: 29/29 successful**, no errors, GET_LINK_STATS counter 36 (suspends) = 1 and
  37 (resumes) = 0 all night, identical to round 0: with the port closed Windows never suspended the device, so the pre-0.2.44
  "suspend -> bus_reset" at reopen was the port reset's lead-in, as fw-gen6 read it. The idle-port drop item is closed on 0.2.44.
- **Zeros, rounds 0-26 (23:16-03:38, 4.4 h):** flat. Gated CH1 A/B 127.95-128.03 / 127.84-128.24, CH2 129.07-129.32 / 128.91-129.23; raw
  gated CH2 A 129.64-129.88, B 128.10-128.42; CIC2 CH1 127.68-127.87, CH2 129.22-129.47; full-rate within the 8-conversion ripple band.
  CH2 stayed about +1.2 codes above 128 with the cores together (same as right after the recalibration): a stable channel offset at
  range 6 with shorted input, not drift.
- **Step at 03:48 (round 27), persisting at 03:58 (round 28):** CH1 both cores +0.47 (gated 128.47/128.60, full 128.02/128.75, CIC2
  128.32); CH2 raw core A -1.7 (129.8 -> 128.0) with core B unchanged (128.2), so corrected CH2 A/B 127.48/129.08 gated and 127.43/130.20
  full (A-B -1.6 / -2.8). Status flags (CHARGING always set, FPGA/auto bits toggling) show no change at the step; no restart (counter 49 =
  0); nothing was done on the bench. A per-core offset step on CH2's ADC plus a common +0.5 on CH1 within one 10-minute interval: cause
  unknown (candidates: battery charge termination / supply step, ADC temperature step, a relay or reference settling event). Morning
  check: read the battery/charger state, recalibrate on 0.2.48, and watch whether the CH2 A/B split returns.
- Logger stopped 08:04 after 53 rounds (23:16-08:00), 53/53 reopens OK. The 03:48 step held unchanged to the end: gated CH1 128.1-128.6
  both cores, CH2 corrected A 127.45 / B 128.9-129.1, raw gated CH2 A 128.0 / B 128.1-128.3 (before the step 129.8 / 128.2): CH2's raw
  cores now agree, so the v2 correction (which expected A 1.6 above B) over-corrects A by 1.6. The owner recalibrates on 0.2.48 this morning.

### 2026-10-19 morning: 0.2.48 MMU bench (owner present)

- 08:05 loaded fw-0.2.48_14b2dc0f (merged main): boots, USB fine, but FFT_BENCH CP15 = 0x0005107C (MMU off) and the old timings. fw-mmu's
  own branch image (2f768fd9) loaded next: CP15 0x0005107D, 4096-point DDR FFT 2.91 ms. Cause: pc_hooks.h guarded the enable with
  `#if PC_PROTOCOL && PC_MMU`; fw-gen7's purge removed -DPC_PROTOCOL, so the merged image compiled the hook away. Fixed 39b98c4.
- Rebuilt and re-pinned: backups/fw-0.2.48_9e0954a5.bin (MMU) and backups/fw-0.2.48-nommu_d244fa56.bin. Loaded 9e0954a5 at 08:10:
  **CP15 0x0005107D (MMU 1, D-cache 1, I-cache 1).** FFT_BENCH run times, MMU off (0.2.37/0.2.48 off) -> on:
  1024 DDR 2.20 -> 0.21 ms, 2048 DDR 4.95 -> 0.68 ms, 4096 DDR 10.96 -> 2.72 ms, 16384 DDR 51.9 -> 33.7 ms (exceeds the 16 KB cache),
  1024 SRAM 1.03 -> 0.22 ms, 2048 SRAM 2.30 -> 0.66 ms; DDR and SRAM now equal, so SRAM A buffers no longer matter for speed.
  **pass_ms in the scope view 14-18 ms (median 15), was 30.** Census 500 PINGs 0 failures 0.89 ms. No restart, fault registers 0.
  Pending: owner's screen coherence check (menus, spectrum, phosphor), touch, soak, SD, ENTER_FEL round trips (section 26 steps 4-9).
- Owner: screen, menus, spectrum and phosphor views and touch look right on the MMU image ("seems to work afaikt").
- 08:15 owner ran the base calibration on 0.2.48 (probes + USB unplugged), then the new gain step with both x1 probes on the post:
  "Gain calibration successful". SD 700-710 backed up before (backups/sd_700-710_20261019-0812_pre-calv3.bin). GET_CAL (dumps in
  backups/getcal_v3_20261019.json): both channels version 3, flags 0x0F (loaded, bound, gated, gains measured), measured on range 2,
  lever 88.0 / 88.6 codes. Gain corrections: CH1 full A 0.9993 B 1.0126, gated A 1.0004 B 0.9993, gain16 1.0059; CH2 full A 1.0161
  B 1.0067, gated A 1.0107 B 0.9895, gain16 1.0114. Raw gated zeros at the PWM centre: CH1 A 131.0 B 125.1-125.4, CH2 A 128.0 B
  128.0-128.25; full CH1 A 131.3-131.75 B 127.5-127.8, CH2 A/B 129.3-129.75. Note: the CH1 full-rate A/B gain ratio from the post
  step is 1.013, smaller than the 1.033 the +-1 V DMM bench gave; the +-1 V check decides which is right.
- 08:30 post-level check under v3 (CH1 on the post, range 2, dcoffset 0; CH2 reading 0 V, apparently off the post), MMU and -nommu
  images identical (backups/calv3_postlevel_*.json): classes agree (CH1 full mean 93.57, gated 93.48, CIC2 93.58 at -1.19 V); gated
  cores agree (A-B -0.14..-0.39); **full-rate CH1 A-B +1.4..+1.6 at the low level, +0.29 at the high level** (generator duty 1.0, +1.87 V).
  Raw A-B: full +5.0 at both -34 and +53 codes (so equal core gains now), gated +5.3..+5.5; the stored full zeros say A-B 3.9, the gated
  5.8. Generator off (pin tri-state) vs driven low: identical, so source impedance is not the cause. Reading: within 15 minutes of the
  calibration the full-rate core relationship differs from what the zero and gain steps measured (zero split off by 1.1 code, gain
  mismatch 1.3 % then vs 0 % now; last night 3.3 % with the opposite sign of today's residual). The full-rate (interleaved) class is not
  stable enough for a one-time calibration; the gated class is. Next: 10-minute full-rate zero log with shorted inputs; design a
  background interleave-mismatch estimator (queue).
- 10:26-10:33 shorted-input zero log on 0.2.48-nommu (backups/zero_drift_full_20261019.jsonl, 7 rounds, then the owner unplugged USB to
  connect the programmer, so the Windows error 22 at 10:3x was the unplug, not a firmware fault): full-rate corrected A-B CH1 -0.3..+0.3, CH2 -0.2..-0.04; raw full CH1 A-B +3.7..+4.3 (stored 3.9), CH2 +0.2..
  +0.3; gated raw CH1 +5.8..+5.9 (stored 5.8). **The v3 zeros are exact at 0 V**; the deviation seen at -1.19 / +1.87 V (raw split 5.0 at
  both) is even in the signal (about a 1-code bow at 35-50 codes from centre), not an offset; last night it was odd (gain-like).
- **FPGA flash written with the CIC + FIR bitstream 0x1535** (10:33-10:40, Sipeed FT2232D on J2, scope on battery): pre-write dump
  backups/20261019-103324-pre-fir.bin sha256 c387a15e... = the pinned 0x1534 padded image (independent verify OK: rollback image
  confirmed); wrote backups/al3_fir_0x1535_5040a2ae_padded.bin (5ade9e75...), 70 sectors erased, 1105 pages programmed, write verified,
  independent verify OK. Power cycle pending (the card boots 0.2.25, which ran 0x1534 in legacy mode; 0x1535 without 0x56 is the same path).
- 10:45 power cycle: the card's 0.2.25 boots FPGA **0x1535** (legacy mode) fine; 0.2.48 (9e0954a5) FEL-loaded: #VER fpga=0x1535, caps
  0x1FC0B (FIR bit 16 set). **FIR bench, shorted inputs, range 2** (backups/fir_bench_0x1535_20261019.json): cic2+fir at 20 MSa/s stays
  effective cic2 (N 10 not eligible), at 10 MSa/s N 20 S 1 gain 100 effective cic2+fir, at 1 MSa/s N 200 S 7 gain 10000; cic3+fir N 200
  S 13. GET_DECIM bytes 26-27 (FPGA 0x29 read now, i.e. the registers of the last arm) match fw-gen6's prediction: 0xE2 0x01 for CIC2+FIR
  at 10 MSa/s, 0x82 0x08 for CIC2 at 1 MSa/s (FIR bits clear), 0xE2 0x07 for CIC2+FIR at 1 MSa/s. Noise (host 16-bit codes, std):
  CIC2 1 MSa/s 0.054, CIC2+FIR 0.075, CIC3+FIR 0.062; at 10 MSa/s CIC2 0.120, CIC2+FIR 0.126 codes. 16-bit path 0 V: 128.67 (CH1) /
  128.78 (CH2) by the row 51 formula in every CIC mode; the firmware 8-bit trace shows 129.0 (all samples round alike). The 0.7-code
  offset is the full-rate class zero having drifted +0.8 code as a common mode of both cores since the 08:15 calibration (raw full
  A/B 132.5/128.5 vs stored 131.6/127.7), while the gated class reads 128.0 exactly: passed to fw-cal3 for the v4 design.
- **FIR alias bench** (11:05, CH1 on the post, range 3, generator 892.857 kHz = 50 MHz / 56 clean-edge square, 2.8 Vpp, fundamental
  +1.8 dBV; 1 MSa/s, N 200, alias lands at 107.1 kHz; Hann FFT of 3000 host samples, 3 captures; backups/fir_alias_bench_20261019.json):
  boxcar -17.0 dBV, boxcar+FIR -81.2; CIC2 -35.6, CIC2+FIR -76.1; CIC3 -54.0, CIC3+FIR -80.8 (noise floor about -92 dBV). fpga-gen4's
  model predicted -16.1 / -34.7 / -53.3 without and -71.5 / -83.5 / -82.8 with the FIR for a 3.1 Vpp square: the CIC rows match within
  1 dB, the FIR rows within 2-7 dB of a floor-limited measurement. **Alias rejection with the FIR: 78-83 dB below the fundamental** in
  every order, against 19 / 37 / 56 dB without it. 0x1535 is fully benched; CIC2+FIR can become the default.
- 11:00 the owner pressed DEFAULT in the diagnostics view: "Settings do default!!!" and the Filter box then reads CIC2+FIR (default verified).
  Owner remarks: the view shows the unused stock compensation tables and "fw: PECO" for the FPGA; redesign proposal published as a Design
  artifact (https://claude.ai/artifact/M3udheReCiHLQmASaY5qZr) and queued.
- 11:25 fw-usb2's 0.2.49 merged (502d90d): suspend watch every 1 ms (pc_usbw.c), inferred resume from SOFs/EP0/EP2 traffic acts like RESUME,
  soft reconnect (D+ pull-up off 1 s) after a suspend storm or 15 min suspended, GET_LINK_STATS n = 62 (60 inferred resumes, 61 soft
  reconnects), event type 20, protocol 1.14. Pinned backups/fw-0.2.49_53559995.bin and fw-0.2.49-nommu_e83461ce.bin. Host tests 9/9,
  vectors OK, mcp 360 passed. fw-usb2 found the selective-suspend GUID in the guide/skill wrong (…53bb50f7e206 vs the real
  …53bb8f07e226) and fixed it; this laptop's active plan: selective suspend **disabled on AC, enabled on DC** (powercfg /Q), so the 10:20
  loss happened with the host setting off on mains.
- 11:40 bench running unattended: Windows USB selective suspend enabled on AC and DC (was AC off / DC on; to be restored), then
  `fnirsi1013d-soak --mode capture --duration 1800` on 0.2.49 (MMU) followed by `--mode reopen --duration 10 --idle 1800 --rounds 3`.
- 12:20 fw-cal3's 0.2.50 merged (cdc7e2f): calibration wizard (modal three-step flow), interleave tracker (A-B offset/gain with a bow-aware
  fit, applied to the full-rate class), class probe (gated/full/gated conversions on a quiet input, every <= 30 s, corrects the full-class
  common mode), Spectrum/Phosphor rows at the top of the main menu, GET_CAL 288 bytes. Renumbered in the merge to protocol 1.15 / row 54
  (fw-usb2 holds 1.14 / row 53). Built on main: backups/fw-0.2.50_e54d29df.bin (MMU), fw-0.2.50-nommu_41c207bb.bin. Host tests 11/11,
  vectors OK, mcp 360. Not loaded yet: the soak holds the port.
- **MMU capture soak passed** (0.2.49 MMU image, 11:04-11:34, backups/soak_0.2.49_mmu_capture_20261019.log): 17,908 content-checked
  4 KiB TEST_PATTERN requests, 0 failures, 0 drops, 0 device restarts, longest gap 0.12 s, latency median 9.2 ms, p99 17.3 ms, max 21.3 ms,
  no new USB events, counters unchanged. Section 26 bench steps 1-3 and 5-6 are now done; left: SD save/restore, ENTER_FEL round trips
  (three loads today without issue), touch (owner: fine). The reopen soak (3 x 30 min idle, selective suspend on) runs next.
- **Reopen soak on 0.2.49 (MMU), Windows selective suspend enabled on AC and DC, 11:34-13:04** (backups/soak_0.2.49_reopen_20261019.log):
  3 rounds of 10 s soak + 30 min idle with the port closed: 3/3 reopens OK, 300 requests 0 failures, 0 restarts; counters 36/37 stayed
  1/0 in every round: **Windows did not suspend the idle device at 30 minutes even with selective suspend on.** The morning's loss
  came after about 1 h 40 of idle, so the 2-hour idle test (fw-usb2 bench step 2) is still needed; it runs when the port is next free
  for two hours. The soft-reconnect rules of 0.2.49 could not fire (no suspend seen).
- 13:05 fw 0.2.50 (e54d29df, MMU) FEL-loaded: #VER fw=0.2.50 proto=1.15 fpga=0x1535; GET_CAL 288 bytes, version 3, flags 0x0F on both
  channels (the 08:15 v3 block is still read), tracker flags 0x00 (no full-rate records yet), pass_ms 15. Wizard walk-through with the
  owner next (README 6.4).

- 2026-10-19 13:37: fw 0.2.51 (UI makeover step 1, merge 9cc599c) built with GCC 10, MMU image sha256 acf88fee1005c1c70ef5793293200eaa3b1e5b18d1ab3a7ea092e1818da83664,
  nommu e011d59776ad1c0cc41c9b0be55e0be8668bd90dbbe916495868e2db99ee571a, pinned in backups/. Not loaded: the scope still runs 0.2.50 in RAM with
  the two-hour idle USB soak on COM10 (ends about 15:12). Walk-through for the owner: firmware/AGENTS.md section 29.
- 2026-10-19 13:50: fw 0.2.52 (diagnostics readers, merge 72eacb3) built with GCC 10, MMU image sha256
  cd8bf6fc27d1d5cf9cdd733f9d2721b024d6a73b0004d393166965c5132d65ef, nommu 372267eb7917580ce0274f381bb02245b503c8ee1f6076dcdb108d2c4a752e5f,
  pinned in backups/. Not loaded (soak still on COM10). Note for the owner before loading: from 0.2.52 the firmware writes SD sector 709
  (settings) once at every boot when the sector loaded valid; SD sectors 700-710 are backed up (backups/), the stock boot chain is untouched.
- 2026-10-19 13:55: fw 0.2.54 (touch fixes batch 1 on 0.2.52; merge of fw-touch1) built with GCC 10, MMU image sha256
  05a126f77cfb8750958959e50132252b6abc94fe5ccaa3efb946467b914c6ba6, nommu 572784ed5b6160f599682576a0fd5f8f0d014013f3b68eadd49e498edc50a419,
  pinned in backups/. This is the image to load for the next owner session (steps 1 + readers + touch handles). Not loaded.
- 2026-10-19 14:07: fw 0.2.55 (UI step 3 Settings on 0.2.54; merge of fw-ui3) built with GCC 10, MMU image sha256
  89c1e94794fc19a13897459ec564092bf9fa3068ef7628dce6e017f9db56b17b, nommu 1dc9b165990bcf5c647af1e68b1319bdef26c1d2e552685868994a0b5c17b7aa,
  pinned in backups/. Now the image to load next (steps 1 + 3, readers, touch handles). Not loaded.
- 2026-10-19 14:20: two-hour idle USB soak on 0.2.50 stopped after about 65 min of idle (13:12 to 14:17) to free COM10 for the
  owner's Bode bench; no reopen check was made, so the long-idle question stays open: rerun overnight. Windows AC selective suspend
  restored to 0 (DC stays 1). First Bode sweep, RC low-pass 1 kOhm / 15 nF (fc 10.6 kHz) with both probe clips on the breadboard ground
  rail (wiring A), 1 kHz to 300 kHz 30 points, backups/bode/rc1_15n_clips.json: V1 1.36 Vrms at 1 kHz falling to 0.68 Vrms at 100 kHz
  (the RC loads the 1 kOhm source as expected, so the R and C are on the post correctly), but V2 only 25 mV rms falling to 9 mV,
  H about -33 to -37 dB flat: CH2 is not reading the R-C junction (floating tip or probe switch). Owner asked to put CH2 on the input.
- 2026-10-19 14:21: fw 0.2.56 (UI step 2 on 0.2.55; merge of fw-ui2) built with GCC 10, MMU image sha256
  35b2c2f6b1c35207673e164b45663e4b9489a0699c50304821c5025a98ac970f, nommu a406ed19c47a8db5bca57ca03aba1632fc57a41c69a7283c85f74061f90ec012,
  pinned in backups/. Now the image to load next (steps 1-3, readers, touch handles). Not loaded.
- 2026-10-19 14:27: fw 0.2.58 (0x1536 support on 0.2.56; merge of fw-seg) built with GCC 10, MMU image sha256
  2f9c36f97bd4576c2d8010e7e78c92ca0dd1c8cf01303e2e0a40d9e9a47d642a, nommu 887f1a0406545735f82803fd588ee79566658cadb258853478a4a869a48f62a5,
  pinned in backups/. Runs on 0x1535 unchanged (host-visible: GET_STATUS 48 bytes). Now the image to load next. Not loaded.
- 2026-10-19 14:35: Bode sweep 2 on the RC 1 kOhm / 15 nF, CH2 now on the R-C junction, wiring A (both clips on the ground
  rail), backups/bode/rc1_15n_clips_2.json: 30/30 valid, low-pass, +0.08 dB at 1 kHz, -3 dB at 11.3 kHz (expected 10.6 kHz), two CH2
  range splices. Above 30 kHz the slope is shallower than one pole and the phase flattens at -71 deg (300 kHz: -24.6 dB / -70 deg vs
  -29.0 / -88 expected): consistent with about 50-60 Ohm in the capacitor's return path, i.e. the stimulus current returning through
  the probe clips and R71 (100 Ohm) as bode-design 2.5 predicts (inference). Next: wiring B (DUT ground to the USB shell, clips open),
  then a thru calibration with both probes on the input.
- 2026-10-19 14:40: Bode sweep 3, same RC, DUT ground on a BNC shell instead of the clips (backups/bode/rc1_15n_bncshell.json):
  repeats sweep 2 within 0.5 dB / 1.8 deg at every point (300 kHz -25.1 dB / -68 deg): the BNC shell is the probe reference node,
  so it is wiring A. Owner has no reachable USB ground at the scope; options given: hub / extension socket shell, PC case, or a
  10 kOhm / 1.5 nF DUT to cut the return current ten times.
- 2026-10-19 14:42: fw 0.2.59 (UI step 4 on 0.2.58; merge of fw-ui4) built with GCC 10, MMU image sha256
  4ba894692d3cdbe92341bbd65cdd5cadd27c16cb68c5db3b8aeb2cddeca39736, nommu 24151796e999fc4105736f786e4eeae33c0948bacab2f6cc74af03ef329b082d,
  602,112 B, pinned in backups/. The complete makeover image: load this one next. Not loaded.
- 2026-10-19 14:45: Bode sweep 4, same RC, ground rail wired to the shell of a 30 cm USB extension between hub and scope
  (backups/bode/rc1_15n_usbgnd.json): identical to the clip-grounded sweeps within 0.4 dB (300 kHz -25.0 dB / -68 deg). Either the
  earlier ground wires (clips, BNC shell) were still attached so the return path did not change, or the extension shell is not bonded
  to USB GND. Next: remove every other ground wire; and the direct test of the return-error theory: CH2 tip on the ground rail with
  wiring A (bode-design 2.5 predicts about -21 dB rising with frequency if the 100 Ohm R71 path carries the return current).
- 2026-10-19 14:50: correction to sweep 4: the probe grounds were floating during it (owner), so the return current went
  through the USB extension and the result still matched wiring A: the R71 return-path explanation for the 4 dB / 18 deg deviation at
  300 kHz is refuted. New hypothesis: a component in phase with V1 added to CH2 (crosstalk / leak, the floor of bode-design 2.3),
  since the phase sticks at -70 deg while the magnitude runs high. Next: the floor sweep (CH2 tip on the ground rail) and the thru
  calibration (both tips on the input), wiring B via a second USB cable through the hub.
- 2026-10-19 14:55: Bode floor sweep, CH1 on the loaded input, CH2 tip on the ground rail, wiring B (rail on a second USB
  cable to the hub, probe grounds floating), backups/bode/floor_rc1_usbgnd.json: -78 to -90 dB at every point, CH2 0.03-0.14 mV rms
  (0.1 code), all points flagged near_floor as designed. Crosstalk is not the cause of the RC deviation; the floor is 25 dB better
  than bode-design 2.3 assumed. Next: thru calibration (both tips on the input), then the RC with it applied.
- 2026-10-19 14:58: Bode thru calibration, both tips on the loaded input, wiring B (backups/bode/thru_rc30.json, run
  thru_rc30_run.json): CH2/CH1 = +0.15 to +0.30 dB, 0.0 to 0.5 deg over 1 kHz to 300 kHz, 30/30 valid. The channels and probes match;
  the RC deviation (thru-corrected: 51 kHz -12.1 dB / -66 deg vs -13.8 / -78 ideal; 137 kHz -19.3 / -70 vs -22.2 / -86) is not a
  channel mismatch. Model fits (series R, parallel C, in-phase leak, skew) all leave > 1 dB / 3 deg rms: no simple fault explains it.
  Wiring A and wiring B gave the same H within 0.4 dB, so the return-path error predicted in bode-design 2.5 is not visible on a
  1 kOhm / 15 nF DUT. Next: the RC with the thru applied, then a 910 / 100 Ohm divider (flat -20.8 dB, 0 deg expected) to separate
  a measurement error from a capacitor property, then a second capacitor.
- 2026-10-19 15:02: RC sweep with the thru applied (backups/bode/rc1_15n_thru.json; an earlier run at 15:00 was taken with the
  resistor unplugged and is void): 30/30 valid, -3 dB about 11 kHz; the deviation is unchanged (112 kHz -17.9 dB / -70 deg vs -20.5 /
  -85 ideal; 202 kHz -22.4 / -70 vs -25.6 / -87). Next: resistive divider, then another capacitor.
- 2026-10-19 15:05: Bode divider 991 / 102 Ohm, wiring B, thru applied, CH2 fixed at 0.5 V/div
  (backups/bode/div_991_102_fixed05.json): -20.62 to -20.43 dB (expected -20.60), phase -0.2 to +1.4 deg over 1 kHz to 300 kHz,
  28/30 valid. The tool is proven flat; the RC deviation above 20 kHz belongs to the 15 nF ceramic capacitor (to be confirmed with a
  second part). BUG found: with CH2 set to 0.1 V/div at the start (auto range chose it, or --ch2-range 0.1) CH2 reads 48 uV rms on a
  68 mV signal (div_991_102_thru.json all points, div_point_01.json), while 0.2 and 0.5 V/div read -20.58 / -20.22 dB correctly
  (div_point_02.json, div_point_05.json); in the RC sweeps 0.1 V/div reached by a mid-sweep range switch (splice) read correctly.
- 2026-10-19 15:15: RC with the second 15 nF ceramic (backups/bode/rc1_15n_cap2_thru.json): same bend, 5 % larger C.
  Apparent shunt element from H (R = 991): cap 1 C_eff 14.7 nF at 1 kHz falling to 9.8 nF at 200 kHz, tan delta 0.03 rising to 0.29;
  cap 2 16.0 -> 11.3 nF, tan delta 0.04 -> 0.26. Both parts the same type, so this is either the dielectric (strong dispersion and
  loss, unusually large for a ceramic) or a measurement effect that a resistive divider cannot show. Discriminators queued: high-pass
  with the same parts, an electrolytic low-pass (different dielectric), and mcp-gen5's DC-saturation hypothesis for the 0.1 V/div
  blank (wiring B puts the DUT about -1.24 V below the probe reference VRF1; the divider output sat at about -1.15 V, the RC at
  -0.46 V).
- 2026-10-19 15:20: DC-overload test of the 0.1 V/div blank (RC on the post, wiring B, generator 10 kHz duty 3 % so the pin
  sits near USB ground): CH1 reads -1.12 V, i.e. the DUT is about 1.2 V below the probe reference VRF1 in wiring B. CH2 DC coupled:
  range 4 mean code -2.4 / std 0.13 / 99 % clipped, and 1000 offset counts (pwm 857 -> 1857) move the mean only 2 codes; range 5
  std 0.000, 100 % clipped; dcoffset 1500 -> E_BAD_ARG pwm out of range. CH2 AC coupled at the same DC: range 4 std 11.8, range 5
  std 23.7 codes, no clipping. Conclusion: a -1.2 V DC input saturates the 0.1 and 0.2 V/div front end ahead of the offset
  injection; the 0.1 V/div "bug" is wiring B's DC, not firmware. Since wiring A measured the same H as B, wiring A (DUT ground on
  the probe clips) becomes the recommended Bode wiring; mcp-gen5 asked to update the tool, docs and add an overload flag.
- 2026-10-19 15:25: high-pass with the same parts (15 nF cap 2 series, 991 Ohm shunt), wiring A (CH1 clip on the rail, USB
  ground removed), thru applied, backups/bode/hp_15n_cap2_thru.json: 30/30 valid; matches the prediction 1 - H_lp computed from the
  low-pass of the same capacitor within 0.14 dB and 1.0 deg at every point (1 kHz -20.23 dB / 81.3 deg vs -20.11 / 82.2 predicted,
  -20.56 / 84.6 ideal). Low-pass and high-pass describe one and the same lossy, dispersive capacitor, and the divider was flat:
  the Bode tool is accepted; the deviation is the cheap ceramic (owner: AliExpress parts, likely Y5V class). Bode bench closed;
  open follow-ups are in the tool (overload flag, wiring A as default) with mcp-gen5.
- 2026-10-19 15:28: fpga-gen6 pinned bitstream 0x1537 (0x1536 + pretrigger race fix + core-order fix): backups/
  al3_seg_0x1537_426a3784.bin sha256 426a3784883b82c9c9f42870584e64d1d082cdcb38c8b62db143e161e2633b17, padded 1 MiB image
  febc92997ee6605090d5581aabc9c240986fa82defdc24d40aca01566dbfe1f1. Not flashed; the flash session (programmer on J2, scope on
  battery, USB unplugged, owner present) now targets 0x1537 instead of 0x1536; rollback stays al3_fir_0x1535 padded 5ade9e75.
  fw 0.2.59 FEL load started 15:22: ENTER_FEL acknowledged, the boot ROM device appeared on busid 7-4 (behind the hub), not bound
  for WSL; owner asked to run usbipd bind --hardware-id 1f3a:efe8 once as admin; scope waiting in FEL.
- 2026-10-19 15:35: fw 0.2.59 (4ba89469) loaded into RAM over FEL (owner bound 1f3a:efe8 for usbipd by hardware id; the scope
  now hangs off the original socket, busid 9-2): #VER fw=0.2.59 proto=1.17 fpga=0x1535. First boot of a firmware that writes sector
  709 at boot. Owner walk-through of UI steps 1-4, readers and touch handles starts (firmware/AGENTS.md sections 29, 31-35).
- 2026-10-19 16:02: owner walk-through of 0.2.59: menu, chips, handles, CTRL page, spectrum, Settings, Diagnostics all work;
  polish list queued (UI polish batch 1). Wizard Zero level step FAILS: "too much noise" on both channels most runs, one channel
  passes sometimes (probes and USB unplugged as instructed). Host check with probes unplugged, USB plugged, 200 MS/s raw per core:
  std 0.5-1.1 codes (CH2 range 0: 1.7-2.1), raw A-B mean offset CH1 about 3.5 codes, CH2 0.5-1.5; limit is 3 codes RMS per core.
  The wizard computes something larger than the real noise: fw-cal4 dispatched (fw 0.2.60). COM10 flapped for about a minute after
  the USB replug (device seen, port not openable), then fine.
- 2026-10-19 16:06: owner: select, save and delete in Saved pictures and Saved waveforms work on 0.2.59 (new delete dialog).
- 2026-10-19 16:12: wizard noise, first reading (WRONG, see 16:18): with the calibration post still wired to the breadboard (probes unplugged), repeated
  200 MS/s captures show CH1 core B 19-30 codes RMS (60-code excursions) and CH2 core A 6-17 codes on ranges 0-1, far over the
  3-code limit; at 2 MS/s 0.1-0.5 codes. With only USB connected: 0.5-2.0 codes per core on all six ranges at both rates, generator
  on or off, std of capture means < 0.26, excursions < 10 codes. The wizard arithmetic is correct (fw-cal4 host check); the zero
  step must be run with nothing on the post, which the instruction did not say. fw-cal4 narrowed to the failure line with numbers
  and the instruction text. Owner: Log page works (scroll, save to card); Developer debug overlay flickers (1.00o5 per-pass redraw).
- 2026-10-19 16:18: correction: the owner's first wizard attempts also had nothing connected, and the Zero level step fails
  the same way with USB connected. So the environment is not the cause; the wizard measures something the host READ_RAW study does
  not (host: 0.5-2.0 codes per core on all ranges, both rates). fw 0.2.60 (fw-cal4, merge of its branch: failure line with range,
  rate, core, total and per-capture RMS; instruction names the post) built 27634587..., pinned backups/fw-0.2.60_27634587.bin, loaded
  into RAM at 16:18 (#VER fw=0.2.60). Owner to run Zero level and read the failure line.
- 2026-10-19 16:25: 0.2.60 failure lines, nothing connected, USB on: "CH1: range 4 at 200MSa/s: core B, 21.1 codes, 21.0 per
  capture", "CH2: range 1 at 200MSa/s: core A, 24.4 codes, 24.3 per capture". One core per channel reads about 21-24 codes RMS inside
  a single 200 MS/s capture inside the wizard, while READ_RAW from the host at 200 MS/s legacy at the same moment gives 0.5-2 codes on
  every core and range (GET_DECIM: filter 0, fpga_mode 0, fpga_shift 2). The same lanes (CH1 B, CH2 A) showed 19-30 codes from the
  host once today, right after the Bode run had left the FPGA in mode 225. Hypothesis under test by fw-cal4: the calibration
  conversion does not force the FPGA decimation / shift registers to legacy, so one byte lane is not an 8-bit sample. The UI filter
  was left at "off" by my study scripts between 15:55 and 16:25 and is restored to CIC2+FIR now.
- 2026-10-19 16:30: ROOT CAUSE of the wizard failure, and a general 200 MS/s fault on 0x1535: fpga-gen6's encode-phase race
  (fpga/al3/README.md Findings 3, fixed in 0x1537). Host test: alternate 0x0D divider 99 <-> 0 (and 1 <-> 0), READ_RAW 1000 samples at
  divider 0 after each change: CH1 core B bimodal, 15/30 and 12/30 trials at 15-29 codes within-RMS versus 1.1 otherwise, CH1 core A
  0.5 -> 1.6 in the same trials; CH2 clean in these 60 trials but flipped earlier (wizard: CH2 core A 24 codes). So about half of all
  full-rate captures after a rate change carry one metastable core on the current bitstream; the v4 class probe toggles the rate
  every 30 s, the wizard twice per range. The fw-cal4 arithmetic hypothesis and my stale-mode hypothesis are both ruled out.
  Actions: fw 0.2.61 (fw-cal4) adds a phase check-and-heal (re-toggle 0x0D) before full-rate measurements and after rate changes
  on 0x1532-0x1536, plus a Diagnostics notice; the 0x1537 flash session is the real fix and is now needed before the release.
- **FPGA flash written with bitstream 0x1537** (16:27-16:31, Sipeed FT2232D on J2, scope on battery, USB unplugged): probe
  W25Q80DV ef 40 14; pre-write double read backups/20261019-162658-pre-0x1537.bin sha256 5ade9e75... = the pinned 0x1535 padded image
  (rollback image confirmed); wrote backups/al3_seg_0x1537_426a3784_padded.bin: 256 sectors erased, 1105 pages programmed, write
  verified febc9299..., independent verify OK over 1048576 bytes. Power cycle pending; then #VER must show fpga=0x1537 and the
  encode-phase host test (alternate 0x0D, READ_RAW at divider 0) must stay clean.
- 2026-10-19 16:33: power cycle: the card's 0.2.25 boots FPGA 0x1537 (#VER fpga=0x1537, caps 0x7C0B); fw 0.2.60 FEL-loaded,
  caps features 31 (SEQ bit set). Encode-phase test on 0x1537 (alternate 0x0D 99/1/3999 <-> 0, 40 trials each, READ_RAW 1000 at
  divider 0): CH1 both cores clean in all 120 trials (max 1.16 codes), but CH2 core A bad in 35, 36 and 11 of 40 trials at 32-34
  codes within-RMS. 0x1537 made the phase deterministic and the second ADC chip now mostly sits in the wrong one. Flag study next.
- 2026-10-19 16:40: 0x1537 follow-up: static divider-0 captures clean 20/20; after a 0x0D write 99 -> 0 the capture is clean at
  0 ms delay (0/20) but bad at 5 ms (7/20), 20 ms (18/20), 50-300 ms (13-16/20), CH2 core A only, and persistent until the next
  write (a second capture stays bad). So a write in the scope's own loop between the host write and the capture sets the bad phase
  deterministically on 0x1537 for ADC chip 2; on 0x1535 it was random on chip 1. SET_DECIM flags do not matter; divider 1 static
  shows CH2 A 55 codes with constant B lanes (layout of that mode, parked). fpga-gen7 dispatched (0x1538), fw-cal4 redirected
  (check-retry heal + bus write instrumentation). Note: an accidental host STOP (sequencer stop) was sent at 16:38; harmless.
- 2026-10-19 16:45: wizard on 0x1537 / 0.2.60: CH1 zero step passes every time, CH2 fails every time with "the input is not at
  0 V". Host READ_RAW right after (8 captures after 0x0D 99 -> 0, all clean): CH2 core means A 128.7 / B 132.0 (A-B 3.3 codes; on
  0x1535 today 129.8 / 130.1 and 130.4 / 130.4), CH1 A 131.0 / B 127.7 unchanged. Chip 2's cores arrive in the other order on
  0x1537, so the stored per-core zeros land on the wrong cores. Both agents informed (fw-cal4: robust zero step, check-retry heal,
  write log; fpga-gen7: deterministic order equal to 0x1535 plus a status read, 0x1538). Bench paused for the owner.
- 2026-10-19 16:48: fw 0.2.61 (merge 65aded6, sha 4593c43f..., pinned backups/fw-0.2.61_4593c43f.bin, protocol 1.18 row 57:
  full-rate encode check-and-retry after 0x0D writes, counters 62/63, events 21/22) FEL-loaded on 0x1537. Experiment for fpga-gen7:
  divider 99 held 30 s, one 0x0D=0 write, then 1221 host captures in 60 s with no other writes (event log: only the arm's 0x01
  resets, caller host, every 48 ms). CH2 core A bad (14-30 codes) in about 40 % of captures in every 4 s bucket, randomly
  interleaved with clean ones (median 0.50), CH1 clean, CH2 A-B mean a constant -3.0 codes. So on 0x1537 chip 2's encode phase is
  re-resolved at every sampling reset with about 40 % failure: a marginal flop (fpga-gen7: encA setup slack -0.209 ns at TT 85C),
  not a write race and not a thermal drift within a minute. 0.2.61's check does not fire here because no 0x0D write precedes the
  arms: the check must run on every full-rate arm. 0x1538 (stateless encode from the write clock + self-check flag) in progress.
- 2026-10-19 16:58: fpga-gen7 interim: 0x1538 RTL builds with 0 timing errors on clk_200MHz (+0.22 ns), encode stateless from the
  write clock (B earlier, the 1.00o5 convention; 0x1537 had chip 1 reversed and chip 2 marginal), new read 0x33 self-check
  (bit 0 sticky fail since the last 0x01, bits 3:2 = 01 convention, 7:4 pin checks, 0xF6 good); FPGA.bin sha256 0c24ecf6...; full
  testbench run pending, then pinning. Calibration taken on 0x1537 must be redone; 0x1535 calibration fits 0x1538. fw-enc dispatched.
- 2026-10-19 17:40: bitstream 0x1538 pinned (fpga-gen7): backups/al3_seg_0x1538_0c24ecf6.bin sha256
  0c24ecf6ff55bc170ce6d7acd1ee4f8afbaf97d73b36e0b7a27a92639d1dbbbd, padded backups/al3_seg_0x1538_0c24ecf6_padded.bin sha256
  2fc7ea5b2fc925157c02f3f492ee3ac17b6154d7ed27e2c20c2d26e855ac1087; committed as fpga/al3/dist/al3-0x1538.bin (unbenched). Not
  flashed. Flash session plan: as 16:27 (programmer on J2, battery, USB out); pre-write dump must equal the 0x1537 padded image
  febc9299...; rollback to 0x1535 padded 5ade9e75... or 0x1537 febc9299...; then the README section 8 checks on fw 0.2.61 (no-write
  1000-capture run clean, CH2 A-B mean near 0, wizard zero step on both channels) and 0x33 once fw 0.2.62 is loaded.
- **FPGA flash written with bitstream 0x1538** (17:43-17:46, Sipeed FT2232D on J2, scope on battery, USB unplugged): JEDEC ef 40 14,
  pre-write double read backups/20261019-174328-pre-0x1538.bin sha256 febc9299... = the pinned 0x1537 padded image; wrote
  backups/al3_seg_0x1538_0c24ecf6_padded.bin: 256 sectors erased, 1105 pages programmed, write verified 2fc7ea5b..., independent
  verify run after. Power cycle pending; then fw 0.2.64 (merge ed06e7e, GCC 10 sha 8caacb31...) FEL-load and the README section 8
  checks (0x33 = 0xF6 per capture, no garbled core over 1000+ captures, CH2 A-B near 0, wizard zero step both channels).
- 2026-10-19 17:50: power cycle: the card's 0.2.25 boots FPGA 0x1538; fw 0.2.64 FEL-loaded (#VER fw=0.2.64 proto=1.19 fpga=0x1538).
  Encode checks on 0x1538: rate toggles 99/1/3999 <-> 0, 40 trials each: 0 garbled captures, max core std 0.93; 60 s no-write run:
  1233 captures, 0 garbled, max std 1.44. GET_HEALTH 88..103: mode 2 (0x33 flag), last 0x33 0xF6, boot 0x33 0xF6 convention OK;
  112 captures flagged by 0x33 and re-taken out of 5798 checked (event log read next). Raw core means: CH1 A-B +5.0 (3.3 on 0x1535),
  CH2 A-B -2.6 (0.5 on 0x1535; -3.3 on 0x1537): the per-core offsets differ from the 0x1535 state, so the unit needs a recalibration
  on 0x1538 (the wizard measures per core). Wizard run next.
- 2026-10-19 17:55: wizard Zero level on 0.2.64 / 0x1538, nothing connected: still fails every time, now "CH1: too much noise,
  range 1 at 200MSa/s: core B 7.2 codes, 7.2 per capture" (6-8 across runs) and "CH2: the input is not at 0 V" (clip). Host READ_RAW
  at the same range and moment: 0.6-2.0 codes per core, no clipping, 0x33 0xF6. So the wizard's own conversion setup differs from
  every other path (offset PWM / relay / settle on the coarsest range suspected); the four-screen wizard has never completed on
  hardware; the 0.2.48 one-screen flow did. fw-cal5 dispatched (fix + host-triggerable dry run). Bench paused.
- 2026-10-19 18:20: fw-cal5 diagnostic image 0.2.67 / 1.21 (f17e2d70, backups/fw-0.2.67-calprobe_f17e2d70.bin) loaded; CAL_PROBE
  runs the wizard's own range measurement from the host. With the probes still on the RC filter (generator off): CH1 range 0 and 1
  pass all 8 points (full zero A 131 rms 1.4, B 128 rms 0.9, no clipping; settle 0 and 4 identical), range 5 reads all zeros (probe on
  the post, not a bug); CH2 never ran (E_BUSY deferred request after each CH1 probe). So the wizard measurement code is sound from the
  host while the on-screen wizard fails: screen context suspected (Cancel touch poll during the measurement, display writes, re-arm).
  Owner asked to disconnect the probes for a clean rerun. fw-polish2 dispatched 18:10 (RUN/STOP action button + host state follow,
  spanner icon, sine with axes, time/div snap not applying the manual path, sun icon = brightness slider, wizard Step M of N, Calibration
  tab button row).
- 2026-10-19 18:35: ROOT CAUSE of the wizard failure found (fw-cal5 diag 0.2.67 b67219c, 7b881451, loaded; probes and post
  disconnected). CAL_PROBE mode 0 and mode 1 (the wizard's exact on-screen path from the host): CH1 passes all ranges; CH2 fails at the
  full-rate zero point of range 0 with core A [124..195] rms 11 and core B [127..255] clip 24-89 while the slope points +-40 codes
  away are clean. Prestrobe (extra MCU strobe before each read) makes it worse, not better. READ_RAW dcoffset sweep at 200 MS/s: CH2
  both cores show outliers (A max 195, B max 255 with up to 564/6000 rail samples) only while the mean is 126..133, clean at every
  other mean; CH1 core A once (mean 131, max 213); gated rates clean everywhere. Conclusion: the FPGA's MCU readout path mixes bits
  of two successive bytes where many bits flip (0x7F/0x80), lane- and build-dependent (0x1535 passed this morning, 0x1537/0x1538 fail;
  fw-cal5: BRAM dob and o_mcu_data on the unconstrained i_mcu_clk). The wizard parks its zero at exactly 128, the worst code. fpga-gen8
  redirected: register the output byte and constrain the MCU clock as the first change of 0x1539; fw-cal5: zero measured off-centre
  as a workaround for all bitstreams, CAL_PROBE kept as protocol 1.21.
- 2026-10-19 18:50: fw 0.2.67 (fw-cal5 workaround: zero as two points +-12 codes; CAL_PROBE 0x0402 kept, protocol 1.21) merged,
  pinned backups/fw-0.2.67_086b4738.bin, loaded; CAL_PROBE both all, probes off: still NOISE on CH1 ranges 1 and 3 (core A rms 3.7-3.8,
  [118..159]) and CH2 ranges 0, 2, 4 (core B rms 3.3-4.6, [115..159]); the other lanes clean (0.5-1.4). So the bit mixing also hits
  the means near 116 and 140 (0x77/0x78, 0x8F/0x90 boundaries): no firmware workaround; the wizard waits for fpga-gen8's readout
  fix (0x1539), which 0.3.0 ships as the recommended bitstream. fw 0.2.68 (polish batch 2: RUN/STOP action button following host
  captures, spanner icon, sine with axes, snap through the manual path, one brightness value, wizard Step M of N, Calibration tab
  button row, eight-row main menu with the Bode row) merged debd123, pinned backups/fw-0.2.68_8568021d.bin, loaded 18:52.
  Owner: the mode is named "Frequency response" (ux-bode informed; the implementation batch renames the row).
- 2026-10-19 19:48: fw 0.2.69 (Frequency response UI from the design, slider row, ten samples per division) merged, pinned
  backups/fw-0.2.69_8babde3b.bin, loaded on 0x1538. Owner walk-through: firmware/AGENTS.md section 45 (RC and divider).
- 2026-10-19 19:52: bitstream 0x1539 pinned (fpga-gen8): backups/al3_seg_0x1539_f36c1f7a.bin sha256
  f36c1f7a0ada876311f5ad5d600f4cb44523165748e96c25bed160a66388ea64, padded backups/al3_seg_0x1539_f36c1f7a_padded.bin sha256
  22db7b9128e2a12149e5c4f44bc3cb5c22338b40b4e888e97fe9c1a3f571a6b8; in fpga/al3/dist (unbenched), 0x1538 marked withdrawn. Cause of
  the bit mixing (TD report with i_mcu_clk constrained): o_mcu_data loaded on the rising i_mcu_clk edge, 9.4-10.9 ns pad delay per bit,
  the MCU read came too early for the slow pads; fix: byte loaded on the falling edge (strobe start) and held, address a falling-edge
  copy of the pointer, i_mcu_clk 0 setup / 0 hold errors, tb_mcuread all 256 codes clean. Not flashed. Flash session: pre-write dump
  must equal the 0x1538 padded image 2fc7ea5b...; rollback 0x1538 or 0x1535; checks fpga/al3/README.md section 9 on fw 0.2.69, then
  the wizard; calibration on 0x1537/0x1538 must be redone.
- **FPGA flash written with bitstream 0x1539** (20:03-20:06, Sipeed FT2232D on J2, scope on battery, USB unplugged): JEDEC ef 40 14,
  pre-write double read = the pinned 0x1538 padded image 2fc7ea5b...; wrote backups/al3_seg_0x1539_f36c1f7a_padded.bin: 256 sectors
  erased, 1105 pages programmed, write verified 22db7b91..., independent verify OK. Power cycle pending; then fw 0.2.69 FEL-load,
  the 128-boundary probe (CAL_PROBE both all) and the no-write captures, then the wizard.
- 2026-10-19 20:25: power cycle: the card's 0.2.25 boots FPGA 0x1539; fw 0.2.69 loaded. CAL_PROBE both all: all 12 range
  measurements OK (zero points at codes about 116 and 140; CH2 A range 0 rms 2.56, a few excursions to 153-157). 30 s no-write run at
  mean about 130: 218 captures clean (max std 1.1). BUT the dcoffset sweep at 200 MS/s range 0 shows the fault moved, not fixed: CH1
  core A and CH2 core A garbled while their means are 128..145 (CH1A std 9.9-56.7 with 0/255 hits at dco 0..8; CH2A std 15-90 with up
  to 396/6000 rail samples at dco 0..20), CH2 B mildly raised (2.2-3.6), CH1 B clean; all cores clean below 127 and above 146. The
  outlier values (0x00, 0xFF, 0x30, 0x41 at a quiet 0x80..0x90 input) are old/new bit mixes of successive samples; the affected lanes
  change with every build (0x1535 clean, 0x1537 CH2 A random, 0x1538 CH2 A+B, 0x1539 both A cores). New reading: the FPGA samples the
  ADC data pins inside their transition window (AD9288 output delay vs the capture edge; board-level, invisible to 0x33), not the MCU
  readout; fpga-gen8 asked for a datasheet-constrained capture edge or a per-core programmable capture phase with firmware training
  (0x153A). The wizard may pass on 0x1539 since its zero points avoid the bad band; normal 200 MS/s traces near those codes are still
  wrong on the A cores.
- 2026-10-19 20:30: the four-screen calibration wizard passed on 0x1539 / fw 0.2.69 (Zero level with USB out, CH1 gain, CH2
  gain, Save): the first full calibration on the new wizard and the first valid one since 0x1535. Owner: "the new wizard is great".
  Owner decision: remove the off-centre zero workaround of 0.2.67 and measure at code 128 again so a bad bitstream fails loudly
  (fail fast); keep the readout-fault wording in the failure line. Scheduled with the batch after 0x153A lands.
- 2026-10-19 20:43-20:51: owner wired the RC low-pass (post -> 1 kOhm -> node -> 15 nF -> clips, CH1 on the post side, CH2 on the
  node) and reported that AUTO SET in Frequency response "kept running, could not stabilise". Host checks over COM10 with the view
  open (display mode 5, sweep_id 0 since boot): a device single sweep 100 Hz..1 MHz / 50 points / Settle Normal via BODE_CTRL took
  4.0 s (header estimate 3.7 s for the host's settings; the first BODE_STATUS had reported 245.7 s for "the settings in effect"
  while showing the same 100 Hz..1 MHz / 50 / Normal: the header and the estimate describe different settings when no sweep has
  run, queued). REPEAT 1 kHz..200 kHz / 20 points ran 0.45 s per sweep, STOP from the host ended it after the sweep (state stopped,
  reason 6, view bit 1 cleared, run_state 0). Both work. The table was wrong, though: every row ch1_range 0 and ch2_range 0 (5 V/div),
  flags clipped + ch1_low on all 50 points, ch2_small above 10 kHz, outliers (+12.5 dB / +57 deg at one point), the floor flat at
  -30 dB from 400 kHz. The host sweep of the same filter (fnirsi1013d-bode 100 Hz..1 MHz, 25 points) was clean: 25/25 valid, 0.13 dB
  reference, -3 dB at 10.33 kHz, -45 deg at 12.3 kHz, -34.0 dB at 1 MHz, CH2 0.5 -> 0.2 -> 0.1 V/div. Cause, measured directly
  (1 kHz square from the post through the filter, 200 MS/s, 1500 samples per core): an A core returns rail values and jumps > 40
  codes whenever its samples lie in codes 128..145 (CH2 A at 5 V/div: 44 rail samples, 642 jumps, std 58.9 vs 2.2 on B; CH1 A at
  1 V/div crossing the band: 13 rails, 74 jumps, B 0 / 0), and is clean when they stay outside it. The device engine counts one rail
  sample as a clip (bode_dsp.c lo < 0.5 / hi > 254.5 codes), backs both channels off to 5 V/div in the pre-scan, and there the whole
  3 V signal sits inside the band: every point flagged (grey dots on the screen, which is why the owner saw no colours), the curve
  jumping between repeated sweeps. The host tool ranges by amplitude first and tolerated the few bad samples. So the on-device
  Frequency response is correct in its mechanics and blocked by the 0x1539 capture fault until 0x153A; fpga-gen8 got the data
  (the bad codes are 0x80..0x91: bit 7 set, bits 6..5 clear, A cores only). Scope state after the checks: generator off, both
  channels 1 V/div DC, the Frequency response view still open, no sweep running.
- 2026-10-19 21:09-21:11: fw 0.2.70 (READ_SCREEN merged and numbered) built with GCC 10, pinned backups/fw-0.2.70_be2518d8.bin
  (sha256 be2518d833d7eb20f54197b131e25d6b448da80c689a68d180d3edd3f4f62aac), loaded hands-free with fnirsi1013d-flash (ENTER_FEL,
  busid 9-2, 10.4 s end to end), `#VER fw=0.2.70,proto=1.21,fpga=0x1539`, caps 0x7FC4B (SCREENSHOT bit 6 set). T9 passed: a full
  800x480 screenshot in 0.22 s, colours right (CH1 yellow, CH2 cyan, ACQ green, STOP red), bars and grid intact, no tearing
  (scratchpad screen_2111_fw0270_t9.png). The FEL reload left the scope in the Scope view at 1 MSa/s, 1 V/div both channels,
  nothing on the inputs ("No trigger" warning). Noted on the screenshot: the Scope's record slider already draws the whole record
  as a thumbnail in its track (stock behaviour), so the slider-overview design's Scope part exists in substance; the batch should
  align the two modes with it rather than add a second drawing.
- 2026-10-19 21:12: GET_HEALTH on fw 0.2.70 / 0x1539 (the "1.19 / 1.20 on the unit" host item): encode check mode 2, boot and last
  0x33 = 0xF6, 256 arms checked, 0 bad, 0 recovered; USB state 3 flags 0x1B, 14 power-ons, 105 minutes of life, no restart record.
  Of that item: scope_health done, scope_cal_probe done 20:25, the device-vs-host Frequency response comparison is blocked by the
  A-core capture fault until 0x153A (20:51 entry); the poll interval and elapsed_ms (445 ms per 20-point sweep, estimate 935 ms)
  were checked at 20:44.
- 2026-10-19 21:26-21:28: fw 0.2.71 (fw-freq2 merged: Frequency response follow-ups, Spectrum slider, grid rule, Trace style,
  READ_BODE reason 8) built with GCC 10, pinned backups/fw-0.2.71_1264e26c.bin (sha256
  1264e26c75fb4ee5046b233fe5a9956e40cc68cdb29272f5bdd640949361dc72), host test suite green on the merged tree, FEL-loaded 21:26
  (`#VER fw=0.2.71,proto=1.21,fpga=0x1539`). BODE_OPEN from the host: the header now reports the view's own settings, 1 Hz ..
  10 kHz / 50 / Normal, estimate 245.7 s, which is where the 20:43 "245 s" came from (the owner's slider play had left Start at
  1 Hz; the 20:43 header showed the engine defaults instead, fixed by 0.2.71). A host-settings sweep 100 Hz .. 1 MHz / 50 ran
  to the end in 4.3 s (reason 0): this time the pre-scan landed CH1 at 2.5 V/div and CH2 at 2.5 .. 0.5 V/div, so the readout-fault
  stop (reason 8, which needs 5 V/div) did not trigger; 17 clean points, the rest ch1_low / ch2_small / clipped, the curve drawn in
  colour and joined where valid and as grey dots where flagged, "CH1 level low: the DUT loads the 1 kOhm source" at the top
  (misleading here: the cause is the fault-driven ranging, not the DUT), cards -3dB 9.355 kHz, Level +1.55 dB (host: 10.33 kHz,
  +0.13 dB; the 13-code CH1 fundamental at 2.5 V/div explains both). The screenshot (scratchpad screen_2135_fr_after_run.png) shows
  the new grid (decades with 2 and 5 lines), the ACQ chip "Set by / the sweep", the generator chip on the last tone, the slider
  window over 100 Hz .. 1 MHz. The on-device mode stays blocked by the capture fault for real use; the walk-through of section 47
  waits for 0x153A. Scope state: Frequency response view open, STOP, generator off.
- 2026-10-19 21:47: fw 0.2.72 (fw-bus merged: dummy strobe in every FPGA readout, EXT shift, arm order, encode check by data, core
  order from 0x33; protocol 1.22) built with GCC 10 and pinned backups/fw-0.2.72_7f85890e.bin (sha256
  7f85890e4bb312cfcc288b00613b9cdd9289d32d8650c34c63d8593cf6fb9950); host test suite and 100 protocol vectors green. Not loaded
  yet: the scope answered E_BUSY at 21:33 (a menu open on the unit). Bench list B1-B8 in firmware/AGENTS.md section 48; B1, B2
  (READ_TRACE vs READ_RAW edge), B3 and B6 (rate toggles, GET_HEALTH bad count) are host-side.
- 2026-10-19 21:53: fw 0.2.73 (fw-over merged on 0.2.72: slider-track overview in Scope, Spectrum and Frequency response, tap to
  jump) built with GCC 10 and pinned backups/fw-0.2.73_ad7b7e24.bin (sha256
  ad7b7e24dc929dc180adaa6ed72fff11d5c0a0e7fa0288dc063546e30b4c59fa); host tests and vectors green. To load when the scope is
  free of menus; then B1 / B2 / B6 of section 48 and the overview walk-through of section 49 (pass_ms first).
- 2026-10-19 22:07-22:09: fw 0.2.73 loaded from the boot menu's FEL (the MCP server held COM10 after this session's tool calls, so
  ENTER_FEL could not be sent; `fnirsi1013d-flash --no-fel`), `#VER fw=0.2.73,proto=1.22,fpga=0x1539`. Section 48 host-side checks:
  B1 caps 0x7EC4B, bit 12 (order unverified) clear, bit 11 (read lag unverified) set, GET_LINK_STATS 68 entries ending in
  adc_fullrate_by_data: pass. B6: 40 rate toggles 200 MSa/s <-> 100 MSa/s with a host capture each: enc_checked 128 -> 231,
  enc_bad 0, recovered 0 (0.2.64 re-took 112 of 5798): pass. B2 (generator 1 kHz on CH1, trigger CH1 rising, pretrigger 200 of
  1000): at 1 MSa/s plain the READ_RAW and READ_TRACE mid-level crossings agree within 0.55 sample over five captures (0.2.69: two
  samples apart); with CIC2+FIR at 1 MSa/s identical to the hundredth; at 200 MSa/s the crossings disagree by up to 19 samples
  both ways, which is the 0x1539 A-core fault on the slow 1 kHz edge (codes 128..145 crossed on every edge), not the lag: repeat
  after 0x153A. B2 accepted at the decimated rates; caps bit 11 stays until the 200 MSa/s repeat. Scope view at 1 MSa/s, 100 us/div,
  1 V/div both channels: GET_STATUS pass_ms 18 (max 111 at start-up) with the overview drawn (0.2.48 bench: 15 ms median without
  it; the section 49 limit is +1 ms, so measure again against a build with the envelope skipped before deciding on the cache).
  Screenshot scratchpad screen_2208_scope_overview_1kHz.png: the track holds both channels' envelopes over the whole record in
  their colours. B3, B4, B5, B7 and the overview walk-through (section 49) wait for the owner.
- 2026-10-19 22:11-22:20: owner on 0.2.73: the two-line window of decision 6 "doesn't work quite well"; decision 7 (window box with
  margin back, track top line removed) built by fw-over2 and merged as fw 0.2.74 (pinned backups/fw-0.2.74_11f6d6dc.bin, sha256
  11f6d6dce031a0a6c9a0709ac0b8a2e463e68446f9c42e6bf2dc20c807586ee2; track y 452..471, 20 rows, box 18 px, 16 rows inside),
  FEL-loaded 22:19 by ENTER_FEL (`#VER fw=0.2.74,proto=1.22,fpga=0x1539`). Owner findings on 0.2.73: tap to jump in the Scope's
  track does nothing, works in Spectrum; a tap on the left of the Scope plot no longer zooms out while the right side zooms in
  (the ten-samples-per-division viability or the record length blocking the slower time/div at the current rate; owner wants the
  zoom to step the sample rate and time/div together "so it pretty much just works"); AUTO SET in Frequency response worked for
  some points and failed for about 60 % (the 0x1539 A-core fault through the pre-scan ranging, as at 20:51; recheck on 0x153A).
- 2026-10-19 22:33-22:39: fw 0.2.75 (Spectrum legends removed) pinned backups/fw-0.2.75_4b9907dc.bin (sha256
  4b9907dcc7ffc47042765f541118f9e1b35747800e273eed130b1e53b1a394fa, never loaded); fw 0.2.76 (fw-zoom: Scope slider tap at
  touch-down, the zoom ladder) pinned backups/fw-0.2.76_bf551df4.bin (sha256
  bf551df436311cf039c8450147af08bea663881f2e22f368733c3d96770e63d7), FEL-loaded 22:39 by ENTER_FEL
  (`#VER fw=0.2.76,proto=1.22,fpga=0x1539`). Owner walk-through: firmware/AGENTS.md section 50 (tap in the track, zoom across
  the range at 3k / 6k / 16k memory with a tap at each end, hand-picked rate then zoom, ACQ time/div boxes, STOP notice, roll).
- 2026-10-19 22:45-22:52: owner on 0.2.76: the Scope track tap works, the zoom ladder "seems to work great". Two reports: (1) after
  switching Filter from Off to a filter and back to Off, the memory depths above 6000 stay hidden in the ACQ menu (queued). (2) At
  200 MSa/s and 2 us/div on the RC filter, CH2 shows large noise at the same point of the pulse. Host dump at the owner's settings
  (both channels 1 V/div DC, divider 0, filter off; scratchpad dump_2245_200MSs.npz, screen_2245_ch2_noise.png): CH2 core A has
  15 rail samples and 175 jumps > 40 codes among 1500 samples (values 254, 48, 246, 16, 252 inside a 118..137 stretch), CH2 core B
  none; CH1 (sitting at 142..159, above the band) clean on both cores. It is the 0x1539 A-core capture fault: the noise sits where
  the CH2 level crosses codes 128..145 on the pulse, so it is always at the same point. Not firmware; waits for 0x153A.
- 2026-10-19 23:16: owner switched the scope off for the night (screen burn-in caution). State at power-off: FPGA 0x1539 in the
  config flash, fw 0.2.76 in RAM (gone at power-on: the card boots 0.2.25, so tomorrow starts with a FEL load of the latest
  pinned image through ENTER_FEL), calibration saved from the 20:30 wizard run, the RC low-pass still wired to the post. Tomorrow
  first: FEL-load the newest firmware (0.2.77 if fw-spzoom has landed), then the walk-throughs of sections 47 (Frequency response
  on a sound bitstream once 0x153A is flashed), 49 (overview), 50 (zoom ladder, owner: works), 52 (Spectrum ladder); the fpga-gen9
  report and the 0x153A flash session (owner present, FT2232D, battery, USB unplugged); the hardware averaging default decision.
  Agents still running overnight: fpga-gen9, fw-spzoom.
- 2026-10-19 23:32: fw 0.2.77 (fw-spzoom: Spectrum zoom ladder) merged, built with GCC 10 and pinned backups/fw-0.2.77_1ffafab7.bin
  (sha256 1ffafab7096a772f71e04425751319fc78481dfc61d61deaf0373f33c9d55977); host tests green; not loaded (scope off). This is the
  morning's FEL image unless fw-yield lands first.
- 2026-10-20 00:02: fw 0.2.78 (fw-yield merged on 0.2.77: host commands leave the modal screens, protocol 1.23) built with GCC 10 and pinned
  backups/fw-0.2.78_195afb82.bin; firmware host tests, 102 vectors, mcp ruff and 428 pytest green on the merged tree. This is the
  morning's FEL image. Bench list: firmware/AGENTS.md section 53 (each menu / fold-out / dialog / screen closes on a host command
  with the note; wizard mid-measurement keeps E_BUSY detail 3; keypad half-typed keeps the old frequency; counters 68-69).
- 2026-10-20 00:30: bitstream 0x153A (fpga-gen9, Fable) merged (a0a1bfe) and pinned: backups/al3_cap_0x153A_75e0ef21.bin sha256
  75e0ef21526248ecc2a7a47231abddf59b77d8075917c460a9630ee97cf3ddde (= fpga/al3/dist/al3-0x153A.bin, status unbenched), padded
  backups/al3_cap_0x153A_75e0ef21_padded.bin sha256 77672b5a38f2225e98cdecd7de012850121b6343525392d973e41d39b42719d0. Review
  verdict: gen8's diagnosis confirmed by re-timing the placed 0x1538 / 0x1539 designs with zero I/O delays (0x1539 sampled the
  A pins 0.8..1.4 ns inside the AD9288 transition); gen8's fix kept but corrected: the A cores are encoded on the falling write
  clock edge, so their window moves with the sample rate (gen8's stages were right at 200 / 100 MSa/s and inside the A transition
  at 50 MSa/s), and the 6:1 pick was a half-cycle path with +0.12 ns slack. 0x153A: A cores captured against the falling edge, B
  against the rising one, stage registers on wire-only 2.5 ns paths, B first by T/2 at every rate by construction, >= 1.33 ns on
  both sides of the worst-case window at the power-on stages (0x08E4), a wrong stage garbles loudly and the 0x36 counters show
  it. Timing: clk_200MHz +0.051 ns (Atlan4's divider-compare path, a placement lottery), the other clocks clean. Testbench: eleven
  steps all pass on seeds 12345 and 3, incl. the new tb_adccap (2274 checks: AD9288 transition model, pad delay 2..12 ns, both
  corners, both B models, all rates). Flash session (owner present, FT2232D on J2, battery, USB unplugged): the pre-write double
  read must equal the pinned 0x1539 padded image 22db7b91...; write the 0x153A padded image; rollback = 0x1539 padded; then
  fw 0.2.78 FEL-load and fpga/al3/README.md section 10 items 1..7 (0x36 zero at a quiet input, the dcoffset sweep 126..146 at
  200 / 100 / 50 / 20 MSa/s, the RC Frequency response, the wizard at zero 128, training vs 0x08E4 and the reference-bit test,
  EXT offsets, warm run). Firmware side (fw-cap dispatched): detect 0x153A, EXT offsets (+4 words at 200 MSa/s, +3 divided;
  K >= 1 +40 ns), 0x53 bits 3/4 inert, training via 0x63 / 0x64 / 0x36 at code 128 stored with the calibration, 0x36 in
  GET_HEALTH and Diagnostics, wizard zero back at 128.
- 2026-10-20 01:09: fw 0.2.79 (fw-cap: 0x153A capture stages, training, self-check, EXT offsets; wizard zero at 128; protocol 1.24)
  merged, built with GCC 10 and pinned backups/fw-0.2.79_7188c5cf.bin (sha256
  7188c5cf84670fbde11da2f4790cd84ba1b952df45afeb475a4fb3ba90bb42ad); firmware host tests, vectors, mcp ruff and 443 pytest green.
  Morning order: FEL-load 0.2.79 on 0x1539 first (the wizard must now fail loudly there: readout fault near 128, the fail-fast
  check), then the 0x153A flash session, then firmware/AGENTS.md section 54 / fpga/al3/README.md section 10 items 1..7.
- 2026-10-20 09:59-10:02: scope on (card boots 0.2.25), fw 0.2.79 FEL-loaded by ENTER_FEL (`#VER fw=0.2.79,proto=1.24,fpga=0x1539`),
  caps 0x7EC4B (no CAPTURE_STAGES on 0x1539, right), GET_HEALTH layout 2, 124 bytes. Fail-fast check: the wizard's zero step at
  code 128 on 0x1539 fails on both channels: CH1 "ADC readout fault", CH2 "too much noise" (the A-core garbage classified as noise
  there; both loud, as intended; minor: the noise classifier could name the readout fault when rail samples are present, queued
  later). Next: the 0x153A flash session.
- **FPGA flash written with bitstream 0x153A** (2026-10-20 10:02-10:03, Sipeed FT2232D on J2, scope on battery, USB unplugged):
  JEDEC ef 40 14; pre-write dump backups/20261020-100247-pre-0x153A.bin and the tool's double-read
  backups/20261020-100301-pre-write.bin both sha256 22db7b91... = the pinned 0x1539 padded image; wrote
  backups/al3_cap_0x153A_75e0ef21_padded.bin: 70 sectors erased, 1105 pages programmed, write verified sha256 77672b5a...
  Rollback: the 0x1539 padded image. Next: power cycle, fw 0.2.79 FEL-load, `#VER fpga=0x153A`, caps bit 19, then
  firmware/AGENTS.md section 54 / fpga/al3/README.md section 10 items 1..7.
- 2026-10-20 10:04-10:08: power cycle, fw 0.2.79 FEL-loaded on 0x153A (`#VER fpga=0x153a`, caps 0xFEC4B with bit 19, health cap
  value 0x08E4 = the power-on stages). A USB write failure (bus reset at 25.5 s after boot, Windows side; the firmware kept
  running, no restart record) killed the first sweep; rerun: dcoffset sweep at range 0, means 113..141, at 200 / 100 / 50 /
  20 MSa/s (scratchpad dco_sweep_153A.log): at 100, 50 and 20 MSa/s every core clean (max std 0.4..1.0, CH2 2.2..4.0 at
  50 / 100 MSa/s in the band without rails or jumps); at 200 MSa/s CH1 core A still garbles at means 127..135 (dco 8: std
  40.7, 130 rails, 279 jumps; dco 12: 7 rails, 14 jumps), CH1 B clean, CH2 A / B std up to 3.2 in the band, no rails. So the
  power-on stage 4 is wrong for CH1 A on this unit, as the design allows, and the self-check saw it: GET_HEALTH counts
  (1A 1842, 1B 0, 2A 233, 2B 69), 563 of 838 captures flagged. CAL_PROBE mode 1 (the zero step with training, both
  channels): CH1 OK (a clean stage found), CH2 READOUT (no clean stage for CH2 A and B at stages 2..5, event 23 a = 0x1C
  = 0x10 | cores 2A 2B, value left at 0x08E4). During the sweeps event 23 flagged CH2 A and CH2 B alternately with an excess of
  9..61 outliers: the CH2 chip has mild outliers on both cores in the band (std 3 vs 0.5), which the relative self-check
  reports as a flip-flop and which makes "count 0 = clean" unreachable for CH2. Next: a PC_DEBUG image with raw 0x63 / 0x64
  writes to map the 0x36 counts per core, stage and reference bit across the band, before deciding whether 0x153A needs
  another stage grid, a looser clean criterion, or whether CH2's chip has a different timing.
- 2026-10-20 10:08-10:14: PC_DEBUG build of 0.2.79 (backups/fw-0.2.79-debug_2c91aa72.bin, apt GCC 13 toolchain, DEBUG_RAW) loaded
  for raw 0x63 / 0x64 / 0x36 access. Stage map at 200 MSa/s, range 0, means 116..140, all four cores at stages 2..5 with and
  without the reference bit (scratchpad stage_map_153A.log): CH1 A clean at 2, 3, 5 and bad only at 4 (the power-on stage:
  counts 346, std 29.5, rails); CH1 B bad at 2, clean at 3, 4, 5; CH2 A clean at every stage; CH2 B bad at 2 (std 57), clean
  at 3, 4, 5; the reference bit changes nothing on this unit (B from its own encode is right, as fpga-gen9 inferred from the
  lane pattern). So the capture fix works and the power-on stages are wrong only for CH1 A on this unit, which is what the
  training is for. The CH2 "no clean stage" of 10:06 was the outlier threshold: the 0x36 counters compare a core's sample
  with its previous word against 0x64 = 8 codes, and CH2's analog noise at range 0 near code 128 is about 3 codes rms (CH1:
  0.5..1.2), so CH2's counters saturate at 65535 on noise alone at every stage, while thresholds 16, 24, 32, 40 and 48 all read
  0 on a good stage set and hundreds (CH1 A4) or 65535 (CH2 B2) on a bad one, three captures each. The counters do not run
  after the capture ends (stable over 1.2 s). Decision: training and self-check threshold 32 codes (firmware; 0x153B can move
  CAP_THR_PO too). Expected training result on this unit: CH1 A3 B4, CH2 A3 B4 = 0x08E3.
- 2026-10-20 10:14-10:19: fw 0.2.80 (threshold 32, pinned backups/fw-0.2.80_6acd4eed.bin sha256
  6acd4eed48c6f8a135af0adde581b6c9ecf7e065ece269b5032d2d6eaa9c2728) loaded; CAL_PROBE mode 1: CH1 OK, CH2 OK. Owner ran the
  wizard (item 4): "Capture stages: CH1 A4 B4" / "CH2 A3 B4", saved; GET_CAL trailer 1, stored 0x08E4 on 0x153A, in effect
  0x08E4. So the training kept the power-on stage 4 for CH1 A although the stage map shows it bad: the band sweep at 200 MSa/s
  with the trained stages still has CH1 A garbled at means 128..132 (dco 12: std 20.7, 41 rails; dco 16: 17 rails), B cores
  and CH2 clean (max std 2.8), and the in-operation self-check flagged 2 of 21 captures. Cause: the training probes one code
  (the centre PWM) with one 8000-word capture per stage; the fault is intermittent and lives at a few codes of the band, so a
  single capture can read 0. Fix (fw-cap2): probe several codes across 120..140 with two captures per point, sum the counts,
  pick the stage with the widest clean run (here A3 with 2 and 5 clean), and let a persistently flagged core in operation
  ask for a calibration. Until then the unit runs with CH1 A4 (bad at two codes at 200 MSa/s only).
- 2026-10-20 10:29: section 53 (host commands leave the modal screens) first check on fw 0.2.80: owner opened a menu in the
  Spectrum view (ui_busy 1); GET_CHANNEL from the host was answered at once (0.00 s wait), ui_busy 0 afterwards, link stats
  ui_yields 0 -> 1, ui_yield_refused 0. The screenshot shows the Spectrum view back on its CTRL page with the 1 kHz square's
  spectrum and the overview in the track; no "Host command: menu closed" note in this view (known: the Spectrum view does not
  draw it yet, queued).
- 2026-10-20 10:31: section 53 nested case: Menu > Settings fold-out > Settings screen open; GET_CHANNEL answered in 0.02 s,
  ui_busy 0, ui_yields 2; owner: "all the way to Spectrum with no menu open". The owner also decided to drop the
  "Host command: menu closed" note (fw-yield2). Remaining section 53 checks: the wizard mid-measurement refusal (detail 3),
  the generator keypad half-typed, the file-save refusal (detail 4): with the next walk-through.
- 2026-10-20 10:33-10:38: fw 0.2.81 (fw-cap2: band training at codes 120..140, two captures per stage and point, widest clean
  run; repeat warning GET_HEALTH 106 bit 5) merged, pinned backups/fw-0.2.81_50f983bf.bin (sha256
  50f983bf0ee636249a13641b0f6d5f4f3c0ef98bc600dab08ce24ab656a8b363), loaded 10:36. First CAL_PROBE mode 1: NOISE on both
  channels before any measurement: the generator was on (the owner's Spectrum AUTO SET at about 10:25 had switched it on) with
  CH1 on the post. Generator off, second probe: the training ran and chose 0x08E3 (event 23 b = 0x08E3 for both channels, the
  predicted CH1 A3 B4 / CH2 A3 B4), then the zero step failed at range 4 (0.2 V/div) with RESPONSE at the coarse hi point on
  both channels: the probe tips are still on the RC filter, whose node floats with the generator off. Owner asked to take the
  probes off, then the probe again and the wizard.
- 2026-10-20 10:41: fw 0.2.82 (fw-spzoom2 + fw-yield2 on 0.2.81) pinned backups/fw-0.2.82_5ad6085e.bin (sha256
  5ad6085e9d9c5d7ac0c37c890442c17a5a8078948cb06f553131055a859d060b), loaded. CAL_PROBE mode 1 with the probes open on the BNCs:
  training 0x08E3 again on both channels; the zero step then fails at range 4 with DRIFT at the gated zero pair (11 / 15 codes
  of movement: the open 1x probe tips pick up hum at 0.2 V/div). The wizard needs the probes unplugged from the device.
- 2026-10-20 10:42-10:48: probes unplugged: CAL_PROBE mode 1 both channels all ranges OK, training 0x08E3. Owner ran the wizard
  (item 4): "CH1 A3 B4" / "CH2 A3 B4", saved: GET_CAL trailer 1, stored 0x08E3 on 0x153A, in effect 0x08E3; the self-check since
  the load: 18424 captures read, 0 flagged. **Band sweep with the trained stages (item 2): 200 MSa/s every core clean over means
  109..135 (CH1 A max std 0.98, 0 rails, 0 jumps; CH1 B 1.5; CH2 A 3.6; CH2 B 2.5), 100 MSa/s clean (0.5 / 0.8 / 1.6 / 2.0).**
  The A-core capture fault of 0x1535..0x1539 is gone on this unit with 0x153A trained. 50 and 20 MSa/s not re-run: the USB link
  is being reset by the host about every 30 s since the last replug (13 bus resets in 6 minutes, pattern suspend then reset =
  a host port reset; the firmware keeps running, no restart record); owner asked to re-seat the cable. Scratchpad logs
  dco_sweep_153A_0x8e3_d0.log, _d1.log.
- 2026-10-20 10:49: owner re-seated the USB cable (20 bus resets at that point, none during the two sweeps after). Band sweep with
  the trained stages at 50 and 20 MSa/s: clean on every core (max std 0.5 / 0.7 / 1.2 / 1.8 and 0.5 / 0.5 / 0.7 / 1.7). Item 2 of
  fpga/al3/README.md section 10 complete at all four rates; self-check 0 flagged of 18865 captures. 0x153A goes to "recommended"
  in fpga/al3/dist (bench 10-20). Remaining section 10 items: 3 (RC + Frequency response, probes back on the filter), 6 (EXT
  loopback, optional), 7 (warm-up, optional).
- 2026-10-20 10:52-10:59: owner: Frequency response AUTO SET on the RC ends with "ADC readout fault: see Diagnostics" (reason 8)
  on 0x153A / fw 0.2.82, although the self-check sees 0 flagged captures. Reproduced from the host: BODE_RUN 100 Hz..1 MHz ends
  in the pre-scan with reason 8 every time (0 of 20 measured). Cause found one layer down: the first decimated capture after a
  sample-rate change can come back as a whole record of one saturated value (45.4 V / 61.2 V at 2.5 V/div = 16-bit full scale)
  on both READ_RAW and READ_TRACE, e.g. CIC2+FIR at divider 4095 (48.8 kSa/s) right after 1 MSa/s; the next capture at the same
  rate is fine; plain (filter off) records are fine at every divider. The pre-scan is exactly such a first capture at the sweep's
  slowest rate, so it reads "clipped" at every range and ends at 5 V/div with reason 8 (the reason text is misleading here). On
  fw 0.2.71 (before fw-bus) on 0x153A the device sweep ran once (20/20, CH1 ranged to 0.5 V/div, 0.07 dB) and failed with reason 8
  on the second run, so the fault is intermittent and not only fw-bus's write order; on 0x1539 / 0.2.71 four sweeps in a row
  ran yesterday. Statistics per filter mode and divider follow.
- 2026-10-20 11:00-11:02: the saturated record could not be reproduced on demand: 80 CIC2+FIR captures after rate changes at
  five dividers, 72 captures over every order of filter / rate writes, relay toggles with 0 / 100 / 300 ms waits and offset
  jumps before the capture, all clean on fw 0.2.82; it was seen three times in sequences that followed a device sweep or a
  fresh range change. The deterministic part is the engine's own `clipped`: pc_bode.c `cap->s[c].clipped = (h.overload != 0)`
  takes `scopesettings.channelN.ADoverload`, which fpga_control.c's readout sets sticky (|= 1 / 254 on a 0 or 255 sample) and
  which is cleared only on paths the engine does not run; one railed sample anywhere (the fault captures of 0.2.79 / 0.2.80,
  a saturated record, a real clip at a sensitive range) marks every later engine capture clipped, the pre-scan ranges down to
  5 V/div and the 0.2.71 rule ends the sweep with reason 8. Yesterday's device sweeps (every point flagged clipped) fit the
  same flag. Fix dispatched (fw-bode2): the engine judges clipping from the record's own min / max only, the sticky flag is
  cleared per capture, and reason 8 is raised from the capture self-check (0x36) rather than from clipping.
- 2026-10-20 11:10-11:16: fw 0.2.83 (fw-bode2: engine clipping from the record only, ADoverload cleared per readout, reason 8
  from the 0x36 self-check) merged, pinned backups/fw-0.2.83_e63e0e02.bin (sha256
  e63e0e024735dab131be6430ba4151a645a1e49ea30df10503b01120a3c77866), loaded 11:14. Two host-started device sweeps on the RC
  (100 Hz .. 1 MHz, 25 points, Settle Normal): both complete (25/25, 3.0 s, reason 0), GET_STATUS overload bits 0 afterwards.
  Sweep 1 is the clean one: CH1 ranged to 0.5 V/div (range 3), CH2 0.5 .. 0.2, 13 of 25 points unflagged, the rest incoherent /
  spliced / ch2_small as the host flags them: -2.86 dB / -40 deg at 10.0 kHz, -17.6 dB at 100 kHz, -36.1 dB at 1 MHz. Sweep 0,
  the first after the engine's configuration change, left CH1 at 2.5 V/div (range 1) for the whole sweep with ch1_low on 15
  points: its pre-scan capture was a bad first record (the "saturated first decimated capture after a change" of 10:55, which
  is systematic in the engine's own sequence, not rare) and the engine never steps CH1 up again. So AUTO SET / the first sweep
  after entering the mode may run at a coarse CH1 range until the next sweep; queued: find the first-record artefact (FPGA
  decimator or the firmware's write order on 0x153A) and make the pre-scan step CH1 up when its fundamental is low. Host
  sweep at 11:16 for the comparison follows.
- 2026-10-20 11:14-11:16: the host tool hit the same artefact: fnirsi1013d-bode on the RC right after the device sweeps died with
  ZeroDivisionError in measure_pair (CH1 tone 0: a flat record; guard queued). Repro statistics: four rounds of (device sweep
  100 Hz..1 MHz 10 points, BODE_CLOSE, host captures): round 0 left GET_DECIM at fpga_mode 0 / fpga_shift 2 after the sweep and
  the host's third capture (CIC2+FIR at divider 4095, range 1, right after two clean CIC3+FIR captures at the same divider)
  was a whole record of 61.2 V / 60.1 V on both channels; rounds 1..3 (state 227 / shift 2 after the sweep) were clean in all
  12 captures. Earlier today the same artefact hit a first capture after set_channel + set_decim + set_timebase (10:57 A) and
  twice after a device sweep (10:55). So the saturated record follows a decimator mode change at a slow rate under some prior
  state, about one in ten of the transitions tried; never with the filter off. Handed to fpga-gen10 (Fable) with the
  firmware's write order.

