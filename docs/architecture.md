# Architecture

```
 MacBook                                                         Display (ESP8266, WiFi)
 ┌─────────────────────────────────────────────────┐             ┌────────────────────────────┐
 │ Claude Code                                     │             │ HTTP server :80            │
 │   │ statusLine stdin JSON (incl. rate_limits)    │             │                            │
 │   ▼                                             │             │ POST /usage                │
 │ bridge/claude-meter-statusline.sh               │             │  ├─ check X-Meter-Token    │
 │   ├─ writes ──▶ latest.json                     │             │  ├─ update state           │
 │   ├─ if changed ──▶ claude-meter-push.sh ───────┼── HTTP ────▶│  ├─ save to flash (change) │
 │   └─ prints the status line                     │   WiFi      │  └─ redraw changed regions │
 │                                                 │             │                            │
 │ launchd: com.claude-meter.push (every 60 s)     │             │ NTP clock (pool.ntp.org)   │
 │   └──────────────▶ claude-meter-push.sh ────────┼────────────▶│ mDNS: claude-meter.local   │
 └─────────────────────────────────────────────────┘             └────────────────────────────┘
```

## 1. Source of truth: Claude Code

Every API response Claude Code receives for a subscription login carries the account's
rate-limit state. Claude Code exposes it to the configured **status line command** on stdin:

```json
{
  "model": { "display_name": "Opus" },
  "rate_limits": {
    "five_hour": { "used_percentage": 11, "resets_at": 1794334200 },
    "seven_day": { "used_percentage": 1,  "resets_at": 1794909600 }
  }
}
```

- `used_percentage` — 0–100.
- `resets_at` — Unix epoch seconds when the window resets.
- A window is omitted when the API doesn't report it or its reset time has passed.

Nothing in this project reads Claude credentials or calls Anthropic APIs directly.

## 2. Mac bridge

### `claude-meter-statusline.sh` (runs inside Claude Code)

1. Extracts `rate_limits.five_hour` / `seven_day` with `jq`.
2. Writes them with the capture time to
   `~/Library/Application Support/claude-meter/latest.json`:
   ```json
   {"captured_at":1794317920,"rate_limits":{"five_hour":{...},"seven_day":{...}}}
   ```
3. Starts `claude-meter-push.sh` in the background (detached, so the status line never
   waits on the network) when the numbers changed, and otherwise at most every 15 s — so
   the display also learns that you are active when the percentages stay the same.
4. Prints `Opus · 5h 11% · 7d 1%` as the status line.

### `claude-meter-push.sh`

Adds `now` to `latest.json` and POSTs it to `http://$CLAUDE_METER_HOST/usage` with the
token from `bridge/meter.env` (4 s timeout).

### LaunchAgent `com.claude-meter.push`

Runs `claude-meter-push.sh` every 60 s and at login. This is the **keep-alive**: after a
power cut the display has its numbers back within a minute even if Claude Code is idle.
Log: `~/Library/Logs/claude-meter-push.log`.

## 3. The request

```http
POST /usage HTTP/1.1
Host: claude-meter.local
X-Meter-Token: <METER_TOKEN>
Content-Type: application/json

{
  "captured_at": 1794317920,
  "now": 1794317980,
  "rate_limits": {
    "five_hour": { "used_percentage": 11, "resets_at": 1794334200 },
    "seven_day": { "used_percentage": 1,  "resets_at": 1794909600 }
  }
}
```

| Field | Required | Meaning |
|---|---|---|
| `rate_limits.five_hour` / `seven_day` | at least one | as reported by Claude Code; a top-level `five_hour`/`seven_day` without the `rate_limits` wrapper is accepted too |
| `captured_at` | no | when Claude Code reported the numbers; defaults to `now` |
| `now` | no | sender's clock; used to set the device clock if NTP hasn't synced, and to compute data age |
| `wake` | no | `true` leaves the idle screen even if nothing changed |

Responses:

| Status | Body |
|---|---|
| 200 | `{"ok":true,"changed":true\|false}` |
| 400 | `{"ok":false,"error":"<json parse error>"}` |
| 401 | `{"ok":false,"error":"bad token"}` |

`GET /usage` returns the device state:

```json
{"five_hour":{"valid":true,"used_percentage":11,"resets_at":1794334200},
 "seven_day":{"valid":true,"used_percentage":1,"resets_at":1794909600},
 "have_data":true,"seconds_since_update":12,"idle":false,"time_valid":true,
 "now":1794317990,"free_heap":28520,"rssi":-68}
```

## 4. Firmware

| Concern | Implementation |
|---|---|
| Networking | `ESP8266WiFi` (auto-reconnect), mDNS `claude-meter.local`, `ESP8266WebServer` |
| Clock | `configTime()` with POSIX TZ from `secrets.h`; NTP from `pool.ntp.org` / `time.google.com` |
| Parsing | ArduinoJson 7 |
| Persistence | LittleFS `/usage.json`, written only when values change (flash wear) |
| Rendering | TFT_eSPI; each region drawn into a small off-screen sprite and pushed in one go (no flicker, fits ESP8266 RAM) |
| Updates | ArduinoOTA, password = `METER_TOKEN` |

### State and timing

- **Change detection** — an update "changes" a window when its rounded percentage or
  `resets_at` differs. Changes redraw and are saved to flash.
- **Activity** — a report with a newer `captured_at` than any seen before (i.e. Claude Code
  ran its status line again) counts as activity even if the numbers are unchanged.
  Keep-alive resends repeat the old `captured_at`, so they don't.
- **Data age** — `seconds_since_update = now − captured_at`. Keep-alive resends carry the
  original `captured_at`, so they never make old data look fresh.
- **Stale dot** — red when data age > `STALE_AFTER_MIN` (15 min); grey before any data.
- **After boot** — numbers restored from flash are treated as stale until the Mac reports in.
- **Window rollover** — if `resets_at` has passed, the card shows 0 % and "Window reset".
- **Idle** — no activity or change for `IDLE_AFTER_MIN` (10 min) → pixel-art scene; the next
  Claude Code report wakes it.

## 5. Security notes

- Only usage percentages and reset times leave the Mac; nothing else from Claude Code's
  status line input (paths, session ids, costs) is sent.
- The device accepts updates only with the shared `METER_TOKEN` (also the OTA password).
  Traffic is plain HTTP on the local network — treat the token as a LAN-level secret.
- `secrets.h` and `bridge/meter.env` are git-ignored.
