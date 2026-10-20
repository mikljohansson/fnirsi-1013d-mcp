# docs/plans

Working papers: design documents, reviews and the task queue. They are not part of the documentation site (mkdocs
excludes `plans/`); the user guide, the protocol reference and the engineering articles under `docs/` are the
reference, and `docs/articles/` tells the stories behind the designs.

## Live

| File | What it is |
|---|---|
| `task-queue.md` | open items and the dispatch log; the bench session appends to it |
| `roadmap.md` | the epic-level view of release 0.3.0, with a dated status paragraph on top |
| `release-readiness.md`, `release-checklist.md`, `announcement.md` | the tag-day paperwork for 0.3.0; retired after the tag |
| `segmented-capture.md` | FPGA 0x1536 sequencer design; its bench is still open |
| `phosphor-fast-capture.md` | design note for the phosphor capture loop, not built |
| `bode-ui.md` | the Frequency response mode specification the firmware cites |
| `ui-rows.md` | the trigger Type row and the ACQ Sample row (FPGA 0x153C): design note and mockups, not built |

## Finished designs, kept as the design record

The documents below describe features that are built and shipped; the reference for how they work today is the code,
`firmware/README.md`, `docs/protocol.md` and `docs/mcp-tools.md`. They stay here because the firmware, the mcp package
and the host tests cite them by section number (for example `touch_pick.h` cites `touch-review.md` sections b and c,
`pc_mmu.c` cites `mmu-dcache.md`). Where a document and the code disagree, the code and the reference documents win.

| File | Built as | Story |
|---|---|---|
| `firmware-usb-protocol.md` | the USB CDC transport and the command protocol (fw 0.1.x, `docs/protocol.md`) | |
| `collaborative-mode.md` | shared control with the UI, protocol 1.3 (fw 0.2.0) | |
| `dpos350p-features.md` | feature study; the spectrum analyser and the phosphor view came from it | |
| `phosphor-design.md` | digital phosphor view (fw 0.2.46) | |
| `mmu-dcache.md` | MMU and D-cache (fw 0.2.48) | `docs/articles/mmu-fft.md` |
| `interleave-tracking.md` | calibration v4, the background A/B tracker (fw 0.2.50) | `docs/articles/calibration.md` |
| `ux-makeover.md`, `ux-review.md`, `diagnostics-review.md`, `touch-review.md`, `slider-overview.md` | the UI rebuilt as one system (fw 0.2.51-0.2.74); rules in `docs/ui-guidelines.md` | `docs/articles/ux-makeover.md` |
| `bode-design.md` | the frequency response (Bode) measurement, host tool and device engine (fw 0.2.66+) | `docs/articles/frequency-response.md` |

A review on 2026-10-20 (before release 0.3.0) checked every document here against the tree; nothing was removed
because every finished design is still cited from code. Should a document be removed later, git keeps it:
`git log --diff-filter=D -- docs/plans`.
