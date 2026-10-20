# Collaborative mode: shared control between the scope UI and the MCP host

Status: approved by the maintainer 2026-10-18. Replaces the exclusive LOCAL/REMOTE model entirely: there is one
behaviour, no modes. Host connected = shared control.
Implement after the request-corruption bug (firmware/AGENTS.md known issues) is fixed.

## Goal

The maintainer and the agent use the scope at the same time. Either side may change settings; the other side
sees the change. The agent reads what is on the screen or asks for its own single capture, which the screen
then shows. A small indicator shows that a host is connected. There is no lock and no banner.

## Semantics

1. **One live settings set.** Host SET_* commands write the same `scopesettings`/`fpgasettings` the UI uses
   and call the same "apply" functions the UI calls (relays, offset, timebase, trigger), then mark the
   screen dirty so the next UI pass redraws with the new values. GET_* always returns the live values.
   No snapshot, no restore; host changes persist and are saved at power-off like user changes.
2. **Last writer wins.** No priority between UI and host. A SET_* arriving while a menu is open is still
   E_BUSY (menus are modal loops); the ISR fast path (PING, GET_INFO, GET_STATUS, GET_BATTERY, text lines)
   is unaffected.
3. **Shared acquisition.**
   - RUN: the UI acquisition loop keeps running. READ_TRACE / READ_RAW return the latest finished capture
     (capture_id tells the host whether it is new). No ARM needed.
   - Host single capture: ARM in shared mode performs one acquisition with the live settings through the
     same path the UI uses for "single"; the UI displays it and stays in STOP until the user or the host
     presses RUN again (host: a RUN/STOP control in SET_ACQ_STATE or an existing command).
   - WAIT and the DONE/trigger_addr semantics are unchanged.
4. **Indicator.** A green "MCP" chip drawn over/next to the RUN/STOP chip in the top bar while a host session
   is active (DTR up, or traffic within the last 2 s). Steady, no pulsing or blinking on traffic (maintainer decision
   2026-10-18). Use the existing chip drawing helpers in menu.c; keep it under 60 px wide.
5. **No lock.** Long scripted runs (e.g. averaging) tag every capture with the settings it was taken under
   (timebase index/divider, ranges, trigger). If a capture's settings differ from the run's, the server
   restarts the run and reports it. The user is never blocked.
6. **Capabilities.** Advertise SHARED_MODE in GET_INFO caps; the MCP server falls back to the old exclusive
   model if the bit is absent (older firmware).

## Firmware work (firmware/scope, see firmware/AGENTS.md for the structure)

- pc_cmds.c: remove the LOCAL/REMOTE state machine and the "requires REMOTE" gates; SET_REMOTE/RELEASE
  become no-ops that answer OK (compatibility) and are dropped from the docs; route SET_*/ARM/READ_* through
  the UI apply functions; add the acquisition-state (RUN/STOP/single) control.
- pc_hal_scope.c: apply functions + screen-dirty flag; latest-capture accessor with capture_id.
- menu.c / display: MCP chip; remove the banner and the snapshot/restore code.
- Power-off ISR: plain settings save as in 1.00o5.
- Protocol 1.3: document in docs/protocol.md section 14; bump proto minor; add vectors.
- Host tests: session tests for shared mode (settings visible both ways, capture_id progression).

## MCP server work (mcp/, see mcp/AGENTS.md)

- Transport/scope.py: remove take_remote/release; `read_capture` without ARM returns the latest UI capture;
  `capture()` does a host single capture; captures carry their settings.
- Tools: drop scope_take_remote/scope_release; averaged_capture restarts on a settings change and reports it.
- Simulator: shared mode; UI-side setting changes simulated via a test hook.

## Acceptance (bench)

- Maintainer changes timebase on the touch screen; GET_TIMEBASE reflects it within one UI pass.
- Host SET_CHANNEL range; screen shows the new V/div, relay clicks once.
- RUN mode: READ_TRACE returns the displayed trace (compare with a screenshot or the on-screen measurements).
- Host single capture appears on screen; UI shows STOP; RUN resumes from either side.
- Maintainer changes the range during an averaged_capture run: the tool reports the restart.
- MCP chip visible while the server is connected, gone 2 s after disconnect.
