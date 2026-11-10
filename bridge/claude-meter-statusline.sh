#!/bin/bash
# Claude Code statusLine command.
# Claude Code pipes session JSON (incl. rate_limits.five_hour / seven_day) to stdin.
# 1. Record the latest numbers + capture time in latest.json
# 2. Push them to the Claude Meter (on change, else at most every 15 s; non-blocking)
# 3. Print a short status line
input=$(cat)
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STATE_DIR="$HOME/Library/Application Support/claude-meter"
LATEST="$STATE_DIR/latest.json"

rl=$(printf '%s' "$input" | jq -c '(.rate_limits // {}) | {five_hour, seven_day} | with_entries(select(.value != null))' 2>/dev/null)
if [ -n "$rl" ] && [ "$rl" != "{}" ]; then
  mkdir -p "$STATE_DIR"
  prev=$(jq -c '.rate_limits' "$LATEST" 2>/dev/null)
  printf '{"captured_at":%s,"rate_limits":%s}\n' "$(date +%s)" "$rl" > "$LATEST.tmp" && mv "$LATEST.tmp" "$LATEST"
  # push on change, and otherwise at most every 15 s so the display knows you're active
  now=$(date +%s)
  last_push=$(cat "$STATE_DIR/last_push" 2>/dev/null || echo 0)
  if [ "$rl" != "$prev" ] || [ $((now - last_push)) -ge 15 ]; then
    echo "$now" > "$STATE_DIR/last_push"
    ( "$DIR/claude-meter-push.sh" </dev/null >/dev/null 2>&1 & )
  fi
fi

printf '%s' "$input" | jq -r '
  def pct(k; name): (.rate_limits[k].used_percentage // null) | if . == null then empty else "\(name) \(. | round)%" end;
  [ (.model.display_name // empty), pct("five_hour"; "5h"), pct("seven_day"; "7d") ] | join(" · ")' 2>/dev/null
