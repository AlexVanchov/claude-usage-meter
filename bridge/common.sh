# Shared settings for the bridge scripts (sourced, not executed).
BRIDGE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$BRIDGE_DIR/meter.env" ] && . "$BRIDGE_DIR/meter.env"
CLAUDE_METER_HOST="${CLAUDE_METER_HOST:-claude-meter.local}"

if [ -z "${CLAUDE_METER_STATE_DIR:-}" ]; then
  if [ "$(uname -s)" = "Darwin" ]; then
    CLAUDE_METER_STATE_DIR="$HOME/Library/Application Support/claude-meter"
  else
    CLAUDE_METER_STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/claude-meter"
  fi
fi
CLAUDE_METER_LATEST="$CLAUDE_METER_STATE_DIR/latest.json"
