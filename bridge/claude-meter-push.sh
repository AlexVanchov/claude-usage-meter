#!/bin/bash
# Sends the latest recorded Claude usage to the Claude Meter.
# Called by the status line script, and every 60 s by launchd (macOS) or a
# systemd user timer (Linux) as a keep-alive, so a freshly powered-on display
# fills within a minute.
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"
[ -s "$CLAUDE_METER_LATEST" ] || exit 0

jq -c --argjson now "$(date +%s)" '. + {now: $now}' "$CLAUDE_METER_LATEST" |
  curl -s -m 4 -w "\n" -X POST \
    -H "X-Meter-Token: ${CLAUDE_METER_TOKEN:-}" -H 'Content-Type: application/json' \
    --data-binary @- "http://$CLAUDE_METER_HOST/usage"
