from PIL import Image, ImageOps

COVER_SIZE = (80, 80)

_cache = {}   


def get_cover(title, artist, thumb):
    key = (title, artist)
    if key in _cache:
        return _cache[key], "cache"
    if thumb is None:
        return None, "none"  

    img = ImageOps.fit(thumb, COVER_SIZE, Image.Resampling.LANCZOS)
    _cache[key] = img
    return img, "Mac thumbnail"


if __name__ == "__main__":
    from nowplaying import get_state

    song = get_state(with_art=True)
    if song and song["cover"]:
        cover, source = get_cover(song["title"], song["artist"], song["cover"])
        cover.save("cover_test.png")
        cover.resize((320, 320), Image.Resampling.NEAREST).save("cover_test_big.png")
        print("Saved cover_test.png and cover_test_big.png from:", source)
    else:
        print("No song or no artwork")