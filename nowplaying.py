import base64
import io
import json
import subprocess
import time

from PIL import Image

SKIP_ARTWORK_FLAG = True


def get_state(with_art=False):
    cmd = ["media-control", "get", "--now"]
    if not with_art and SKIP_ARTWORK_FLAG:
        cmd.append("--no-artwork")

    result = subprocess.run(cmd, capture_output=True, text=True)
    try:
        raw = json.loads(result.stdout)
    except json.JSONDecodeError:
        return None
    if not isinstance(raw, dict) or not raw.get("title"):
        return None

    cover = None
    if with_art and raw.get("artworkData"):
        try:
            cover = Image.open(io.BytesIO(base64.b64decode(raw["artworkData"]))).convert("RGB")
        except Exception:
            cover = None

    return {
        "title": raw["title"],
        "artist": raw.get("artist", ""),
        "album": raw.get("album", ""),
        "duration": raw.get("duration", 0),
        "elapsed": raw.get("elapsedTimeNow", raw.get("elapsedTime", 0)),
        "playing": bool(raw.get("playing", raw.get("playbackRate", 0) > 0)),
        "cover": cover,
    }


if __name__ == "__main__":
    while True:
        start = time.time()
        song = get_state()
        took = time.time() - start
        if song:
            state = "playing" if song["playing"] else "paused"
            print(f'{song["title"]} - {song["artist"]} | '
                  f'{song["elapsed"]:.0f}s / {song["duration"]:.0f}s | {state} | {took:.2f}s')
        else:
            print("Nothing playing")
        time.sleep(1)