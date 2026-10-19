# Agent setup

Connect an AI agent (Claude Code, Codex, any MCP client) to the scope over USB. The full setup, environment variables
and install from a checkout are in the [mcp README](../mcp/README.md).

## Install and register

```
uv tool install <URL of the release's fnirsi1013d-X.Y.Z-py3-none-any.whl>   # or pipx install / pip install
fnirsi1013d-mcp --list-tools                         # the tools and where files go (~/.fnirsi1013d)
claude mcp add --scope user fnirsi1013d -- fnirsi1013d-mcp --port auto
```

The scope enumerates as a USB serial device ("FNIRSI 1013D"); `--port auto` finds it. Only one client may hold the
scope's serial port at a time. Without a scope, `fnirsi1013d-mcp --sim "--variant al3"` runs against the built-in
simulator, which answers every tool.

For Codex, in `~/.codex/config.toml`:

```toml
[mcp_servers.fnirsi1013d]
command = "fnirsi1013d-mcp"
args = ["--port", "auto"]
```

## Then

- Read [MCP tools and safety tiers](mcp-tools.md): read-only and acquisition tools run freely, relay changes are
  rate-limited, anything that drives the calibration pin needs a confirmation token.
- Read the [measurement guide](agent-measurement-guide.md) before the first capture. Claude Code loads it as the skill
  `fnirsi-scope` when the repository is open.
- Ask, for example: "capture CH1 and measure the frequency", "average 32 captures at 1 MS/s and fit the decay after the
  trigger", "set the generator to 5 kHz 25 % and show me the duty on CH2". Large results are saved as `.npz` files and
  the path is returned.

!!! note "To be written after fw 0.2.70"
    Worked examples with plots (each with the command that generated it) and a screenshot of the scope while a host
    is connected.

![The scope with a host connected](screenshot:scope-mcp)
