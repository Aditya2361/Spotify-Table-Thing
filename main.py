import os
import time

from PIL import Image

import server
from nowplaying import get_state
from covers import get_cover
from layout_mockup import render_screen, W, H

POLL_SECONDS = 1
GAP_SECONDS = 3       
MAX_ART_TRIES = 5 


def save_preview(screen):
    screen.save("live_preview_tmp.png")
    os.replace("live_preview_tmp.png", "live_preview.png")
    screen.resize((W * 2, H * 2), Image.Resampling.NEAREST).save("live_preview_2x_tmp.png")
    os.replace("live_preview_2x_tmp.png", "live_preview_2x.png")


def wait(loop_start):
    time.sleep(max(0, POLL_SECONDS - (time.time() - loop_start)))


def main():
    current_key = None
    cover = None
    art_tries = 0
    idle = False
    last_seen = time.time()

    server.start()
    print("Running. Press Ctrl+C to stop.")

    while True:
        loop_start = time.time()
        song = get_state()

        if song is None:
            if not idle and time.time() - last_seen > GAP_SECONDS:
                save_preview(render_screen(None))
                server.set_idle()
                idle = True
                current_key = None
                print("Nothing playing")
            wait(loop_start)
            continue

        idle = False
        last_seen = time.time()

        key = (song["title"], song["artist"])
        if key != current_key:
            current_key = key
            cover = None
            art_tries = 0
            print("Now playing:", song["title"], "-", song["artist"])

        if cover is None and art_tries < MAX_ART_TRIES:
            art_tries += 1
            full = get_state(with_art=True)
            if full and (full["title"], full["artist"]) == key and full["cover"] is not None:
                cover, source = get_cover(song["title"], song["artist"], full["cover"])
                print("  cover source:", source)

        song["cover"] = cover
        save_preview(render_screen(song))
        server.update(song, cover)
        wait(loop_start)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")