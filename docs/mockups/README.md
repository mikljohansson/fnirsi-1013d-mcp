# Mockup sources

The 800x480 artboards of the UX makeover (`makeover/`, design list `docs/plans/ux-makeover.md`) and of the Frequency
response mode (`frequency-response/`, design `docs/plans/bode-ui.md`), as the HTML files the Design artifacts were built
from, plus the generators in `gen_ux/` (`common.py` and `boards.py` draw the bars, chips, cards and fold-outs; `bode*.py`
and `patch_spectrum_slider.py` build the Frequency response and Spectrum-slider boards on top). Regenerate: run the
generator scripts from `gen_ux/` with the project directory as the output. They are the acceptance reference for the
bench walk-throughs; the published artifacts are convenience views of the same files.

The current boards come from `python gen_ux/boards.py makeover` (every makeover board it draws; a board name as a second
argument writes only that one) and `python gen_ux/bode_final.py frequency-response` (every current Frequency response
board); `frequency-response/SpectrumSlider.dc.html` is a copy of `makeover/Spectrum.dc.html`. `bode_icon.py` and
`bode_menu_part.py` write the menu icon and main-menu boards. The `patch_*.py` scripts are one-shot edits already applied
to the generators; they record how the boards got here and do not run again.

The slider-track overview (`docs/plans/slider-overview.md`) is drawn by `common.py` (`slider_row`, `ov_svg`,
`ov_line_runs`, `scope_square_overview`); `boards.py` adds the Spectrum's (`spectrum_overview`) and the board
`makeover/ScopeOverview` (a pulse-induction record zoomed into one decay, `pi_board`), `bode_final.py` the Frequency
response's (`track_x`, `fr_overview`).
