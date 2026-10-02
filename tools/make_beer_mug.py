#!/usr/bin/env python3
"""🍺 (Noto Color Emoji) を「ガラス」「ビール液」「泡」の3層に分け、
M5GFX 用の RGB565 配列として BeerMug.h に書き出す。

使い方: python3 tools/make_beer_mug.py  (スケッチフォルダで実行)
必要: Pillow, /usr/share/fonts/truetype/noto/NotoColorEmoji.ttf
出力: BeerMug.h, tools/beer_mug_preview.png (液面の高さ・傾きの確認用)
元絵: Noto Color Emoji (SIL Open Font License 1.1)
"""
import colorsys
import math
import os
import sys
from PIL import Image, ImageDraw, ImageFont

SIZE = 44                    # 実機で描く1辺のピクセル数
FONT = "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(HERE, "..", "BeerMug.h")
OUT_PNG = os.path.join(HERE, "beer_mug_preview.png")

TINT = (34, 42, 52)          # 空になったジョッキの内側の色
CREAM = (255, 244, 214)      # 液面の泡の色
CAP_MIN_LEVEL = 0.85         # この量以上のときだけ、大きな泡の山を出す


def render_emoji():
    font = ImageFont.truetype(FONT, 109)
    c = Image.new("RGBA", (160, 160), (0, 0, 0, 0))
    ImageDraw.Draw(c).text((10, 10), "\U0001F37A", font=font, embedded_color=True)
    c = c.crop(c.getbbox())
    side = max(c.size)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(c, ((side - c.width) // 2, (side - c.height) // 2))
    return sq


def split_layers(im):
    w, h = im.size
    layers = {k: Image.new("RGBA", (w, h), (0, 0, 0, 0)) for k in ("glass", "liquid", "foam")}
    for y in range(h):
        for x in range(w):
            r, g, b, a = im.getpixel((x, y))
            if a < 40:
                continue
            hh, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            amber = 0.05 <= hh <= 0.17 and s >= 0.5
            if amber and y > 0.30 * h:
                k = "liquid"
            elif y < 0.50 * h and x < 0.72 * w and v > 0.80 and s < 0.55:
                k = "foam"            # 白い泡と縁のうす茶色 (取っ手のハイライトは除く)
            elif y < 0.27 * h:
                continue              # 泡の縁の細かいゴミ。泡を消したとき宙に浮くので捨てる
            else:
                k = "glass"
            layers[k].putpixel((x, y), (r, g, b, a))
    return layers


def to_small(layer):
    small = layer.resize((SIZE, SIZE), Image.LANCZOS)
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            r, g, b, a = small.getpixel((x, y))
            if a < 48:
                row.append(None)
            else:
                k = a / 255.0
                row.append((int(r * k), int(g * k), int(b * k)))
        rows.append(row)
    return rows


def rgb565(rgb):
    r, g, b = rgb
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return v or 1                 # 0x0000 は透過キーなので避ける


def swap16(v):
    return ((v & 0xFF) << 8) | (v >> 8)   # M5GFX は uint16_t* を swap565 として読む


def c_array(name, rows):
    vals = [0 if p is None else swap16(rgb565(p)) for row in rows for p in row]
    lines = ["  " + ", ".join("0x%04X" % v for v in vals[i:i + 11]) + "," for i in range(0, len(vals), 11)]
    return "static const uint16_t %s[%d] = {\n%s\n};\n" % (name, len(vals), "\n".join(lines))


def extents(liquid_rows):
    top = bot = None
    mins, maxs = [], []
    for y, row in enumerate(liquid_rows):
        xs = [x for x, p in enumerate(row) if p is not None]
        if xs:
            top = y if top is None else top
            bot = y + 1
            mins.append(min(xs)); maxs.append(max(xs))
        else:
            mins.append(255); maxs.append(0)
    return top, bot, mins, maxs


def compose(layers, ext, level, angle_deg):
    """BeerMug.h を使う C++ (DrunkMouth::renderMug) と同じ手順のプレビュー"""
    top, bot, mins, maxs = ext
    img = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
    px = img.load()
    for y in range(top, bot):
        for x in range(mins[y], maxs[y] + 1):
            px[x, y] = TINT
    if level > 0.01:
        sy = bot - int(round(level * (bot - top)))
        for y in range(sy, SIZE):
            for x in range(SIZE):
                if layers["liquid"][y][x] is not None:
                    px[x, y] = layers["liquid"][y][x]
        if level >= CAP_MIN_LEVEL:
            for y in range(SIZE):
                for x in range(SIZE):
                    if layers["foam"][y][x] is not None:
                        px[x, y] = layers["foam"][y][x]
        else:
            for y in range(sy, min(sy + 3, bot)):
                for x in range(mins[y], maxs[y] + 1):
                    px[x, y] = CREAM
    for y in range(SIZE):
        for x in range(SIZE):
            if layers["glass"][y][x] is not None:
                px[x, y] = layers["glass"][y][x]
    if angle_deg:
        img = img.rotate(-angle_deg, resample=Image.BILINEAR)   # PIL は反時計回りが正
    return img


def main():
    if not os.path.exists(FONT):
        sys.exit("font not found: " + FONT)
    layers_hi = split_layers(render_emoji())
    layers = {k: to_small(v) for k, v in layers_hi.items()}
    ext = extents(layers["liquid"])
    top, bot, mins, maxs = ext

    with open(OUT_H, "w") as f:
        f.write("// 🍺 を「ガラス」「ビール液」「泡」の3層に分けた %dx%d の RGB565 画像。\n" % (SIZE, SIZE))
        f.write("// tools/make_beer_mug.py で生成 (直接編集しない)。元絵: Noto Color Emoji (SIL OFL 1.1)\n")
        f.write("// M5GFX が uint16_t* を swap565 として解釈するため、各値は上下バイト入れ替え済み。透過キーは 0x0000。\n\n")
        f.write("#ifndef BEER_MUG_H_\n#define BEER_MUG_H_\n\n#include <stdint.h>\n\n")
        f.write("static const int16_t kMugSize = %d;\n" % SIZE)
        f.write("// ビール液が入る行の範囲 [kMugLiquidTop, kMugLiquidBottom)\n")
        f.write("static const int16_t kMugLiquidTop = %d;\nstatic const int16_t kMugLiquidBottom = %d;\n" % (top, bot))
        f.write("// 各行でビール液が入る x の範囲 (液の無い行は min=255, max=0)\n")
        f.write("static const uint8_t kMugRowMin[%d] = {%s};\n" % (SIZE, ", ".join(map(str, mins))))
        f.write("static const uint8_t kMugRowMax[%d] = {%s};\n\n" % (SIZE, ", ".join(map(str, maxs))))
        for key, name in (("glass", "kMugGlass"), ("liquid", "kMugLiquid"), ("foam", "kMugFoam")):
            f.write(c_array(name, layers[key]) + "\n")
        f.write("#endif  // BEER_MUG_H_\n")

    cases = [(1.0, 0), (0.83, 0), (0.5, 0), (0.17, 0), (0.0, 0), (0.5, -65)]
    sheet = Image.new("RGB", ((SIZE + 6) * len(cases), SIZE + 4), (0, 0, 0))
    for i, (lv, ang) in enumerate(cases):
        sheet.paste(compose(layers, ext, lv, ang), (i * (SIZE + 6) + 3, 2))
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(OUT_PNG)
    print("liquid rows", top, bot, "->", OUT_H)


if __name__ == "__main__":
    main()
