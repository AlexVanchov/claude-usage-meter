# Hardware

## Board

Developed on a GeekMagic **SmallTV**-style desk display (sold under several names as a
"weather / crypto / GIF clock"):

| Part | Detail |
|---|---|
| MCU | ESP8266EX, 160 MHz, 4 MB flash |
| Display | 1.54" ST7789 IPS, 240×240, SPI (no CS line) |
| USB | CH340 USB-serial (`/dev/cu.usbserial-*` on macOS) |
| Power | USB-C 5 V, no battery |

The stock firmware is recognisable by its web UI (WiFi settings, "GIF Select", weather
icons, BTC price) and boot log `Read WiFi Config..... Connecting.....` at 115200 baud.

## Pinout

| Signal | GPIO | Notes |
|---|---|---|
| SCLK | 14 | hardware SPI |
| MOSI | 13 | hardware SPI |
| DC | 0 | |
| RST | 2 | |
| CS | — | not connected → `SPI_MODE3` required |
| Backlight | 5 | PWM, **active low** |

Configured in `firmware/platformio.ini` (`build_flags`) and `firmware/src/config.h`
(`PIN_BACKLIGHT`, `BACKLIGHT_ACTIVE_LOW`).

### Other boards

Any ESP8266 + ST7789 240×240 should work after adjusting the TFT pins in `platformio.ini`
and the backlight settings in `config.h`. If the screen stays dark, flip
`BACKLIGHT_ACTIVE_LOW`; if colours are inverted add `-D TFT_INVERSION_OFF=1` (or `_ON`);
if red and blue are swapped add `-D TFT_RGB_ORDER=TFT_BGR`. The layout assumes 240×240.

## Backup & restore

Do this **before** the first flash — the stock firmware is not publicly downloadable.

```sh
pip install esptool
PORT=$(ls /dev/cu.usbserial-* | head -1)

# chip info
esptool --port $PORT flash-id

# full 4 MB dump; --after no-reset keeps the chip in the bootloader so the
# stock firmware can't rewrite its WiFi settings sector between read and verify
esptool --port $PORT --baud 460800 --after no-reset read-flash 0 ALL stock-backup.bin
esptool --port $PORT --baud 460800 verify-flash 0 stock-backup.bin
shasum -a 256 stock-backup.bin > stock-backup.bin.sha256
```

Restore:

```sh
esptool --port $PORT --baud 460800 write-flash 0 stock-backup.bin
```

> The dump contains your WiFi credentials (stored by the stock firmware). Keep it private.

If `verify-flash` fails only in the last sectors (`0x3FD000`–`0x3FFFFF`), the stock firmware
booted between read and verify and updated its SDK/WiFi parameters — re-read with
`--after no-reset` as above.

## Flash layout

`eagle.flash.4m2m.ld`: ~1 MB sketch, 2 MB LittleFS (stores `/usage.json`), the rest for OTA
and SDK settings. On first boot the old stock filesystem is reused if it mounts, otherwise
formatted.
