#!/usr/bin/env python3
# Convert an image (PNG, JPG, ...) into the LVGL 7 binary format for an 'img' object of openHASP:
#   ./img2bin.py <image> <out.bin> [width height]
#   {"page":1,"id":5,"obj":"img","x":10,"y":20,"src":"L:/out.bin"}
# No image decoder of the firmware needed (PNG/JPG decoders are off in the build), RGB565 without
# byte swap (LV_COLOR_16_SWAP 0); with alpha channel TRUE_COLOR_ALPHA (3 bytes per pixel), otherwise
# TRUE_COLOR (2 bytes). Large motives as image: the firmware draws MDI symbols up to 120 px only.

import struct
import sys

from PIL import Image

LV_IMG_CF_TRUE_COLOR       = 4
LV_IMG_CF_TRUE_COLOR_ALPHA = 5


def convert(img, path):
    alpha = img.mode in ("RGBA", "LA") or "transparency" in img.info
    img = img.convert("RGBA" if alpha else "RGB")
    w, h = img.size
    cf = LV_IMG_CF_TRUE_COLOR_ALPHA if alpha else LV_IMG_CF_TRUE_COLOR
    data = bytearray(struct.pack("<I", cf | (w << 10) | (h << 21)))   # lv_img_header_t: cf:5 x:3 res:2 w:11 h:11

    for px in img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata():
        r, g, b = px[0], px[1], px[2]
        data += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        if alpha:
            data.append(px[3])

    with open(path, "wb") as f:
        f.write(data)
    return len(data)


if __name__ == "__main__":
    if len(sys.argv) not in (3, 5):
        sys.exit(__doc__ or "usage: img2bin.py <image> <out.bin> [width height]")
    img = Image.open(sys.argv[1])
    if len(sys.argv) == 5:
        img = img.resize((int(sys.argv[3]), int(sys.argv[4])), Image.LANCZOS)
    print("%s: %dx%d, %d bytes" % (sys.argv[2], img.size[0], img.size[1], convert(img, sys.argv[2])))
