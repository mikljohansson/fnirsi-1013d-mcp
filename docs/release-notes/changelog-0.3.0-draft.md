# CHANGELOG 0.3.0 section, draft

Draft of the `## [0.3.0]` section of `CHANGELOG.md`, prepared by rel-gen2 on 2026-10-20 from the 0.1.0-0.2.102
development entries. The coordinator applies it in the release commit and then deletes this file. How to apply:

1. Keep `## [Unreleased]` at the top, empty (no text under it). Remove the "Nothing has been tagged yet" paragraph
   and the "Plan for the first tag (v0.3.0)" block.
2. Insert everything between the two rules below as the `## [0.3.0]` section, with the release date.
3. Move the existing development entries under its "Development history" heading, unchanged except for the heading
   levels: the three `### ...` component headings become `####`, the `#### 0.2.x` version headings become `#####`.
   Order the 0.2.46-0.2.50 entries (today 0.2.50, 0.2.47, 0.2.48, 0.2.49, 0.2.46) as 0.2.50, 0.2.49, 0.2.48, 0.2.47,
   0.2.46.
4. Run `python .github/scripts/release_meta.py --check-changelog` and the `--tag v0.3.0` check
   (`docs/releasing.md` section 2 step 5).

Commit hashes are the ones the development entries cite; every one resolves in the repository (checked 2026-10-20).

---

## [0.3.0] - YYYY-MM-DD

The first tagged release. The firmware (`PC_FW_VERSION`) and the mcp package share the version 0.3.0. The user-facing
release notes are `docs/release-notes/0.3.0.md`; the build-by-build history follows under "Development history". Every
firmware build was benched on one unit, the owner's AL3A10 (`docs/hardware-status.md`).

### Firmware (`firmware/scope`, from Atlan4 v1.00o5)

- USB control protocol 1.24 over USB CDC: framed, CRC-checked requests with shared test vectors (0.1.0, 9ffd7ba);
  shared control with the touch screen, no remote lock-out, the MCP chip in the top bar (0.2.0, d77298f). Link fixes:
  the FIFO layout (0.1.1, 5a8432b; 0.1.6, 697e1f6), the first request after opening the port (0.2.11, 1594918), the USB
  PHY tuning that never reached the PHY on the F1C100s (0.2.25, 85a113d), recovery after a long idle with an inferred
  resume and a soft reconnect (0.2.49, c20b657). Event log, link counters and a restart record for diagnosing the link
  (0.2.12-0.2.24).
- SD card: sector commands for persisting an image from the PC (0.2.2, 0d4d9bb); the write window ends at the FAT32
  partition, capped at sector 16384, and cards need the partition at 8 MiB or later (0.2.101, f2c53e7, f758b51).
- Spectrum analyser view (0.2.28, e6a7c4c): fixed-point FFT up to 4096 points with ARM assembly hot loops (0.2.31,
  0930c72), waterfall and density views (0.2.32, 83aa5b5), averaging and max hold (0.2.40, e595a0f), the 16-bit
  filtered records (0.2.34, 1a9a773), span-driven sample rate and pans that keep it (0.2.77, 52c1c76; 0.2.82, 9294f9e).
- MMU on with D-cache and write buffer: the 4096-point FFT from 10.96 to 2.72 ms (0.2.48, 8cdefda).
- FPGA decimation filters (boxcar, CIC2, CIC3; 0.2.30, b770356) and the compensation FIR (0.2.44, 2e0cd10), the Filter
  kept per mode with CIC2 in the Scope and CIC2+FIR in the Spectrum (0.2.64, 5818b5b).
- External trigger on the calibration pin (0.2.9, bebf901) and its pipeline offsets per bitstream (0.2.72, 7833312;
  0.2.79, 381dd8a).
- Calibration: per-core zeros in two clock classes (v2, 0.2.43), per-core gains (v3, 0.2.45, c8f17bf), background
  tracking of the full-rate mismatch and common mode (v4, 0.2.50, 7cfc7f7, 93087b2), the four-screen wizard and the
  first-run screen (0.2.59, 95f440e, 5e8a5db), capture-stage training over a band of codes on 0x153A and later (0.2.79,
  381dd8a; 0.2.81, 3e5a2a1), noise limits of the open-input condition shared by the wizard and Diagnostics (0.2.100,
  5879193).
- Full-rate capture checks against garbled ADC cores: every 200 MSa/s capture checked and re-taken when garbled (0.2.62,
  9e7e53d), the 0x153A capture self-check and a repeated-flag warning (0.2.79, 0.2.81).
- Diagnostics (Menu > Diagnostics): identity, FPGA line, verdict tiles, calibration provenance, event log, a report on
  the card; GET_HEALTH and the stored restart record (0.2.52, b830434; 0.2.59, f95dbee); Restarts counts only crashes
  (0.2.100, 5879193).
- Digital phosphor view (0.2.46, 15f15e0), also in X-Y (0.2.56, a41ca39).
- Frequency response mode, the on-device Bode plot (0.2.66; redesigned 0.2.69, 5d262b7): AUTO SET that fails fast and
  ends in STOP (0.2.84, 9079725, 071c206; 0.2.86, a119035), range plans reused by repeated sweeps (0.2.87, 53447c4), CH2
  range Auto / Constant (0.2.93, d51979d; 0.2.94, 39213e7), points below the floor grey with a note (0.2.99, a590c1f).
