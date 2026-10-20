# FNIRSI 1013D open firmware

Open firmware, FPGA changes and an MCP server for the FNIRSI 1013D tablet oscilloscope (Anlogic AL3A10 variant).
The firmware runs from the SD card and leaves the stock firmware untouched. A USB protocol and a Python MCP server let
an AI agent configure, capture and analyse while you keep using the touch screen.

This site is built from the `docs/` folder of the repository
([mikljohansson/fnirsi-1013d-mcp](https://github.com/mikljohansson/fnirsi-1013d-mcp)); the front page of the
repository is the [README](https://github.com/mikljohansson/fnirsi-1013d-mcp#readme).

## Three shelves

**User guide.** For someone who owns the scope.
Start with [Install](guide/install.md), then [Hardware preparation](hardware-prep.md) if you will flash the FPGA,
[First run](guide/first-run.md) and [Calibration](guide/calibration.md). [The screens](guide/screens.md) walk through
the main menu one entry at a time: the Scope with its phosphor view, the Spectrum analyser with waterfall and density
views, the Frequency response mode that measures a filter or tank with the scope alone, the file viewers, Settings,
Diagnostics and the USB drive. [Generator](guide/generator.md) covers the square wave on the calibration post.

**Agent guide.** For an AI agent, and for the person setting one up.
[Setup](agent-setup.md), [MCP tools and safety tiers](mcp-tools.md) and the
[measurement guide](agent-measurement-guide.md): what an agent should know before it measures.

**Engineering.** For contributors and the curious.
[Articles](articles/index.md) (the stories behind the hard parts, planned), the [protocol](protocol.md), the
[UI guidelines](ui-guidelines.md), [releasing](releasing.md) and the [bench log](hardware-status.md).

!!! warning "Use at your own risk"
    Flashing the FPGA configuration flash or the SD card sectors can leave the scope unbootable until you restore a
    backup. Back up first ([Hardware preparation](hardware-prep.md), section 3). The real limits of the hardware are
    200 MS/s per channel, 8 bit and about 30 MHz of analog bandwidth.
