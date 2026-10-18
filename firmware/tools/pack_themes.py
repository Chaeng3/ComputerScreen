"""Convert original generated artwork to the display's 320x240 RGB565 wire format."""
from pathlib import Path
from PIL import Image
import struct

root = Path(__file__).resolve().parents[1]
for name in ("day", "dusk", "night"):
    image = Image.open(root / "assets" / "themes" / f"{name}.png").convert("RGB")
    image = image.resize((320, 240), Image.Resampling.LANCZOS)
    packed = bytearray()
    for r, g, b in image.getdata():
        packed.extend(struct.pack(">H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
    assert len(packed) == 320 * 240 * 2
    (root / "main" / f"theme_{name}.rgb565").write_bytes(packed)
    print(name, len(packed), "bytes")
