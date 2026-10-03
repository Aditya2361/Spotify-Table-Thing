import base64
import io
import json
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont, ImageOps



PREFIX = "kMRMediaRemoteNowPlayingInfo"
FONT_PATH = "/System/Library/Fonts/Helvetica.ttc"

W, H = 320, 240          
MARGIN = 12
COVER = 80               
TOP_END = H // 2                 
PROG_END = H // 2 + H // 4       
                                 
BAR_H = 6

BG = "#000000"
TEXT = "#FFFFFF"
SUBTEXT = "#B3B3B3"
TRACK = "#404040"

HIT_W, HIT_H = 64, 56   
SHOW_TOUCH_ZONES = "zones" in sys.argv



def get_now_playing():
    result = subprocess.run(["media-control", "get", "--now"], capture_output=True, text=True)
    try:
        raw = json.loads(result.stdout)
    except json.JSONDecodeError:
        return None
    if not isinstance(raw, dict) or not raw.get("title"):
        return None

    cover = None
    art = raw.get("artworkData")
    if art:
        try:
            cover = Image.open(io.BytesIO(base64.b64decode(art))).convert("RGB")
        except Exception:
            cover = None

    return {
        "title": raw["title"],
        "artist": raw.get("artist", ""),
        "duration": raw.get("duration", 0),
        "elapsed": raw.get("elapsedTimeNow", raw.get("elapsedTime", 0)),
        "playing": bool(raw.get("playing", raw.get("playbackRate", 0) > 0)),
        "cover": cover,
    }



def load_font(size, bold=False):
    try:
        return ImageFont.truetype(FONT_PATH, size, index=1 if bold else 0)
    except OSError:
        return ImageFont.load_default(size)


def fit(draw, text, font, max_w):

    if draw.textlength(text, font=font) <= max_w:
        return text
    while len(text) > 1 and draw.textlength(text + "...", font=font) > max_w:
        text = text[:-1]
    return text + "..."


def mmss(seconds):
    seconds = int(seconds)
    return f"{seconds // 60}:{seconds % 60:02d}"



def draw_prev(d, cx, cy):
    d.rectangle((cx - 12, cy - 9, cx - 9, cy + 9), fill=TEXT)
    d.polygon([(cx + 11, cy - 9), (cx + 11, cy + 9), (cx - 7, cy)], fill=TEXT)


def draw_next(d, cx, cy):
    d.polygon([(cx - 11, cy - 9), (cx - 11, cy + 9), (cx + 7, cy)], fill=TEXT)
    d.rectangle((cx + 9, cy - 9, cx + 12, cy + 9), fill=TEXT)


def draw_play_pause(d, cx, cy, playing):
    d.ellipse((cx - 24, cy - 24, cx + 24, cy + 24), outline=TEXT, width=2)
    if playing:   # show the PAUSE icon while playing
        d.rectangle((cx - 8, cy - 11, cx - 3, cy + 11), fill=TEXT)
        d.rectangle((cx + 3, cy - 11, cx + 8, cy + 11), fill=TEXT)
    else:         # show the PLAY icon while paused
        d.polygon([(cx - 7, cy - 11), (cx - 7, cy + 12), (cx + 12, cy)], fill=TEXT)


def render_screen(song):
    screen = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(screen)

    if song is None:
        d.text((W // 2, H // 2), "Nothing playing", font=load_font(20, True),
               fill=SUBTEXT, anchor="mm")
        return screen

    
    cover_y = (TOP_END - COVER) // 2
    if song["cover"]:
        cover = ImageOps.fit(song["cover"], (COVER, COVER), Image.Resampling.LANCZOS)
        screen.paste(cover, (MARGIN, cover_y))
    else:
        d.rectangle((MARGIN, cover_y, MARGIN + COVER, cover_y + COVER), fill=TRACK)

    
    text_x = MARGIN + COVER + MARGIN
    text_w = W - MARGIN - text_x
    title_font = load_font(22, bold=True)
    artist_font = load_font(16)
    block_h = 22 + 6 + 16
    text_y = cover_y + (COVER - block_h) // 2
    d.text((text_x, text_y), fit(d, song["title"], title_font, text_w),
           font=title_font, fill=TEXT)
    d.text((text_x, text_y + 28), fit(d, song["artist"], artist_font, text_w),
           font=artist_font, fill=SUBTEXT)

    # ---- Progress zone: bar with times underneath ----
    time_font = load_font(12)
    block_h = BAR_H + 6 + 12
    bar_y = TOP_END + (PROG_END - TOP_END - block_h) // 2
    bar_x0, bar_x1 = MARGIN, W - MARGIN

    fraction = 0
    if song["duration"] > 0:
        fraction = max(0, min(1, song["elapsed"] / song["duration"]))

    d.rounded_rectangle((bar_x0, bar_y, bar_x1, bar_y + BAR_H), radius=BAR_H // 2, fill=TRACK)
    fill_x = bar_x0 + int((bar_x1 - bar_x0) * fraction)
    if fill_x > bar_x0:
        d.rounded_rectangle((bar_x0, bar_y, max(fill_x, bar_x0 + BAR_H), bar_y + BAR_H),
                            radius=BAR_H // 2, fill=TEXT)

    time_y = bar_y + BAR_H + 6
    d.text((bar_x0, time_y), mmss(song["elapsed"]), font=time_font, fill=SUBTEXT)
    d.text((bar_x1, time_y), mmss(song["duration"]), font=time_font, fill=SUBTEXT, anchor="ra")

    cy = PROG_END + (H - PROG_END) // 2
    buttons = [W // 4, W // 2, 3 * W // 4]
    draw_prev(d, buttons[0], cy)
    draw_play_pause(d, buttons[1], cy, song["playing"])
    draw_next(d, buttons[2], cy)

    if SHOW_TOUCH_ZONES:
        for cx in buttons:
            d.rectangle((cx - HIT_W // 2, cy - HIT_H // 2, cx + HIT_W // 2, cy + HIT_H // 2),
                        outline="#FF4444")

    return screen



if __name__ == "__main__":
    song = get_now_playing()
    if song:
        print("Playing:", song["title"], "-", song["artist"])
    else:
        print("Nothing playing")

    screen = render_screen(song)
    screen.save("layout_mockup.png")
    screen.resize((W * 2, H * 2), Image.Resampling.NEAREST).save("layout_mockup_2x.png")
    print("Saved layout_mockup.png and layout_mockup_2x.png")