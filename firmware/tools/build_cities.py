"""Convert the official Seniverse workbook to offline mainland province/city choices.

Source: https://cdn.sencdn.com/download/data/thinkpage_cities.zip
Workbook snapshot: 2026-07-02. County-level rows beneath cities are omitted.
Usage: bundled-python tools/build_cities.py ../tmp/seniverse-cities.xlsx
"""
import json
import sys
from pathlib import Path
from openpyxl import load_workbook

rows = load_workbook(sys.argv[1], read_only=True, data_only=True)["国内城市"].values
next(rows)
groups = {}
seen = set()
province_aliases = {"新疆":"新疆维吾尔自治区", "黑龙江":"黑龙江省", "浙江":"浙江省",
    "山西":"山西省", "江西":"江西省", "广东":"广东省", "重庆":"重庆市"}
for identifier, hierarchy, short, pinyin in rows:
    if not hierarchy or not identifier:
        continue
    parts = hierarchy.split("/")
    province = parts[0]
    if province.startswith(("台湾", "香港", "澳门")) or len(parts) > 2:
        continue
    province = province_aliases.get(province, province)
    if province in {"北京市", "天津市", "上海市", "重庆市"} and len(parts) > 1:
        continue
    if identifier in seen:
        continue
    seen.add(identifier)
    name = short.split("/")[-1] if short else parts[-1]
    groups.setdefault(province, []).append({"id": identifier, "name": name,
        "aliases": sorted(set(filter(None, [pinyin, parts[-1], short, name])))})
result = [{"name": province, "cities": cities} for province, cities in groups.items()]
assert len(result) == 31
ids = [city["id"] for group in result for city in group["cities"]]
assert len(ids) == len(set(ids))
root = Path(__file__).resolve().parents[1] / "main"
(root / "cities.json").write_text(json.dumps(result, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
(root / "city_ids.h").write_text("// Generated from Seniverse official city list (2026-07-02).\n"
    "#pragma once\nstatic const char *const mainland_city_ids[] = {\n"
    + "\n".join('    "' + identifier + '",' for identifier in ids) + "\n};\n", encoding="utf-8")
print(f"Generated {len(result)} provinces and {len(ids)} cities")
