# Firmware (ESP32)

**Status: draft, not yet tested on hardware.** It was written before the board arrived.

## What it does

Connects to WiFi, asks the Mac server for the current song about once a second, downloads the 80x80 cover when it changes, and draws the screen (same layout as `layout_mockup.py`). It only redraws what changed, so it should not flicker. Touching a button should send a command to the Mac (touch is not implemented yet, see below).

## Setup

1. Install Arduino IDE 2.
2. Boards Manager: install **esp32 by Espressif Systems**.
3. Library Manager: install **LovyanGFX** and **ArduinoJson** (version 7).
4. Copy `nowplaying_display/secrets_example.h` to `nowplaying_display/secrets.h` and fill in your WiFi name (2.4 GHz), password, your Mac's IP, and the same access key as `config.py`.
5. Tools, then Board, then **ESP32 Dev Module**. Open `nowplaying_display.ino` and click Verify.

## To check on the real board

- **Backlight pin:** set to GPIO21 (common for this kind of board), not confirmed in Freenove's docs. If the screen stays black, check this first.
- **Touch:** the touch chip and pins are not confirmed for this model, so touch is a stub. The button areas and the command sending are written already.
- **Rotation:** `setRotation(1)` may need to be 3 if the picture is upside down.
- **Colours:** if colours look inverted or red and blue are swapped, change `invert` or `rgb_order` in the screen setup.
- **Fonts:** only basic English letters show. Other characters become "?".

Screen pins (MOSI 13, MISO 12, SCK 14, DC 2, CS 15) come from Freenove's documentation.
