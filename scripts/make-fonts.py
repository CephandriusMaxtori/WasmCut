"""Instantiate static Space Grotesk weights from the Google Fonts variable font.

Dear ImGui uses stb_truetype, which ignores `gvar` deltas and therefore cannot
read a variable font's weight axis. This script pins the axis to concrete values
so the editor can ship real Regular/Bold faces.

Usage:
    python scripts/make-fonts.py <variable-font.ttf> <output-dir>
"""

import sys
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

WEIGHTS = {
    "SpaceGrotesk-Regular.ttf": (400, "Regular", False),
    "SpaceGrotesk-Medium.ttf": (500, "Medium", False),
    "SpaceGrotesk-Bold.ttf": (700, "Bold", True),
}

FAMILY = "Space Grotesk"


def rename(font: TTFont, subfamily: str) -> None:
    full_name = FAMILY if subfamily == "Regular" else f"{FAMILY} {subfamily}"
    values = {
        1: FAMILY,
        2: subfamily,
        3: f"{FAMILY} static instance",
        4: full_name,
        6: full_name.replace(" ", ""),
        16: FAMILY,
        17: subfamily,
    }
    for record in font["name"].names:
        value = values.get(record.nameID)
        if value is None:
            continue
        record.string = value.encode(record.getEncoding())


def apply_weight_metadata(font: TTFont, weight: int, bold: bool) -> None:
    font["OS/2"].usWeightClass = weight
    font["OS/2"].fsSelection = (font["OS/2"].fsSelection & ~0b1100001) | (0b0100000 if bold else 0b1000000)
    font["head"].macStyle = (font["head"].macStyle & ~0b11) | (1 if bold else 0)


def build(source: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    for filename, (weight, subfamily, bold) in WEIGHTS.items():
        font = TTFont(source)
        instancer.instantiateVariableFont(font, {"wght": weight}, inplace=True, updateFontNames=False)
        apply_weight_metadata(font, weight, bold)
        rename(font, subfamily)
        target = output_dir / filename
        font.save(target)
        font.close()
        print(f"{target} ({target.stat().st_size} bytes)")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    build(Path(sys.argv[1]), Path(sys.argv[2]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
