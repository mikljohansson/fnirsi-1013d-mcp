# Runbook for the agent guiding an install

You are an AI agent, and a person has asked you to help put this project's firmware on their FNIRSI 1013D. This
page tells you what to ask, which checks you may never skip, the commands in order with what a good result looks
like, and what to do when something fails. The person does the physical steps (cards, soldering, the programmer);
you prepare each command, read its output, and stop when a check fails.

Sources, which you should open when you need the detail: [Install](install.md) (the card),
[Hardware preparation](../hardware-prep.md) (identifying the board, backups, programmers, FEL recovery),
[`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md) (the FPGA bitstream, its programmer tool and the rollback),
[First run](first-run.md) and [Calibration](calibration.md).

## Ask first

1. **Which operating system is the PC running?** Writing the card is tested on Linux and on WSL with the card reader
   attached through `usbipd`. `make_sd_image.py` also accepts a Windows drive (`\\.\PhysicalDriveN`), but that path
   is **untested**: do not take it unless the person accepts that. On Windows, ask them to install WSL or use a Linux
   machine.
2. **Have they identified the FPGA variant?** The firmware is verified on the Anlogic AL3A10 variant only, and the
   bitstreams are for it only. If they have not looked, guide them (power off, battery disconnected, USB unplugged):
    - Open the back. Find the FPGA: an unmarked 144-pin square chip. Next to it, an 8-pin flash chip marked
      **25Q80** (Winbond 25Q80DV on the tested unit; ZB25VQ80, 25VQ16 or 25VQ32 on other AL3 units) and an
      unpopulated 2x3 header. That is the AL3A10 board.
    - A marked FPGA, Anlogic EF2L45 (no external flash chip) or Altera EP4CE6, is another variant. The firmware
      for those is inherited from Atlan4 and untested here, and no bitstream exists for them. Tell the person and
      stop, unless they want to try the firmware at their own risk; never flash their FPGA.
    - Ask for a photo of the area around the flash chip and the header, and have them read the chip marking.
      [Hardware preparation](../hardware-prep.md), sections 1.2 and 3a, has the detail.
3. **Do they want the FPGA bitstream, and do they have a 3.3 V SPI programmer?** The firmware runs without it (on the
   stock bitstream the deep memory, the generator, the filters, the external trigger and the frequency-response mode
   are missing). The bitstream needs six pins soldered to the 2x3 header, the pinout found by continuity, and a 3.3 V
   programmer: the FT2232D route in `BITSTREAMS.md` is the tested one; a CH341A needs a 3.3 V modification
   ([Hardware preparation](../hardware-prep.md), section 2). 5 V logic can damage the board.
4. **Do they have a spare micro SD card?** The card's files are erased when its partition is recreated, and the
   card the scope used so far should be kept as it is.

## Safety gates, in this order, never skipped

1. Identify the variant before anything else. No AL3A10, no bitstream.
2. Back up the card before writing it: a full image if it holds anything the person wants to keep or an earlier open
   firmware (its calibration and settings live in raw sectors 708 to 710), and in every case let
   `make_sd_image.py write` take its own backup, which it does before it writes; check that its output says so.
3. Back up the FPGA flash **twice and compare** before any write to it, and keep the file. It is the only way back
   for the FPGA.
4. Verify `SHA256SUMS` before building or writing anything.
5. Never `dd` the whole SD package to the card. It zeroes sectors 708 to 710.
6. When any check fails, stop and ask the person. Do not retry with a variation, do not skip ahead.

## The card

### 1. Download and verify

From the [releases page](https://github.com/mikljohansson/fnirsi-1013d-mcp/releases) (v0 releases are marked
pre-release): `fnirsi_1013d-fw-X.Y.Z.bin`, `fnirsi_1013d-sdtools-X.Y.Z.zip`, `SHA256SUMS`, plus the bitstream
`fnirsi_1013d-fpga-al3-0x153b-flash1m.bin` and the MCP wheel if wanted. In the download directory:

```
sha256sum -c SHA256SUMS --ignore-missing
```

Good: one `: OK` line per downloaded file and nothing else. Any `FAILED` means a bad download: stop.

### 2. Prepare the card

The firmware sits in the raw sectors before the FAT32 partition, which must start at **8 MiB (sector 16384)** or
later on an MBR card. Most cards come with the partition at 1 MiB or 4 MiB, so it usually has to be recreated. This
erases the card's files. With the card reader at `/dev/sdX` (confirm the device with the person; a wrong device
destroys a disk):

```
sudo fdisk -l /dev/sdX
sudo parted /dev/sdX --script mklabel msdos mkpart primary fat32 8MiB 100%
sudo mkfs.vfat -F 32 /dev/sdX1
sudo fdisk -l /dev/sdX
```

Good: the last `fdisk -l` shows a `dos` disklabel and one partition whose Start is 16384. The
[install page](install.md#prepare-the-card) has the `diskpart` equivalent for a card prepared on Windows.

### 3. Build the SD parts

Unzip the SD tools. The build downloads Atlan4's v1.00o5 package from a pinned commit of his repository, takes the
bootloader v0.8 and the updater from it (they are binary-only, so this project does not redistribute them) and checks
their hashes:

```
python3 make_release_sd_package.py build --scope fnirsi_1013d-fw-X.Y.Z.bin --scope-sha256 <hash from SHA256SUMS> --parts parts
```

Good: the tool reports the hashes as matching and `parts/` holds `bootloader.bin`, `updater.bin` and `scope.bin`.
A hash mismatch or a download failure: stop. `--local <zip>` uses a copy the person already has.

### 4. Write the parts

```
python3 make_sd_image.py write --device /dev/sdX --bootloader parts/bootloader.bin --updater parts/updater.bin --scope parts/scope.bin --boot-mode 4
```

The tool asks the person to type the device path again. Good: it backs up sectors 0 to the last written sector
into `backups/`, writes only these three parts and the boot-mode byte, reads everything back and reports success.
It refuses a GPT card and a card whose partition starts too low, and names the start it needs: then go back to
step 2. Boot mode 4 shows the boot menu with the open firmware preselected, the safe choice for a first boot.

## The bitstream (optional)

Only on an AL3A10 board, only with a 3.3 V programmer, only after the card works. The recommended bitstream is
**0x153B**; it needs firmware 0.2.97 or later, which every release from 0.3.0 has. The config flash is wired only to
the FPGA, so the scope cannot write it itself. Follow [`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md), "How to
flash", step by step; in short, with the FT2232D tool from a checkout of the release tag:

