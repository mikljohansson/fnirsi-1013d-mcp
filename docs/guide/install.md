# Install

The open firmware boots from a micro SD card through Atlan4's bootloader. The stock firmware in the scope's flash is
never touched: remove the card and the scope is stock again (with a custom FPGA bitstream, write your backup of the
FPGA flash first).

!!! note "To be written after fw 0.2.70"
    This page restates the README and [releasing](../releasing.md) section 3 in order. A tested, click-by-click
    version follows after fw 0.2.70.

## Before you start

1. **Check that you have the supported variant**: the Anlogic AL3A10 FPGA (an unmarked 144-pin LQFP next to a Winbond
   25Q80 flash with an unpopulated 2x3 header). [Hardware preparation](../hardware-prep.md) helps you identify your
   board. The EF2L45 and Altera EP4CE6 variants are untested.
2. **Back up the SD card** you will use, and the FPGA config flash if you will flash a bitstream
   ([Hardware preparation](../hardware-prep.md), section 3).

## Steps

1. Download from the [releases page](https://github.com/mikljohansson/fnirsi-1013d-mcp/releases):
   `fnirsi_1013d-fw-X.Y.Z.bin`, `fnirsi_1013d-sdtools-X.Y.Z.zip`, `SHA256SUMS`, and the bitstream and wheel you want.
   Check them with `sha256sum -c SHA256SUMS --ignore-missing`.
2. Build the SD parts from the tools zip. This downloads Atlan4's bootloader v0.8 and updater from his repository and
   checks their hashes (they are binary-only, so this project does not redistribute them):

    ```
    python3 make_release_sd_package.py build --scope fnirsi_1013d-fw-X.Y.Z.bin --parts parts
    ```

3. Write the parts to a spare micro SD card (Linux, or WSL with the card reader attached through `usbipd`):

    ```
    python3 make_sd_image.py write --device /dev/sdX --bootloader parts/bootloader.bin \
            --updater parts/updater.bin --scope parts/scope.bin --boot-mode 4
    ```

    The tool backs up the card, writes only these parts and reads them back. Do not `dd` the whole package: that
    zeroes the sectors holding calibration, settings and boot mode.

4. Optional, the FPGA bitstream: [`BITSTREAMS.md`](../../fpga/al3/dist/BITSTREAMS.md) (also attached to each release)
   names the image, the programmer wiring, the backup, the write, the verification and the rollback. The config flash
   is wired only to the FPGA, so this needs an SPI programmer and soldering. A wrong pin or 5 V logic can damage the
   board.
5. Put the card in the scope and power on. Continue with [First run](first-run.md).

## Which features need which bitstream

The firmware runs on every bitstream; a feature appears when the bitstream has it. The table is in the
[README](../../README.md).

## Going back to stock

Pick FNIRSI in the boot menu or remove the card.
