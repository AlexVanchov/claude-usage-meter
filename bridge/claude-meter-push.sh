#!/bin/bash
# Sends the latest recorded Claude usage to the Claude Meter.
# Called by the status line script on every change, and by launchd every 60s
# (keep-alive, so a freshly powered-on display fills within a minute).
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$DIR/meter.env" ] && . "$DIR/meter.env"
HOST="${CLAUDE_METER_HOST:-claude-meter.local}"
LATEST="${CLAUDE_METER_STATE:-$HOME/Library/Application Support/claude-meter/latest.json}"
[ -s "$LATEST" ] || exit 0

/usr/bin/jq -c --argjson now "$(date +%s)" '. + {now: $now}' "$LATEST" |
  /usr/bin/curl -s -m 4 -w "\n" -X POST \
    -H "X-Meter-Token: ${CLAUDE_METER_TOKEN}" -H 'Content-Type: application/json' \
    --data-binary @- "http://$HOST/usage"