- UX makeover: main menu, cards and chips (0.2.51), touch handles (0.2.54, 4bec0ff), Settings screen (0.2.55,
  66a3a70), CTRL page and fold-outs (0.2.56, a41ca39), polish batches (0.2.63, 0.2.68), Measurements v2 (0.2.65,
  8749ffe), slider-track overviews and tap to jump (0.2.73-0.2.74, a5c875e, de8f49c), time/div-driven sample rate
  (0.2.76, da24d91). Menus no longer offer what the hardware cannot do (0.2.6, 68f7868; removed in code 0.2.47,
  55960cf).
- Host requests close an open menu instead of answering E_BUSY (0.2.78, 0493025); read-only requests and READ_SCREEN
  are answered inside it (0.2.94, ce78bc9); screenshots over USB (0.2.70, 350823e).
- Capture correctness: auto trigger mode in the firmware (0.2.41, 0fd6a9c); the FPGA bus contract (one word late,
  record start, arm order; 0.2.72, 5f38d23); clipping judged per record (0.2.83, 51ab2a4); decimated records no longer
  saturated after a configuration change (0.2.88, 61af5dd, 1299f7a); auto-mode records no longer spliced on 0x1532-0x153A
  and a "may be spliced" flag (0.2.97, 49d99bf, dc35b9f; 0.2.98, d1f4e6b).
- Reproducible images: the same commit gives the same sha256 in any checkout and in CI; the `PC_PROTOCOL=0` build and
  the byte-identical check against v1.00o5 removed (0.2.47, 473bd0b, 57d0d26, 8770209, 0f84761).

### FPGA (`fpga/al3`, AL3A10 only)

- Headless Anlogic TD flow that rebuilds Atlan4's 0x1532 byte for byte (e790a7d, 42c79dc).
- 0x1533 external trigger (526c969), 0x1534 decimation filters (6d4c8ad), 0x1535 compensation FIR (e2aec2e), 0x1536
  segmented capture and hardware averaging (27c0c75, unbenched).
- 0x1537, 0x1538 and 0x1539 (c15981e, ce943bf, b4bd8b0): encode phase, encode check and MCU readout fixes, each
  superseded on the bench and withdrawn.
- 0x153A (f735fae): ADC data captured in IOB registers at a trainable stage per core, with a capture self-check;
  supported.
- 0x153B (c173ed5): records that end on the auto time-out are the newest words, the time-out no longer cuts a triggered
  record; recommended (benched 2026-10-20 20:40-20:49).
- Released images committed under `fpga/al3/dist` with `manifest.json`, `BITSTREAMS.md` and the CI check
  `tools/bitstreams.py` (375adb7); releases attach every image that is not withdrawn.

### MCP server and host tools (`mcp/`)

- Protocol client, serial and TCP transport, a device simulator and a FastMCP server (0d8b40f, 8c80c1e, a60d35a): 32
  tools with safety tiers (30 by default; raw FPGA and FEL only when enabled), confirmation tokens for the generator
  and the calibration pin, an audit log.
- Measurement tools: averaged capture, FFT, decay fit, measurements, segmented capture and hardware averaging (4699593),
  the scope's spectrum (7f364a8), host and device Bode sweeps (f0940ee, 363f676, d3a16bb, 0a8e94d), reports as PNG and
  HTML (1a3c0aa), the volts model with the scope's per-core calibration (62da1e0, 5f6bc0d).
- Diagnostics readers (`scope_health`, `scope_sd_info`, `scope_save_report`, b830434, 8614032) and `scope_cal_probe`
  (516b76a, b3b9c51).
- `fnirsi1013d-flash` (FEL load, persist after a backup, restore; 136eb1b, 9e79583) and its refusal texts for the card
  rule (a7c5727); `fnirsi1013d-sd`, `fnirsi1013d-bench`, `fnirsi1013d-soak`, `fnirsi1013d-bode`.
- Works installed from the wheel with data under `~/.fnirsi1013d` (8ea8886), tested by the CI `wheel` job (0e1d36b);
  frees the serial port after 30 s idle (275e273).
- Agent measurement guide and the Claude Code skill `fnirsi-scope` (dc7f4b1).

### Documentation and tooling

- Documentation site from `docs/` with MkDocs Material, published to GitHub Pages (9704316, 41d0e2d): install, first
  run, calibration, the screens, the generator, the agent guide, six engineering articles, screenshots taken on the unit
  by `tools/screenshots.py` (d20d753).
- CI and release workflows (573f9b6), release notes from `docs/release-notes/`, bitstream assets and `-rcN` dry runs
  (bd16d02); SD packages built on the user's machine from Atlan4's download (7659ed6); `make_sd_image.py` enforces the
  partition start (1563daf).
- SPI flash tools for the FT2232D (71774ff, 108df11, fee75d9); protocol specification, hardware preparation guide and
  roadmap (9d32933, 1bd4179, a947e78).

### Development history

(The existing entries, from "Firmware (scope application, `firmware/scope`, base Atlan4 v1.00o5)" to the end of
"Tooling, FPGA and documentation", go here with their headings demoted one level.)

---
