#!/bin/bash
# Installs the Mac side of the Claude Usage Meter:
#   - bridge/meter.env           (display host + token)
#   - LaunchAgent                (re-sends latest usage every 60 s)
#   - Claude Code statusLine     (only with --configure-claude; otherwise prints the snippet)
#
# Usage: bridge/install.sh [--configure-claude] [--force]
set -euo pipefail

BRIDGE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$BRIDGE")"
LABEL="com.claude-meter.push"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/claude-meter-push.log"
SETTINGS="${CLAUDE_CONFIG_DIR:-$HOME/.claude}/settings.json"
STATUSLINE="$BRIDGE/claude-meter-statusline.sh"

CONFIGURE=0; FORCE=0
for a in "$@"; do
  case "$a" in
    --configure-claude) CONFIGURE=1 ;;
    --force) FORCE=1 ;;
    -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

step() { printf '\n\033[1;33m==>\033[0m %s\n' "$*"; }

step "Checking dependencies"
for bin in jq curl; do
  command -v "$bin" >/dev/null || { echo "missing: $bin (brew install $bin)"; exit 1; }
done
echo "ok"

step "Configuring bridge/meter.env"
if [ -f "$BRIDGE/meter.env" ]; then
  echo "exists, leaving as is"
else
  token=""
  if [ -f "$REPO/firmware/src/secrets.h" ]; then
    token=$(sed -n 's/^#define METER_TOKEN "\(.*\)"/\1/p' "$REPO/firmware/src/secrets.h")
  fi
  if [ -z "$token" ] || [ "$token" = "change-me" ]; then
    echo "No METER_TOKEN found in firmware/src/secrets.h - set it there first (see README)." >&2
    exit 1
  fi
  printf 'CLAUDE_METER_HOST=claude-meter.local\nCLAUDE_METER_TOKEN=%s\n' "$token" > "$BRIDGE/meter.env"
  chmod 600 "$BRIDGE/meter.env"
  echo "created (token taken from firmware/src/secrets.h)"
fi
chmod +x "$BRIDGE"/*.sh

step "Installing LaunchAgent $LABEL"
mkdir -p "$(dirname "$PLIST")" "$(dirname "$LOG")"
sed -e "s|__PUSH_SCRIPT__|$BRIDGE/claude-meter-push.sh|" -e "s|__LOG__|$LOG|g" \
  "$BRIDGE/com.claude-meter.push.plist.template" > "$PLIST"
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "loaded: $PLIST (log: $LOG)"

step "Claude Code status line ($SETTINGS)"
current=$(jq -r '.statusLine.command // empty' "$SETTINGS" 2>/dev/null || true)
if [ "$current" = "$STATUSLINE" ]; then
  echo "already configured"
elif [ "$CONFIGURE" = 1 ]; then
  if [ -n "$current" ] && [ "$FORCE" != 1 ]; then
    echo "A different statusLine is already set:"
    echo "  $current"
    echo "Re-run with --force to replace it, or call $STATUSLINE from your own script."
    exit 1
  fi
  mkdir -p "$(dirname "$SETTINGS")"
  [ -f "$SETTINGS" ] || echo '{}' > "$SETTINGS"
  backup="$SETTINGS.bak-$(date +%Y%m%d%H%M%S)"
  cp "$SETTINGS" "$backup"
  jq --arg cmd "$STATUSLINE" '.statusLine = {type: "command", command: $cmd}' "$backup" > "$SETTINGS"
  echo "configured (backup: $backup)"
else
  echo "Not changed. Add this to $SETTINGS (or re-run with --configure-claude):"
  printf '\n  "statusLine": { "type": "command", "command": "%s" }\n' "$STATUSLINE"
fi

step "Checking the display"
. "$BRIDGE/meter.env"
if curl -s -m 4 "http://${CLAUDE_METER_HOST}/usage" >/dev/null; then
  echo "reachable at http://${CLAUDE_METER_HOST}"
else
  echo "not reachable at http://${CLAUDE_METER_HOST} - is it powered and on the same WiFi?"
fi
echo
echo "Done. Usage appears on the display after your next Claude Code message."
