# spiflash_d2xx.py - SPI flash tool for the FT2232D (Sipeed debugger)

Backs up, verifies and programs the 1 MB Winbond W25Q80DV FPGA config flash of the FNIRSI 1013D using the
Sipeed USB-JTAG/TTL RISC-V Debugger (FTDI FT2232D, USB 0403:6010). It uses the FTDI **D2XX** driver through
the `ftd2xx` package, so the stock CDM driver stays installed (no Zadig/WinUSB, no pyftdi).

    python -m pip install --user ftd2xx
    python firmware/tools/spiflash_d2xx.py wiring

Only channel A has MPSSE on the FT2232D; the tool opens D2XX serial `A` (override with `--serial`).
`--clock` defaults to 1 MHz (12 MHz / ((1+div)*2)), maximum 6 MHz.

## Wiring (3.3 V logic only)

| Debugger pin | FT2232D | SPI | W25Q80 SOIC-8 pin |
|---|---|---|---|
| TCK | ADBUS0 | SCK | 6 CLK |
| TDI | ADBUS1 | MOSI | 5 DI |
| TDO | ADBUS2 | MISO | 2 DO |
| TMS | ADBUS3 | /CS | 1 /CS |
| GND | GND | | 4 GND |

Pins 3 (/WP) and 7 (/HOLD) must be high. Do not connect the adapter's power pins to the scope.
The TCK/TDI/TDO/TMS to ADBUS0..3 mapping is the standard FTDI JTAG convention; the Sipeed schematic was never
found, but the mapping is confirmed on the maintainer's adapter by the valid JEDEC ID it reads with this wiring.
`probe` prints the idle pin state (expected `0xfc` unconnected). A valid JEDEC ID confirms the wiring; if you
read 00/FF, check the labels against ADBUS0..3 before suspecting the flash. On the 1013D the header's own pin order
is found by continuity first (`docs/guide/prepare.md`); on the maintainer's board it is 1 /CS, 2 DO, 3 CLK, 4 DI,
5 VCC, 6 GND.

## Commands

    spiflash_d2xx.py probe [--uid]
    spiflash_d2xx.py read -o file.bin [--size N] [--twice] [--force]
    spiflash_d2xx.py verify -i file.bin
    spiflash_d2xx.py write -i file.bin --yes [--no-backup] [--no-erase | --chip-erase] [--pad 0xFF] [--unprotect]
    spiflash_d2xx.py erase (--chip | --range START:LEN) --yes [--no-backup]

`probe` exits non-zero when the ID is 00/FF and prints `no chip detected (ID ff ff ff or 00 00 00)`.
`write` and `erase` need `--yes` **and** an interactive typed confirmation of the JEDEC ID (e.g. `ef4014`).
They first save a double-read backup to `backups/<timestamp>-pre-write.bin` plus a `.sha256` file, refuse to
continue if the two reads differ, erase only the 4 KB sectors whose content must change, program only changed
pages, then read the whole chip back and exit non-zero on any mismatch. Short files are padded (default 0xFF)
and long files truncated, each with a warning.

## Backup procedure

1. On the 1013D: scope switched on and running from its battery, USB unplugged (no PC, no charger), the header's
   VCC pin and the adapter's power pins unconnected. Connect as above. This is how every backup and every write on
   the maintainer's unit was done, 2026-10-18 to 2026-10-20 (`docs/hardware-status.md`).
2. `probe`: expect `ef 40 14` (or `5e 60 14` for a ZB25VQ80 clone, which the stock dump was taken from).
3. `read -o backups/fpga-flash-orig.bin --twice` - fails if the two reads differ. Note the printed sha256.
4. Check the header: 32 bytes of `ff`, then `cc 55 aa 33` at offset 32 (AL3 sync word, see
   `research/notes/fpga-verilog-analysis.md`). The stock Atlan4 dump (`fpga-1013D-1-zb25vq80.bin`) has
   `cc55aa33 f0000006 18006c31` there. Compare with `verify -i` against any known-good dump.
5. Copy the backup off the machine and record the hash in `docs/hardware-status.md`.

## Safety notes

- 3.3 V only. A 5 V adapter level will destroy the flash and the FPGA.
- Power state. On the 1013D this project reads and writes with the board powered from its battery and USB
  unplugged (above): the FPGA shares the SPI bus but reads the flash only at power-up, so once it is configured
  it leaves the bus alone, and every write on the maintainer's unit read back identical that way. The adapter's
  power pins stay unconnected; the scope powers the flash. If reads are unstable, first lower `--clock` (e.g.
  500000). There is no way to hold the FPGA in reset from this adapter.
- General caution for other boards: a powered target can mean bus contention with whatever else drives the SPI
  lines, and an unpowered target with the flash fed from the adapter's 3.3 V also powers the FPGA partially
  through its I/O clamps. Writing the 1013D with the board off is untested here. The alternative that avoids
  both is to lift flash pin 1 (/CS) and wire the adapter to the lifted pin.
- Never write without a verified backup (AGENTS.md rule). `--no-backup` exists for chips already dumped.
- Test status: tested against a simulated chip, and on the maintainer's unit for every FPGA flash backup and write
  from 0x1532 (2026-10-18) to 0x153B (2026-10-20), each one read back and verified (`docs/hardware-status.md`;
  the list of images is `fpga/al3/dist/BITSTREAMS.md`).
