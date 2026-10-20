# FNIRSI 1013D open firmware

Open firmware for the FNIRSI 1013D tablet oscilloscope (Anlogic AL3A10 variant) that boots from the SD card and leaves
the stock firmware untouched. It gives the scope 16,384 samples per channel instead of 3,000 with a trigger you can
place anywhere, clean traces at every time base because the FPGA filters before it decimates, a spectrum analyser with
waterfall and density views, a frequency-response mode that measures a filter with the scope and its calibration post
alone, a calibration wizard, a digital phosphor view, an external trigger on the calibration post and a diagnostics
screen. A USB protocol and a Python MCP server let an AI agent (Claude Code, Codex, any MCP client) configure the
scope, capture and analyse while you keep using the touch screen. Pull the card and the scope is stock again.

This site is built from the `docs/` folder of the repository
([mikljohansson/fnirsi-1013d-mcp](https://github.com/mikljohansson/fnirsi-1013d-mcp)); the front page of the
repository is the [README](https://github.com/mikljohansson/fnirsi-1013d-mcp#readme).

## Three shelves

**User guide.** For someone who owns the scope.
Start with [Install](guide/install.md), or point your AI agent at a clone of the repository and ask it to guide you:
it follows the [agent runbook](guide/agent-runbook.md). The card alone needs no soldering and is undone by removing
it; the optional FPGA bitstream needs a soldered header and a 3.3 V programmer. Then [Prepare and back up](guide/prepare.md),
[First run](guide/first-run.md) and [Calibration](guide/calibration.md). [The screens](guide/screens.md) walk
through the main menu one entry at a time: the Scope with its phosphor view, the Spectrum analyser with waterfall and
density views, the Frequency response mode that measures a filter or tank with the scope alone, the file viewers,
Settings, Diagnostics and the USB drive. [Generator](guide/generator.md) covers the square wave on the calibration
post.

**Agent guide.** For an AI agent, and for the person setting one up.
[Setup](agent-setup.md), [MCP tools and safety tiers](mcp-tools.md) and the
[measurement guide](agent-measurement-guide.md): what an agent should know before it measures.

**Engineering.** For contributors and the curious.
[Development](development.md) (layout, builds, the dev loop), [Articles](articles/index.md) (the stories behind the
hard parts), the [protocol](protocol.md), the [UI guidelines](ui-guidelines.md), [releasing](releasing.md) and the
[bench log](hardware-status.md).

!!! warning "Use at your own risk"
    Flashing the FPGA configuration flash or the SD card sectors can leave the scope unbootable until you restore a
    backup. Back up first ([Prepare and back up](guide/prepare.md)). The real limits of the hardware are
    200 MS/s per channel, 8 bit and about 30 MHz of analog bandwidth. Everything was verified on one unit.
