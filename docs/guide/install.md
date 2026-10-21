# Install

The open firmware boots from a micro SD card, through Atlan4's bootloader, and runs with its own FPGA bitstream, which
you flash into the FPGA's configuration flash. The stock firmware in the scope's flash is never touched. To run it
again, write your backup of the FPGA flash back and remove the card.

## What you need

- An FNIRSI 1013D with the **Anlogic AL3A10** FPGA: an unmarked 144-pin chip next to a Winbond 25Q80 flash with an
  unpopulated 2x3 header. [Prepare and back up](prepare.md) helps you identify your board. The EF2L45 and
  Altera EP4CE6 variants are untested.
- A spare micro SD card with an MBR partition table and one FAT32 partition that starts at **8 MiB (sector 16384)**
  or later. The firmware sits in the raw sectors before the partition. A GPT card does not work. Most cards come
  with the partition at 1 MiB or 4 MiB, so you usually have to recreate it: see [Prepare the card](#prepare-the-card).
- A PC with Python 3. Writing the card needs Linux, or WSL with the card reader attached through `usbipd`
  ([Prepare and back up](prepare.md#attach-a-usb-card-reader-to-wsl) shows how). The tool also accepts a Windows
  drive (`\\.\PhysicalDriveN`), but that path is untested.
- A 3.3 V SPI programmer and a soldering iron, for the FPGA bitstream.

!!! warning "Back up first"
    Back up the SD card before you write it. Back up the FPGA config flash, twice, before you flash the bitstream.
    [Prepare and back up](prepare.md) gives the steps.

## Prepare the card

The firmware image needs the space before the FAT32 partition. The image is about 0.8 MB and ends near sector 2260;
the 8 MiB start leaves room for later versions. Recreating the partition erases the files on the card, so copy
them off first. On Linux or WSL, with the card reader at `/dev/sdX`:

```
sudo fdisk -l /dev/sdX                                  # check the device and the current start
sudo parted /dev/sdX --script mklabel msdos mkpart primary fat32 8MiB 100%
sudo mkfs.vfat -F 32 /dev/sdX1
sudo fdisk -l /dev/sdX                                  # the partition now starts at 16384
```

On Windows, `diskpart` does the same: `select disk N`, `clean`, `create partition primary offset=8192` (the offset is
in KB), `format fs=fat32 quick`, `assign`. Pick the disk number with care. `diskpart` formats FAT32 only up to 32 GB.

`make_sd_image.py` never changes the partition table. It refuses to write when a part would overlap the partition,
and the message names the start the image needs. A partition that starts below sector 16384 without overlapping is
written, with a note that cards for this firmware should start it at 8 MiB so later images fit too; a new card gets
the 8 MiB start. Copy your files back after the card is written.

## Steps

1. Download from the [releases page](https://github.com/mikljohansson/fnirsi-1013d-mcp/releases):
   `fnirsi_1013d-fpga-al3-0x153b-flash1m.bin` (the FPGA bitstream), `fnirsi_1013d-fw-X.Y.Z.bin` (the scope firmware),
   `fnirsi_1013d-sdtools-X.Y.Z.zip` (the SD tools) and `SHA256SUMS`. Add the MCP wheel if you want it. Check the files:

    ```
    sha256sum -c SHA256SUMS --ignore-missing
    ```

    On Windows, `Get-FileHash -Algorithm SHA256 <file>` in PowerShell (or `certutil -hashfile <file> SHA256`) prints
    the hash to compare with the file's line in `SHA256SUMS`.

2. Flash the FPGA bitstream, **0x153D**. [`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md), also attached to each
   release, names the file, the programmer wiring, the backup, the write, the check and the way back. The config flash
   is wired only to the FPGA, so this needs the SPI programmer on the 2x3 header. A wrong pin or 5 V logic can damage
   the board. The FPGA comes before the card: with any other bitstream in it, this firmware only shows a banner.
3. Unzip the SD tools. Build the SD parts. The tool downloads Atlan4's package from his repository and takes the
   bootloader v0.8 and the updater from it. This project does not redistribute them, because they are binary-only.
   The tool checks their hashes:

    ```
    python3 make_release_sd_package.py build --scope fnirsi_1013d-fw-X.Y.Z.bin \
            --scope-sha256 <hash from SHA256SUMS> --parts parts
    ```

4. Write the parts to the card, prepared as in [Prepare the card](#prepare-the-card). Replace `/dev/sdX` with your card
   reader (on WSL, [attach it](prepare.md#attach-a-usb-card-reader-to-wsl) first):

    ```
    python3 make_sd_image.py write --device /dev/sdX --bootloader parts/bootloader.bin \
            --updater parts/updater.bin --scope parts/scope.bin --boot-mode 4
    ```

    The tool asks you to type the device path again before it writes. It backs up the card, writes only these parts
    and reads them back. Boot mode 4 shows the boot menu with the open firmware preselected.

    !!! warning "Do not `dd` the whole package"
        `dd` of the SD package from sector 16 also overwrites sectors 708 to 710, which hold the calibration, the
        settings and the boot mode.

5. Put the card in the scope and switch it on. Continue with [First run](first-run.md).

!!! note "After a new bitstream, calibrate again"
    The calibration stores values per ADC core, and the core order can differ between bitstreams. Diagnostics shows
    "FPGA changed since calibration" until you run the [wizard](calibration.md).

## Which bitstream

This release runs with bitstream 0x153D only. An older or newer bitstream shows a banner that names the one needed
([First run](first-run.md)). The hashes are in [`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md).

## The MCP server (optional)

To let an AI agent use the scope over USB, install the MCP server from the release's wheel. See
[Agent setup](../agent-setup.md).

## Going back to stock

Write your backup of the FPGA flash back first ([`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md), "Rollback"),
then pick FNIRSI in the boot menu, or remove the card.
