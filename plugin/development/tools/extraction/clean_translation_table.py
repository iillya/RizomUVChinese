#!/usr/bin/env python3
import csv
import re
from collections import Counter
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parents[4]
ROOT = WORKSPACE / "dependencies" / "reference" / "core_executable_extraction"
SOURCE = ROOT / "03_UI候选_去重翻译表.csv"
KEEP = ROOT / "06_UI汉化表_已清理.csv"
DROP = ROOT / "07_已排除词条_含原因.csv"

UNITS = {
    "%", "px", "dpi", "ppi", "bit", "bits", "byte", "bytes", "kb", "mb", "gb", "tb",
    "b", "kib", "mib", "gib", "hz", "khz", "mhz", "ghz", "ms", "s", "sec", "secs",
    "min", "mins", "h", "hr", "hrs", "day", "days", "mm", "cm", "dm", "m", "km",
    "in", "inch", "inches", "ft", "yd", "deg", "rad", "rpm", "fps", "bpp", "kbps",
    "mbps", "gbps", "w", "kw", "v", "kv", "a", "ma", "db", "c", "f", "k",
    "x", "y", "z", "u", "uv", "uvw", "rgb", "rgba", "hsv", "hsl", "xyz",
    "pixel", "pixels", "degree", "degrees", "radian", "radians", "millisecond",
    "milliseconds", "second", "seconds", "minute", "minutes", "hour", "hours",
    "millimeter", "millimeters", "centimeter", "centimeters", "meter", "meters",
    "kilometer", "kilometers", "percent", "percentage", "byte", "bytes", "kilobyte",
    "kilobytes", "megabyte", "megabytes", "gigabyte", "gigabytes", "terabyte", "terabytes",
    "hertz", "kilohertz", "megahertz", "gigahertz", "bit", "bits", "inch", "inches",
    "foot", "feet", "yard", "yards",
}
EXTENSIONS = {
    "obj", "fbx", "dae", "abc", "usd", "usda", "usdc", "gltf", "glb", "stl", "ply",
    "ma", "mb", "3ds", "dxf", "zom", "lua", "py", "json", "xml", "txt", "log", "ini",
    "png", "jpg", "jpeg", "bmp", "tif", "tiff", "tga", "exr", "hdr", "psd", "svg",
    "dll", "exe", "pyd", "zip", "lic",
}
FULL_UNIT_WORDS = {
    "pixel", "pixels", "degree", "degrees", "radian", "radians", "millisecond",
    "milliseconds", "second", "seconds", "minute", "minutes", "hour", "hours",
    "day", "days", "millimeter", "millimeters", "centimeter", "centimeters",
    "meter", "meters", "kilometer", "kilometers", "percent", "percentage",
    "byte", "bytes", "kilobyte", "kilobytes", "megabyte", "megabytes", "gigabyte",
    "gigabytes", "terabyte", "terabytes", "hertz", "kilohertz", "megahertz",
    "gigahertz", "bit", "bits", "inch", "inches", "foot", "feet", "yard", "yards",
}


def exclusion_reason(value: str):
    s = value.strip()
    low = s.lower()
    if low in FULL_UNIT_WORDS:
        return None
    if not s:
        return "空白"
    if re.fullmatch(r"[+-]?(?:\d+(?:[.,]\d+)?|[.,]\d+)(?:[eE][+-]?\d+)?", s):
        return "纯数字"
    if re.fullmatch(r"(?:0x)?[0-9A-Fa-f]{6,}", s):
        return "十六进制或哈希值"
    if re.fullmatch(r"[^\w\u4e00-\u9fff]+", s, re.UNICODE):
        return "纯符号"
    if re.fullmatch(r"[A-Za-z]", s):
        return "单个字母"
    if low in UNITS:
        return "单位或坐标缩写"
    if re.fullmatch(r"[+-]?(?:\d+(?:[.,]\d+)?)\s*(?:" + "|".join(map(re.escape, sorted(UNITS, key=len, reverse=True))) + r")", low):
        return "数值加单位"
    if re.fullmatch(r"\.?[A-Za-z0-9]{1,5}", s) and low.lstrip(".") in EXTENSIONS:
        return "文件扩展名"
    if re.fullmatch(r"(?:%[-+ #0']*\d*(?:\.\d+)?[a-zA-Z]|\{\d+(?::[^}]*)?\}|\\[nrt0])", s):
        return "格式占位符或转义符"
    if re.fullmatch(r"[A-Z][A-Z0-9_]{1,11}", s):
        return "纯大写缩写或常量"
    if re.fullmatch(r"[A-Za-z]:[\\/].*|(?:[.]{0,2}[\\/]).*", s):
        return "文件路径"
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+", s):
        return "程序符号名"
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*\([^)]*\)", s) and " " not in s:
        return "函数表达式"
    return None


def main():
    kept, dropped = [], []
    with SOURCE.open(encoding="utf-8-sig", newline="") as f:
        fields = list(csv.DictReader(f).fieldnames or [])
        f.seek(0)
        for row in csv.DictReader(f):
            reason = exclusion_reason(row["source_text"])
            if reason:
                row["exclusion_reason"] = reason
                dropped.append(row)
            else:
                kept.append(row)
    for i, row in enumerate(kept, 1): row["id"] = i
    with KEEP.open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields); w.writeheader(); w.writerows(kept)
    drop_fields = fields + ["exclusion_reason"]
    with DROP.open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=drop_fields); w.writeheader(); w.writerows(dropped)
    reasons = Counter(r["exclusion_reason"] for r in dropped)
    print(f"原始去重候选: {len(kept) + len(dropped)}")
    print(f"保留待翻译: {len(kept)}")
    print(f"排除: {len(dropped)}")
    for reason, count in reasons.most_common(): print(f"  {reason}: {count}")


if __name__ == "__main__":
    main()
