# Install

The open firmware boots from a micro SD card, through Atlan4's bootloader. The stock firmware in the scope's flash is
never touched. Remove the card and the scope runs the stock firmware again. If you also flash an FPGA bitstream, write
your backup of the FPGA flash back first.

## What you need

- An FNIRSI 1013D with the **Anlogic AL3A10** FPGA: an unmarked 144-pin chip next to a Winbond 25Q80 flash with an
  unpopulated 2x3 header. [Hardware preparation](../hardware-prep.md) helps you identify your board. The EF2L45 and
  Altera EP4CE6 variants are untested.
- A spare micro SD card with an MBR partition table and one FAT32 partition that starts at **8 MiB (sector 16384)**
  or later. The firmware sits in the raw sectors before the partition. A GPT card does not work. Most cards come
  with the partition at 1 MiB or 4 MiB, so you usually have to recreate it: see [Prepare the card](#prepare-the-card).
- A PC with Python 3. Writing the card needs Linux, or WSL with the card reader attached through `usbipd`. The tool also
  accepts a Windows drive (`\\.\PhysicalDriveN`), but that path is untested.
- For the FPGA bitstream only: a 3.3 V SPI programmer and a soldering iron.

!!! warning "Back up first"
    Back up the SD card before you write it. Back up the FPGA config flash, twice, before you flash a bitstream.
    [Hardware preparation](../hardware-prep.md), section 3, gives the steps.

## Prepare the card

The firmware image needs the space before the FAT32 partition. Firmware 0.2.101 is about 0.8 MB and ends at sector
2261; the 8 MiB start leaves room for later versions. Recreating the partition erases the files on the card, so copy
them off first. On Linux or WSL, with the card reader at `/dev/sdX`:

```
sudo fdisk -l /dev/sdX                                  # check the device and the current start
sudo parted /dev/sdX --script mklabel msdos mkpart primary fat32 8MiB 100%
sudo mkfs.vfat -F 32 /dev/sdX1
sudo fdisk -l /dev/sdX                                  # the partition now starts at 16384
```

On Windows, `diskpart` does the same: `select disk N`, `clean`, `create partition primary offset=8192` (the offset is
in KB), `format fs=fat32 quick`, `assign`. Pick the disk number with care. `diskpart` formats FAT32 only up to 32 GB.

`make_sd_image.py` never changes the partition table. It refuses a card whose partition starts too low, and the
message names the start the image needs. Copy your files back after the card is written.

## Steps

1. Download from the [releases page](https://github.com/mikljohansson/fnirsi-1013d-mcp/releases):
   `fnirsi_1013d-fw-X.Y.Z.bin` (the scope firmware), `fnirsi_1013d-sdtools-X.Y.Z.zip` (the SD tools) and `SHA256SUMS`.
   Add the bitstream and the MCP wheel if you want them. Check the files:

    ```
    sha256sum -c SHA256SUMS --ignore-missing
    ```

2. Unzip the SD tools. Build the SD parts. The tool downloads Atlan4's package from his repository and takes the
   bootloader v0.8 and the updater from it. This project does not redistribute them, because they are binary-only.
   The tool checks their hashes:

    ```
    python3 make_release_sd_package.py build --scope fnirsi_1013d-fw-X.Y.Z.bin \
            --scope-sha256 <hash from SHA256SUMS> --parts parts
    ```

3. Write the parts to the card, prepared as in [Prepare the card](#prepare-the-card). Replace `/dev/sdX` with your card
   reader:

    ```
    python3 make_sd_image.py write --device /dev/sdX --bootloader parts/bootloader.bin \
            --updater parts/updater.bin --scope parts/scope.bin --boot-mode 4
    ```

    The tool asks you to type the device path again before it writes. It backs up the card, writes only these parts
    and reads them back. Boot mode 4 shows the boot menu with the open firmware preselected.

    !!! warning "Do not `dd` the whole package"
        `dd` of the SD package from sector 16 also overwrites sectors 708 to 710, which hold the calibration, the
        settings and the boot mode.

4. Optional, the FPGA bitstream. The recommended one is **0x153B**. It needs firmware 0.2.97 or later.
   [`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md), also attached to each release, names the file, the programmer
   wiring, the backup, the write, the check and the way back. The config flash is wired only to the FPGA, so this
   needs the SPI programmer on the 2x3 header. A wrong pin or 5 V logic can damage the board.
5. Put the card in the scope and switch it on. Continue with [First run](first-run.md).

!!! note "After a new bitstream, calibrate again"
    The calibration stores values per ADC core, and the core order can differ between bitstreams. Diagnostics shows
    "FPGA changed since calibration" until you run the [wizard](calibration.md).

## Which features need which bitstream

The firmware runs on every bitstream. A feature appears when the bitstream has it.

| Bitstream | What it adds |
|---|---|
| 0x1432 (stock) | The scope, the Spectrum analyser and the phosphor view. No generator, no gain calibration |
| 0x1532 (Atlan4's) | 16,384 samples per channel, programmable trigger position, the generator on the calibration post |
| 0x1533 | External trigger on the calibration post |
| 0x1534 | Decimation filters (Acquisition > Filter) and the Frequency response mode |
| 0x1535 | The compensation FIR after the filters |
| 0x1536 | Hardware averaging (the ACQ menu's Average uses it at the fast rates) and segmented capture (over USB) |
| 0x153A | Clean ADC data at every sample rate, with a capture self-check and the capture stages the wizard trains |
| 0x153B | Records that end on the auto time-out are never joined from two passes of the memory. **Recommended** |

0x1537 to 0x1539 are withdrawn. 0x1532 to 0x1536 can garble one ADC core at 200 MSa/s after a rate change; the firmware
checks every such capture and takes it again, and Diagnostics shows the FPGA line in amber. The full list with hashes
is in [`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md).

## The MCP server (optional)

To let an AI agent use the scope over USB, install the MCP server from the release's wheel. See
[Agent setup](../agent-setup.md).

## Going back to stock

Pick FNIRSI in the boot menu, or remove the card. If you flashed a bitstream, write your backup of the FPGA flash back
first ([`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md), "Rollback").
