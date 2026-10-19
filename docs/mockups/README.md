# Mockup sources

The 800x480 artboards of the UX makeover (`makeover/`, design list `docs/plans/ux-makeover.md`) and of the Frequency
response mode (`frequency-response/`, design `docs/plans/bode-ui.md`), as the HTML files the Design artifacts were built
from, plus the generators in `gen_ux/` (`common.py` and `boards.py` draw the bars, chips, cards and fold-outs; `bode*.py`
and `patch_spectrum_slider.py` build the Frequency response and Spectrum-slider boards on top). Regenerate: run the
generator scripts from `gen_ux/` with the project directory as the output. They are the acceptance reference for the
bench walk-throughs; the published artifacts are convenience views of the same files.