```
git clone --branch vX.Y.Z https://github.com/mikljohansson/fnirsi-1013d-mcp
cd fnirsi-1013d-mcp
python -m pip install --user ftd2xx
python firmware/tools/spiflash_d2xx.py wiring
```

1. **Pinout by continuity** before anything is connected ([Hardware preparation](../hardware-prep.md), section 1.3).
   On the tested unit the header reads 1 /CS, 2 DO, 3 CLK, 4 DI, 5 VCC, 6 GND, which differs from the reconstructed
   schematic: the person must measure theirs. Scope on its battery, USB unplugged, the header's VCC and the
   programmer's power pins left unconnected.
2. **Probe**: `python firmware/tools/spiflash_d2xx.py probe`. Good: JEDEC ID `ef 40 14` (W25Q80DV) or `5e 60 14`
   (ZB25VQ80). `00` or `ff` means wiring: stop.
3. **Back up twice**: `python firmware/tools/spiflash_d2xx.py read -o backups/fpga-flash-before.bin --twice`.
   Good: the two reads agree, the file is 1,048,576 bytes, and bytes 32 to 35 are `cc 55 aa 33` (the stock payload
   reports 0x1432). Two reads that differ: stop. Have the person copy the file somewhere safe.
4. **Write and verify**:

    ```
    python firmware/tools/spiflash_d2xx.py write -i fnirsi_1013d-fpga-al3-0x153b-flash1m.bin --yes
    python firmware/tools/spiflash_d2xx.py verify -i fnirsi_1013d-fpga-al3-0x153b-flash1m.bin
    ```

    The tool asks for the JEDEC ID to be typed and takes another double-read backup before it erases. Good: verify
    reports the chip identical to the file. **On any error do not power-cycle the scope**: write the backup from
    step 3 back first (Rollback below).
5. Disconnect the programmer and power-cycle. Good: the scope boots, and Menu > Diagnostics, Status, shows FPGA
   0x153B with its features. A bootloader older than v0.8 hangs on any custom bitstream; the card from step 4 above
   carries v0.8.
6. Calibrate again (next section). The calibration is stored per ADC core and per bitstream.

## First boot and the wizard

Put the card in and switch the scope on. Good: the boot menu with four buttons, PECO (the open firmware) preselected,
FEL, FNIRSI (the stock firmware) and Update; after about 2 seconds the open firmware starts. An uncalibrated unit
shows the First start screen with one line per check (the FPGA bitstream it found, the settings, the calibration).

Have the person let the scope warm up for 5 minutes, then tap **Calibrate now** (or later Menu > Diagnostics >
Calibrate) and follow the four screens: zero level with both probes, anything on the calibration post and the USB
cable disconnected; CH1 probe at 1X on the calibration post; CH2 the same; Save calibration. Good: screen 4 says
"CH1 calibrated" and "CH2 calibrated" with the cores matched. On the stock bitstream the gain steps are skipped and
the wizard says so; that is expected. [Calibration](calibration.md) lists every failure message and its remedy.

Then, if wanted, connect the MCP server: [Agent setup](../agent-setup.md).

## When it goes wrong

- **The open firmware does not start, or crashes.** The boot menu is still there at power-on: pick FNIRSI to run the
  stock firmware, or remove the card. Nothing in the scope's own flash was changed. If the menu does not appear, the
  bootloader on the card is the problem: write the card again from step 4, after checking the card's partition start.
- **Hang after the loader banner with a custom bitstream.** A bootloader older than v0.8 waits for the stock FPGA
  forever. Write the release's card parts (they carry v0.8), or restore the stock bitstream.
- **A failed FPGA write, or a dead scope after one.** Do not power-cycle. Rollback with the backup from the double
  read, then verify:

    ```
    python firmware/tools/spiflash_d2xx.py write -i backups/fpga-flash-before.bin --yes
    python firmware/tools/spiflash_d2xx.py verify -i backups/fpga-flash-before.bin
    ```

    Going back to the stock firmware for good needs the stock bitstream first: the stock firmware with a custom
    bitstream is not a valid combination.
- **A card that no longer boots anything.** Remove it: the stock firmware in the scope's flash runs. The card can
  be prepared and written again from step 2.
- **FEL.** The boot menu's FEL button puts the scope into the chip's USB boot mode, through which a PC can load a
  firmware image into RAM without writing anything; the scope then shows up as USB device `1f3a:efe8`. This is the
  development loop and the recovery path when a card image misbehaves; the commands, the `usbipd` attach for WSL
  and the limits are in [Hardware preparation](../hardware-prep.md), section 6. A FEL load touches no flash.
- **Calibration lost or wrong after a card write.** Sector 708 of the card holds it. Restore it from the full card
  image if one was taken, or run the wizard again.

Do not improvise around a failed check. A mismatching hash, a refused card, a differing double read or a probe that
reads `ff` each have one correct response: stop, tell the person what you saw, and ask.
