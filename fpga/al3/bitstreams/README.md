# Bitstreams (on disk only, not in git)

Our own released bitstreams (0x1533 onwards) are committed in `../dist/` with their hashes; this folder holds
the third-party reference images only.

Everything here except this README is ignored by git (`fpga/**/*.bin` in `.gitignore`).
These are binary SPI-flash images for the FPGA configuration flash (Winbond 25Q80, 1 MiB part).
Never overwrite the stock dump. Never flash without a verified backup first (see `CLAUDE.md`).

| File | Size (bytes) | sha256 | Provenance |
|---|---|---|---|
| `atlan4-FPGA-0x1532.bin` | 282,898 | `0d4c829c231b3bbe18c6dd6e815523b6e168f787561376aae502db6c1f370fd2` | Atlan4's prebuilt custom AL3 bitstream (reports FPGA version 0x1532). Copied from `D:\src\Fnirsi1013D\Verzia 1.xxx\FPGA for 1.xxx\FPGA AL3\FPGA.bin`; identical to `FPGA AL3S10\FPGA.bin`. Built with TD 4.6.116866 on 2026-02-14. Needs bootloader >= 0.8 on the MCU side. Never flashed by us. |
| `fpga-1013D-1-zb25vq80.bin` | 1,048,576 | `90f65f084bdee8f443830ff6c48b7a90d0600fafa7e68713686542a29fa0d253` | Atlan4's dump of a stock FNIRSI 1013D AL3 config flash (ZB25VQ80, 1 MiB). Stock design, reports 0x1432. Payload is 282,866 bytes, the rest is 0xFF. Same file as in his `backup fpga` folder. This is somebody else's unit, not the owner's. The owner's own flash must still be dumped into `backups/`. |

Both images start with 32 bytes of 0xFF, then the sync word `cc55aa33`, then the packet `f0000006 18006c31`
(device id of AL3A10). Neither has been verified against the owner's unit.
