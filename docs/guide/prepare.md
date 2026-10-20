# Prepare and back up

Do this before the steps on the [Install](install.md) page. It takes you through identifying your board, backing up
what the install can overwrite, and, if you want the FPGA bitstream, the one piece of soldering and the backup of
the FPGA's configuration flash. The open firmware itself never touches the stock firmware inside the scope: the only
things you write are a micro SD card and, optionally, that small flash chip.

Everything here was done on one unit, the maintainer's. Where this page says "on the maintainer's unit", that is
what was measured there; your board can differ, so measure yours.

## Identify the board

There is no way to tell the variant from the outside. Switch the scope off, unplug USB, open the back (the board
comes out of the case for the header soldering anyway) and compare with this photo:

![The AL3A10 board, back cover off, with the programmer's jumper wires on the 2x3 header](../photos/board-al3a10-programmer.jpg)

What to look for:

- **The FPGA**: a large, square, unmarked 144-pin chip (bottom right in the photo, next to the two ADCs). It has no
  maker's logo and no part number.
- **The FPGA flash chip**: a small 8-pin chip (bottom left) marked **25Q80** (a Winbond 25Q80DV on the maintainer's
  unit; other units of this variant carry a ZB25VQ80, 25VQ16 or 25VQ32). Right beside it, an empty 2x3 footprint.
  That footprint is the header you solder for the bitstream.
- The MCU (the Allwinner F1C100s, centre) with its own 8-pin flash nearby, and the micro SD slot.

An unmarked FPGA next to a 25Q80 and the empty 2x3 footprint is the **Anlogic AL3A10** variant. That is the board the
firmware and the bitstreams are made for.

Other boards exist: an FPGA marked **Anlogic EF2L45** (it has no external flash chip) or **Altera EP4CE6**. The
firmware for those is inherited from Atlan4's and untested here, and no bitstream exists for them. If you have one,
the card may still work, at your own risk; do not flash the FPGA.

## What you need

- A **spare micro SD card**. The card's partition has to be recreated, which erases its files, and the card the scope
  has used so far is worth keeping as it is.
- A **USB card reader**.
- A **PC with Linux, or Windows with WSL**. Writing the card is tested on Linux and on WSL with the reader attached
  through `usbipd` (below). Windows alone is enough for downloading, checking hashes and backing up a card with
  Win32DiskImager.
- Only for the FPGA bitstream:
    - A **3.3 V SPI programmer**. The tested one is a Sipeed USB-JTAG/TTL debugger with an FTDI FT2232D, driven by
      the project's own tool on Windows; every bitstream on the maintainer's unit went in that way. Any 3.3 V SPI
      programmer that flashrom supports works in principle (the engineering page lists them).
    - **Not a plain CH341A.** Many CH341A boards switch their supply pin to 3.3 V but still drive the data lines at
      5 V, and 5 V on these lines can destroy the flash chip and the FPGA. Use one only with a verified 3.3 V
      modification, and measure the idle level of its CLK, MOSI and CS pins with a multimeter first.
    - A soldering iron, a 2x3 pin header and six female-to-female jumper wires, and a multimeter with a continuity
      beeper.

## Back up the SD card that is in the scope

If the scope came with a card, or you ran another firmware from a card, image the whole card before you change
anything. Open firmware keeps its calibration, its settings and the display and boot configuration in raw sectors
708 to 710, outside the partition, so a plain file copy does not save them; only a whole-card image does.

**On Linux or WSL**, with the reader at `/dev/sdX` (find the letter with `lsblk`; a wrong device destroys a disk):

```
lsblk
sudo dd if=/dev/sdX of=sd-before.img bs=4M status=progress conv=fsync
sha256sum sd-before.img | tee sd-before.img.sha256
```

Check the backup by comparing it with the card:

```
sudo cmp -n $(stat -c %s sd-before.img) sd-before.img /dev/sdX && echo IDENTICAL
```

**On Windows**, use Win32DiskImager: pick the card's drive letter, choose a file name, press **Read**. The image is
as large as the card. Afterwards **Verify Only** compares the file with the card.

Keep the image and its hash somewhere that is not the card. Writing it back with `dd` (or Win32DiskImager's Write)
restores the card completely, calibration included.

## Attach a USB card reader to WSL

WSL does not see USB devices by itself. With [usbipd-win](https://github.com/dorssel/usbipd-win) a device is handed
from Windows to WSL; this is how the maintainer's card was written. Install usbipd-win from its releases page or with
`winget install --id dorssel.usbipd-win`, plug in the reader with the card, then:

```
usbipd list                              # find the reader's BUSID (a "USB Mass Storage Device")
usbipd bind --busid <id>                 # once per device, from an administrator PowerShell
usbipd attach --wsl --busid <id>         # from any PowerShell, every time you plug it in
```

In WSL, `lsblk` now shows the card as `/dev/sdX` with its partition as `/dev/sdX1`. While it is attached, Windows
does not see the reader; `usbipd detach --busid <id>` gives it back. The same three commands attach the scope itself
when it is in FEL mode (below) or when it offers its card as a USB drive.

## Verify what you download

Every release comes with a `SHA256SUMS` file. Check every file before you use it, and stop on any mismatch.

On Linux or WSL, in the download directory:

```
sha256sum -c SHA256SUMS --ignore-missing
```

One `: OK` line per file is the good result.

On Windows, in PowerShell, compare the printed hash with the line for that file in `SHA256SUMS`:

```
Get-FileHash -Algorithm SHA256 fnirsi_1013d-fw-X.Y.Z.bin
```

or, in any command prompt, `certutil -hashfile fnirsi_1013d-fw-X.Y.Z.bin SHA256`.

## For the bitstream: the FPGA flash

Skip this whole section if you only want the firmware on the card. The firmware runs on the stock bitstream, without
the deep memory, the generator, the filters, the external trigger and the frequency-response mode.

The bitstream lives in the 25Q80 flash chip, which is wired only to the FPGA: neither the firmware nor the USB boot
mode can reach it, so it is written once through the 2x3 header with the programmer. The write itself is described on
the [Install](install.md) page and in [`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md); this section is
everything that comes before it.

### Solder the header

![The 2x3 header with pins fitted, next to the 8-pin flash chip](../photos/fpga-flash-header.jpg)

The footprint comes empty. Fit a 2x3 pin header and solder it from the other side of the board (the back), so the
board comes out of the case once. Do it with the scope off, the battery connector unplugged and USB out.

### Confirm the pinout by continuity

Nobody guarantees the pin order of this header, and the maintainer's board did not follow the pinout that an earlier
reconstructed schematic predicted. Measure yours before you connect anything. With the scope off and the battery
unplugged, put one probe of the multimeter (continuity mode) on a pin of the 8-pin flash chip and move the other
across the six header pins. The chip's pins, counted counter-clockwise from the dot or bevel, are 1 /CS, 2 DO, 3 /WP,
4 GND, 5 DI, 6 CLK, 7 /HOLD, 8 VCC.

Measured on the maintainer's unit, numbering pin 1 at the top-left of the header with the board lying as in the
photos (connectors at the top):

| Header pin | Flash chip pin | Signal | What the meter shows |
|---|---|---|---|
| 1 | 1 | /CS | beep |
| 2 | 2 | DO | about 34 ohm, not a clean beep: a small resistor sits in series |
| 3 | 6 | CLK | beep |
| 4 | 5 | DI | beep |
| 5 | 8, also 3 and 7 | VCC | beep to the three pins; the node sits behind a diode from the 3.3 V rail |
| 6 | 4 | GND | beep, also to any ground such as the shield of a BNC |

Also check that no two header pins beep to each other. If a flash pin has no header partner, stop and ask; do not
guess. Write your table down and photograph the board with the pin numbers marked; you will wire the programmer from
it.

### Wire the programmer

3.3 V logic only. With the Sipeed FT2232D debugger (the labels on its housing):

| Debugger pin | Header pin (your table) |
|---|---|
| TMS | /CS |
| TDO | DO |
| TCK | CLK |
| TDI | DI |
| GND | GND |

Leave the header's VCC pin **unconnected**, and leave the debugger's 3V3 and 5V pins **unconnected**: the scope
powers the flash chip. Keep the leads short.

### Power during flashing

Scope **on, running on its battery, with USB unplugged** (no PC, no charger) while the programmer is attached. The
FPGA reads its flash only when the scope powers up, so once it is running it leaves the bus alone. This is how every
backup and every bitstream write on the maintainer's unit was done, and every one read back correctly.

### Back up the FPGA flash, twice

The project's tool runs on Windows through the standard FTDI driver (the one Windows uses for the debugger's serial
ports; nothing else to install). Get the tool from a checkout of the release you are installing, and install its one
dependency:

```
git clone --branch vX.Y.Z https://github.com/mikljohansson/fnirsi-1013d-mcp
cd fnirsi-1013d-mcp
python -m pip install --user ftd2xx
python firmware/tools/spiflash_d2xx.py wiring
```

Then probe and read:

```
python firmware/tools/spiflash_d2xx.py probe
python firmware/tools/spiflash_d2xx.py read -o backups/fpga-flash-before.bin --twice
```

- `probe` must print the chip's JEDEC ID: `ef 40 14` for a Winbond 25Q80DV, `5e 60 14` for a ZB25VQ80. All `00` or
  all `ff` means the wiring is wrong. Stop and check the table; do not write anything.
- `read --twice` reads the whole chip two times and refuses to save unless the two reads agree. The file is
  1,048,576 bytes and the tool prints its sha256.

Look at the start of the file. Bytes 0 to 31 are `ff`, then at offset 32 the bitstream begins with the sync word
`cc 55 aa 33` followed by `f0 00 00 06 18 00 6c 31`; the last group is the FPGA's identity, which is how you know it
is an AL3A10. On Linux or WSL `xxd -s 32 -l 16 backups/fpga-flash-before.bin` shows it; on Windows
`Format-Hex -Path backups\fpga-flash-before.bin -Offset 32 -Count 16`. A stock chip has its content end at byte
282,866 with erased `ff` after it, reports version 0x1432 to the firmware, and on the maintainer's unit had the sha256
`90f65f084bdee8f443830ff6c48b7a90d0600fafa7e68713686542a29fa0d253`, the same as two published dumps of other stock
units. A matching hash means your stock bitstream is the known one; a different one is still a valid backup, just a
different stock revision.

**Keep this file**, with its hash, in two places that are not the scope. It is the only way back to the stock
bitstream, and the stock firmware needs the stock bitstream. The write tool takes another double-read backup of its
own before it erases, but that one lands in the checkout's `backups/` folder, where it is easy to lose.

## FEL, the USB boot mode

FEL is a mode of the scope's processor in which a PC loads a firmware image into the scope's memory over USB and
starts it, without writing anything. The boot menu's **FEL** button enters it; the scope then shows up on the PC as
USB device `1f3a:efe8`, and on WSL you attach it with the `usbipd` commands above. A power cycle leaves it.

You need it in two cases: to try a firmware image before you put it on the card, and to get a working firmware into
the scope when the image on the card misbehaves. For a card that no longer boots at all, FEL is not needed: remove
the card and the stock firmware runs. The commands and the limits are in the engineering pages,
[`firmware/README.md`](../../firmware/README.md) section 4 and [Hardware preparation](../hardware-prep.md) section 6.

## Going back to stock

The stock firmware never leaves the scope. Pick **FNIRSI** in the boot menu, or take the card out, and it runs.

If you flashed a bitstream, write your backup of the FPGA flash back **first** and verify it, as
[`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md), "Rollback", describes:

```
python firmware/tools/spiflash_d2xx.py write -i backups/fpga-flash-before.bin --yes
python firmware/tools/spiflash_d2xx.py verify -i backups/fpga-flash-before.bin
```

The stock firmware with a custom bitstream is not a valid combination; the scope cannot restore its own FPGA flash.
A card you imaged above goes back with `dd` or Win32DiskImager's Write, calibration included.
