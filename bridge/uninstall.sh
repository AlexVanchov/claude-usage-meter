#!/bin/bash
# Removes the Mac side of the Claude Usage Meter (LaunchAgent, statusLine entry, cached state).
set -euo pipefail
BRIDGE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LABEL="com.claude-meter.push"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
SETTINGS="${CLAUDE_CONFIG_DIR:-$HOME/.claude}/settings.json"

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
rm -f "$PLIST"
echo "LaunchAgent removed"

if [ -f "$SETTINGS" ] && [ "$(jq -r '.statusLine.command // empty' "$SETTINGS")" = "$BRIDGE/claude-meter-statusline.sh" ]; then
  backup="$SETTINGS.bak-$(date +%Y%m%d%H%M%S)"
  cp "$SETTINGS" "$backup"
  jq 'del(.statusLine)' "$backup" > "$SETTINGS"
  echo "statusLine removed from $SETTINGS (backup: $backup)"
fi

rm -rf "$HOME/Library/Application Support/claude-meter"
echo "Done. bridge/meter.env was kept."
