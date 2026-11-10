# Display design

Dark, minimal UI with one warm-orange accent, adapted from a 240×280 spec to the 240×240 panel.

## Layout (240×240)

| Block | y | height | Contents |
|---|---|---|---|
| Header | 4 | 30 | mascot 24×18 · clock (Inter Medium 28, tabular digits) · stale dot · battery |
| Card "Current" | 40 | 76 | `65%` (Inter Bold 26) + pill · progress bar 8 px · "Resets in 2h 23m" (Inter 13) |
| Card "Weekly" | 122 | 76 | same structure, 7-day window |
| Footer | 206 | 28 | mini mascot · `Reticulating...` (JetBrains Mono 14) · radial spinner |

Outer padding 8 px, card radius 8, card padding 10, gaps 6 px.

## Palette

| Role | Colour |
|---|---|
| Accent | `#F07A2B` |
| Accent > 80 % | `#F5A524` (amber) |
| Accent > 95 % | `#E5484D` (red) |
| Background | `#0E0E0E` |
| Card | `#1C1C1C` |
| Progress track | `#2E2E2E` |
| Pill bg / text | `#2A2A2A` / `#D8D8D8` |
| Text / secondary | `#FFFFFF` / `#BDBDBD` |
| Battery outline | `#CFCFCF` |

## Animation

- Spinner: 10 dashes, rotating every 80 ms with a fading tail.
- Ellipsis `.` `..` `...` every 400 ms (width reserved so text doesn't shift).
- Status word rotates every 4 s: Reticulating, Computing, Pondering, Noodling, Brewing,
  Tinkering, Synthesizing, Percolating, Musing, Crunching, Conjuring, Cogitating.
  Shows "Connecting" without WiFi and "Awaiting data" before the first update.

## Idle scene

24×24 pixel art at 8× (192×192) on black: orange creature (`#F07A2B` / `#E85A2A`) with
lavender headphones (`#A9AEE6` / `#8A8FC4`) behind a grey laptop (`#7E7E8C`, highlight
`#B4B8E8`) on a desk line (`#2A2630`). Animated typing hands with pauses, and a blink every
2.5–6 s. Only changed pixels are redrawn.

| Frame | |
|---|---|
| base | ![](images/idle_0.png) |
| blink | ![](images/idle_1.png) |
| typing | ![](images/idle_2.png) ![](images/idle_3.png) |

Header mascot ![](images/mascot.png) and footer mascot ![](images/mascot_mini.png).

## Fonts

Anti-aliased bitmaps rendered from Inter and JetBrains Mono by `tools/gen_assets.py` in
TFT_eSPI's smooth-font (`.vlw`) format and compiled into flash (~53 KB). Digits in the
clock and percentage fonts are forced to equal width so numbers don't jitter.

## Deviations from the original spec

- 240×240 instead of 240×280: tighter vertical spacing (header 30, cards 76, footer 28).
- Battery icon is drawn full — the board is USB-powered with no battery sensing.
- No background vignette.
