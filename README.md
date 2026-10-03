# Spotify Table Thing

A small desk display that shows what is playing on my Mac: album cover, song title, artist, a live progress bar, and play/pause, back and next buttons on a 2.8" colour touch screen.

## Why this design

Spotify's Web API now needs a Premium account for developers, and I don't have one. So this project doesn't use Spotify's API at all. A Python script on my Mac reads the system's "Now Playing" information (the same thing shown in Control Center), so it works with Spotify in a browser, and also with YouTube and other audio on websites.

## How it works

```
Mac (Python)                          ESP32 display board
------------                          -------------------
reads "Now Playing"  --> web server <-- asks for song info about once a second
(title, artist, time,      on home      draws the screen
 cover, play state)        WiFi         sends touch commands back
                                        (play/pause, next, back)
```

The Mac does the heavy work (reading the song, shrinking the cover to 80x80). The board only has to draw. The server only answers requests that include a secret access key.

## Hardware

- Freenove ESP32 Display 2.8" (FNK0114F): ESP32 with a built-in 240x320 ILI9341 touch screen
- USB-C data cable
- Case: hand-built from cardboard, plastic and paint

## Files

| File | What it does |
|---|---|
| `main.py` | Runs everything in a loop |
| `nowplaying.py` | Reads the current song using `media-control` |
| `covers.py` | Turns the cover into an 80x80 image |
| `layout_mockup.py` | Draws the 320x240 screen image (a preview on the Mac) |
| `server.py` | The web server for the board, with access key check |
| `config_example.py` | Template for your own `config.py` |
| `firmware/` | ESP32 code (draft, see `firmware/README.md`) |

## Setup (Mac)

1. Install [Homebrew](https://brew.sh), then the Now Playing tool:
   ```
   brew tap ungive/media-control
   brew install media-control
   ```
2. Get the code and install the library:
   ```
   git clone https://github.com/Aditya2361/Spotify-Table-Thing.git
   cd nowplaying-desk
   python3 -m venv .venv
   source .venv/bin/activate
   pip install pillow
   ```
3. Make your own key and config:
   ```
   cp config_example.py config.py
   python3 -c "import secrets; print(secrets.token_urlsafe(16))"
   ```
   Paste the key into `config.py`. Never commit this file.
4. Play something, then run:
   ```
   python3 main.py
   ```
   macOS may ask to allow incoming connections. Click Allow so the board can reach the server.

While it runs, it saves `live_preview.png`, an image of what the screen will show.

## Server addresses

Every address needs `?key=YOUR-KEY` on the end.

| Address | What it does |
|---|---|
| `GET /state` | Song info as JSON |
| `GET /cover.png` | The 80x80 cover as a PNG (for checking in a browser) |
| `GET /cover.raw` | The cover as raw pixels (RGB565) for the board |
| `POST /cmd/toggle` | Play / pause |
| `POST /cmd/next` | Next song |
| `POST /cmd/previous` | Previous song |

Example: `curl -X POST "http://localhost:8000/cmd/toggle?key=YOUR-KEY"`

To reach it from another device, use your Mac's IP address (find it with `ipconfig getifaddr en0`) on the same WiFi.


## Credits

- [mediaremote-adapter](https://github.com/ungive/mediaremote-adapter) and `media-control` by ungive, for reading Now Playing on recent macOS
- [Pillow](https://python-pillow.org) for image handling
- Claude (AI) debug parts of this project
