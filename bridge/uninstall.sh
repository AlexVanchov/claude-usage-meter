#!/bin/bash
# Removes the computer side of the Claude Usage Meter (keep-alive, statusLine entry, cached state).
# Uses $CLAUDE_CONFIG_DIR (default ~/.claude) to find the Claude Code settings.
set -euo pipefail
BRIDGE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$BRIDGE/common.sh"
SETTINGS="${CLAUDE_CONFIG_DIR:-$HOME/.claude}/settings.json"

if [ "$(uname -s)" = "Darwin" ]; then
  launchctl bootout "gui/$(id -u)/com.claude-meter.push" 2>/dev/null || true
  rm -f "$HOME/Library/LaunchAgents/com.claude-meter.push.plist"
  echo "launchd agent removed"
else
  UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
  systemctl --user disable --now claude-meter-push.timer 2>/dev/null || true
  rm -f "$UNIT_DIR/claude-meter-push.service" "$UNIT_DIR/claude-meter-push.timer"
  systemctl --user daemon-reload 2>/dev/null || true
  echo "systemd timer removed"
fi

if [ -f "$SETTINGS" ] && [ "$(jq -r '.statusLine.command // empty' "$SETTINGS")" = "$BRIDGE/claude-meter-statusline.sh" ]; then
  backup="$SETTINGS.bak-$(date +%Y%m%d%H%M%S)"
  cp "$SETTINGS" "$backup"
  jq 'del(.statusLine)' "$backup" > "$SETTINGS"
  echo "statusLine removed from $SETTINGS (backup: $backup)"
fi

rm -rf "$CLAUDE_METER_STATE_DIR"
echo "Done. bridge/meter.env was kept."
