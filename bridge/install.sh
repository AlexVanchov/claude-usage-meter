#!/bin/bash
# Installs the computer side of the Claude Usage Meter (macOS or Linux):
#   - bridge/meter.env           display host + token
#   - keep-alive every 60 s      launchd agent (macOS) / systemd user timer (Linux)
#   - Claude Code statusLine     only with --configure-claude; otherwise prints the snippet
#
# Usage: bridge/install.sh [--configure-claude] [--force]
#   The token is taken from firmware/src/secrets.h, or from $CLAUDE_METER_TOKEN
#   (for extra computers that don't have the firmware secrets).
#   The Claude Code config dir is $CLAUDE_CONFIG_DIR (default ~/.claude).
set -euo pipefail

BRIDGE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$BRIDGE")"
OS="$(uname -s)"
SETTINGS="${CLAUDE_CONFIG_DIR:-$HOME/.claude}/settings.json"
STATUSLINE="$BRIDGE/claude-meter-statusline.sh"
PUSH="$BRIDGE/claude-meter-push.sh"

CONFIGURE=0; FORCE=0
for a in "$@"; do
  case "$a" in
    --configure-claude) CONFIGURE=1 ;;
    --force) FORCE=1 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

step() { printf '\n\033[1;33m==>\033[0m %s\n' "$*"; }

step "Checking dependencies"
for bin in jq curl; do
  command -v "$bin" >/dev/null || { echo "missing: $bin (macOS: brew install $bin, Debian/Ubuntu: sudo apt install $bin)"; exit 1; }
done
echo "ok ($OS)"

step "Configuring bridge/meter.env"
if [ -f "$BRIDGE/meter.env" ]; then
  echo "exists, leaving as is"
else
  token="${CLAUDE_METER_TOKEN:-}"
  if [ -z "$token" ] && [ -f "$REPO/firmware/src/secrets.h" ]; then
    token=$(sed -n 's/^#define METER_TOKEN "\(.*\)"/\1/p' "$REPO/firmware/src/secrets.h")
  fi
  if [ -z "$token" ] || [ "$token" = "change-me" ]; then
    echo "No token: set METER_TOKEN in firmware/src/secrets.h, or run with CLAUDE_METER_TOKEN=<token>." >&2
    exit 1
  fi
  printf 'CLAUDE_METER_HOST=%s\nCLAUDE_METER_TOKEN=%s\n' "${CLAUDE_METER_HOST:-claude-meter.local}" "$token" > "$BRIDGE/meter.env"
  chmod 600 "$BRIDGE/meter.env"
  echo "created"
fi
chmod +x "$BRIDGE"/*.sh

if [ "$OS" = "Darwin" ]; then
  LABEL="com.claude-meter.push"
  PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
  LOG="$HOME/Library/Logs/claude-meter-push.log"
  step "Installing launchd agent $LABEL"
  mkdir -p "$(dirname "$PLIST")" "$(dirname "$LOG")"
  sed -e "s|__PUSH_SCRIPT__|$PUSH|" -e "s|__LOG__|$LOG|g" \
    "$BRIDGE/com.claude-meter.push.plist.template" > "$PLIST"
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
  launchctl bootstrap "gui/$(id -u)" "$PLIST"
  echo "loaded: $PLIST (log: $LOG)"
else
  UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
  step "Installing systemd user timer claude-meter-push.timer"
  command -v systemctl >/dev/null || { echo "systemd not found; run $PUSH from cron every minute instead"; exit 1; }
  mkdir -p "$UNIT_DIR"
  sed -e "s|__PUSH_SCRIPT__|$PUSH|" "$BRIDGE/claude-meter-push.service.template" > "$UNIT_DIR/claude-meter-push.service"
  cp "$BRIDGE/claude-meter-push.timer.template" "$UNIT_DIR/claude-meter-push.timer"
  systemctl --user daemon-reload
  systemctl --user enable --now claude-meter-push.timer
  echo "enabled (logs: journalctl --user -u claude-meter-push)"
  if [ "$(loginctl show-user "$USER" -p Linger --value 2>/dev/null)" != "yes" ]; then
    echo "note: to keep it running while you're logged out: sudo loginctl enable-linger $USER"
  fi
fi

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
  echo "not reachable at http://${CLAUDE_METER_HOST} - is it powered and on the same network?"
  echo "(if .local names don't resolve here, set CLAUDE_METER_HOST to the display's IP in bridge/meter.env)"
fi
echo
echo "Done. Usage appears on the display after your next Claude Code message."
