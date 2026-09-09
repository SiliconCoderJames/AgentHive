# -*- coding: utf-8 -*-
"""重绘 AgentHive 品牌图标：全出血圆角贴片（角部真透明、无白边距）+ 蜂巢三六边形 + 入口蓝点。

几何与 docs/assets/logo.svg 完全一致；产物：
  docs/assets/logo.png  256px 全出血透明 PNG（GUI 窗口图标）
  src/gui/icon.ico      256px PNG 压缩 ICO（exe 资源图标，app.rc 引用）
不要用无头浏览器截图 SVG 生成图标——透明边距会被渲染成白底，任务栏里出现白边。

用法：在仓库根目录执行  python scripts/gen_icon.py   （需要 Pillow）
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw

if not Path("src/gui/app.rc").is_file():
    raise SystemExit("请在仓库根目录执行：python scripts/gen_icon.py")

PNG_PATH = Path("docs/assets/logo.png")
ICO_PATH = Path("src/gui/icon.ico")

S = 256
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# 圆角贴片 r=52（与 svg rx=48/240 同比例），贴片满幅铺满、四角透明
d.rounded_rectangle([0, 0, S - 1, S - 1], radius=52, fill=(0x1E, 0x1E, 0x1E, 255))

AMBER = (0xF5, 0x9E, 0x0B, 255)
hexes = [
    [(128, 56), (162.6, 76), (162.6, 116), (128, 136), (93.4, 116), (93.4, 76)],
    [(93.4, 116), (128, 136), (128, 176), (93.4, 196), (58.8, 176), (58.8, 136)],
    [(162.6, 116), (197.2, 136), (197.2, 176), (162.6, 196), (128, 176), (128, 136)],
]
# 每个六边形独立绘制（跨多边形连线会出现假对角线）；顶点补 radius=5 圆点，
# 等效 SVG 的 stroke-linejoin/linecap="round"（width=10 → 半径 5）
for poly in hexes:
    d.line([tuple(p) for p in poly + [poly[0]]], fill=AMBER, width=10, joint="curve")
    for (x, y) in poly:
        d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=AMBER)

# 蜂巢入口蓝点
d.ellipse([128 - 12, 96 - 12, 128 + 12, 96 + 12], fill=(0x0E, 0xA5, 0xE9, 255))

img.save(PNG_PATH)

# ---- 自检：四角全透明、贴片内不得残留近白不透明像素（白边即此物）----
px = img.load()
corners_ok = all(px[x, y][3] == 0 for x, y in [(2, 2), (253, 2), (2, 253), (253, 253)])
near_white = sum(
    1
    for y in range(0, S, 2)
    for x in range(0, S, 2)
    if px[x, y][3] > 200 and min(px[x, y][:3]) > 230
)
print("corners_ok =", corners_ok, "| near_white =", near_white)
if not corners_ok or near_white:
    raise SystemExit("icon self-check FAILED: white edge or opaque corners")

# ---- 打包 256px PNG 压缩 ICO（ICONDIR + 单条 ICONDIRENTRY + PNG 载荷）----
png_bytes = PNG_PATH.read_bytes()
ico = struct.pack("<HHH", 0, 1, 1) + struct.pack(
    "<BBBBHHII", 0, 0, 0, 0, 1, 32, len(png_bytes), 22
)
ICO_PATH.write_bytes(ico + png_bytes)
print("icon.ico bytes =", 22 + len(png_bytes))
