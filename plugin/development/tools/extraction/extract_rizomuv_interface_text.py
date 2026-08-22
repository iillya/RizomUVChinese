#!/usr/bin/env python3
"""Inventory a RizomUV installation and extract every plausible UI string.

The source tree is read-only. Results are UTF-8 with BOM where appropriate.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import shutil
import sys
from collections import Counter
from pathlib import Path


TEXT_EXTS = {
    ".txt", ".md", ".html", ".htm", ".json", ".py", ".cpp", ".h",
    ".usda", ".glslfx", ".rtf", ".gitignore", "._pth", ".xml", ".ini",
    ".cfg", ".conf", ".lua", ".js", ".css", ".csv", ".tsv",
}
BINARY_EXTS = {".exe", ".dll", ".pyd", ".dat", ".cat", ".zip", ".tex", ".obj"}
CORE_NAMES = {"rizomuv.exe", "zomscience.dll", "unins000.exe", "unins000.dat"}
MIN_LEN = 4

ASCII_RE = re.compile(rb"[\x20-\x7e]{4,}")
UTF16_RE = re.compile(rb"(?:[\x20-\x7e]\x00){4,}")
WORD_RE = re.compile(r"[A-Za-z]{2,}")
PATHISH_RE = re.compile(r"^(?:[A-Za-z]:\\|[/\\]|\.?\.?[/\\])|[/\\].*[/\\]")
SYMBOL_RE = re.compile(r"^[A-Za-z_?$@][A-Za-z0-9_?$@:.<>~`'\-+*/=\[\](),&|!]*$")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def tier(path: Path, root: Path) -> str:
    rel = path.relative_to(root)
    low = str(rel).lower()
    if path.name.lower() in CORE_NAMES:
        return "core"
    if low.startswith("rizomuvlink") or low.startswith("doc"):
        return "associated"
    if low.startswith("thirdparty") or low.startswith("usd") or path.name.lower().startswith(("python", "usd_", "omni")):
        return "third_party"
    return "runtime"


def decode_text(data: bytes) -> tuple[str, str]:
    for enc in ("utf-8-sig", "utf-16", "cp1252"):
        try:
            return data.decode(enc), enc
        except UnicodeDecodeError:
            pass
    return data.decode("utf-8", "replace"), "utf-8-replace"


def classify(s: str, source_kind: str, file_tier: str) -> tuple[bool, str]:
    t = s.strip()
    if len(t) < MIN_LEN or not WORD_RE.search(t):
        return False, "too_short_or_no_words"
    if "http://" in t or "https://" in t or "www." in t:
        return False, "url"
    if PATHISH_RE.search(t):
        return False, "path"
    if len(t) > 700:
        return False, "very_long"
    spaces = t.count(" ")
    punctuation = sum(not (c.isalnum() or c.isspace()) for c in t)
    if source_kind.startswith("pe_"):
        return True, "pe_resource"
    if source_kind == "text_line":
        if file_tier in {"core", "associated"} and spaces >= 1:
            return True, "readable_text"
        return False, "documentation_or_code"
    if source_kind.startswith("binary_"):
        # Machine code frequently contains accidental 4–15 byte printable runs.
        # Keep those in the exhaustive table, but require language-like structure
        # for the translation sheet.
        if file_tier != "core":
            return False, "non_core_binary"
        if len(t) > 400 or any(x in t for x in ("$ ", "UATA", "AVAWH", "UVWATA", "@@", "__")):
            return False, "machine_code_or_symbol"
        letters = sum(c.isalpha() for c in t)
        visible = sum(not c.isspace() for c in t)
        if not visible or letters / visible < 0.62 or not re.search(r"[a-z]", t):
            return False, "not_language_like"
        words = re.findall(r"[A-Za-z][A-Za-z'’-]*", t)
        if len(words) >= 2 and re.search(r"\s", t):
            natural = all(
                re.fullmatch(r"(?:[A-Z]?[a-z]+(?:['’-][A-Za-z]+)?|[A-Z]{2,5}|a|I)", w)
                for w in words
            )
            substantial = sum(len(w) >= 2 for w in words) >= 2
            if natural and substantial:
                return True, "language_like_core_binary"
            return False, "machine_code_or_symbol"
        if len(words) == 1 and 3 <= len(t) <= 28 and re.fullmatch(r"[A-Z][a-z]+(?:['’-][A-Za-z]+)?", t):
            return True, "single_ui_label"
        return False, "identifier_or_token"
    if SYMBOL_RE.fullmatch(t) and spaces == 0:
        return False, "identifier_or_symbol"
    if punctuation > len(t) * 0.35:
        return False, "symbol_heavy"
    if spaces >= 1 or re.search(r"[.!?:]$", t) or re.search(r"[a-z][A-Z]", t):
        return file_tier in {"core", "associated"}, "readable_binary_string"
    return False, "identifier_or_token"


def add(rows: list[dict], root: Path, path: Path, kind: str, offset: int | str, text: str, encoding: str = "") -> None:
    text = text.replace("\x00", "").strip()
    if not text:
        return
    ft = tier(path, root)
    candidate, reason = classify(text, kind, ft)
    rows.append({
        "file": str(path.relative_to(root)).replace("\\", "/"),
        "tier": ft,
        "source_kind": kind,
        "offset_or_line": offset,
        "encoding": encoding,
        "candidate": "yes" if candidate else "no",
        "classification": reason,
        "source_text": text,
        "zh_cn": "",
        "translator_note": "",
    })


def extract_binary(root: Path, path: Path, rows: list[dict]) -> None:
    data = path.read_bytes()
    for m in ASCII_RE.finditer(data):
        add(rows, root, path, "binary_ascii", m.start(), m.group().decode("ascii"), "ascii")
    for m in UTF16_RE.finditer(data):
        add(rows, root, path, "binary_utf16le", m.start(), m.group().decode("utf-16le"), "utf-16le")


def extract_text(root: Path, path: Path, rows: list[dict]) -> None:
    text, enc = decode_text(path.read_bytes())
    for no, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if line:
            add(rows, root, path, "text_line", no, line, enc)


def resource_name(entry) -> str:
    return str(entry.name) if entry.name is not None else str(entry.struct.Id)


def extract_pe_resources(root: Path, path: Path, rows: list[dict]) -> None:
    try:
        import pefile
        pe = pefile.PE(str(path), fast_load=False)
        if not hasattr(pe, "DIRECTORY_ENTRY_RESOURCE"):
            return
        for type_e in pe.DIRECTORY_ENTRY_RESOURCE.entries:
            typ = resource_name(type_e)
            for name_e in type_e.directory.entries:
                name = resource_name(name_e)
                for lang_e in name_e.directory.entries:
                    lang = resource_name(lang_e)
                    ds = lang_e.data.struct
                    blob = pe.get_data(ds.OffsetToData, ds.Size)
                    label = f"type={typ};name={name};lang={lang};rva=0x{ds.OffsetToData:X}"
                    if typ == "6":  # RT_STRING: 16 length-prefixed UTF-16 strings
                        pos = 0
                        index = 0
                        while pos + 2 <= len(blob) and index < 16:
                            n = int.from_bytes(blob[pos:pos + 2], "little")
                            pos += 2
                            raw = blob[pos:pos + n * 2]
                            pos += n * 2
                            if n:
                                add(rows, root, path, "pe_string_table", f"{label};slot={index}", raw.decode("utf-16le", "replace"), "utf-16le")
                            index += 1
                    else:
                        for m in UTF16_RE.finditer(blob):
                            add(rows, root, path, "pe_resource_utf16le", f"{label};+0x{m.start():X}", m.group().decode("utf-16le"), "utf-16le")
                        for m in ASCII_RE.finditer(blob):
                            add(rows, root, path, "pe_resource_ascii", f"{label};+0x{m.start():X}", m.group().decode("ascii"), "ascii")
        pe.close()
    except Exception as e:
        add(rows, root, path, "extractor_error", "", f"PE resource parse failed: {e}")


def write_csv(path: Path, rows: list[dict]) -> None:
    fields = ["id", "file", "tier", "source_kind", "offset_or_line", "encoding", "candidate", "classification", "source_text", "zh_cn", "translator_note"]
    with path.open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for i, row in enumerate(rows, 1):
            w.writerow({"id": i, **row})


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()
    root = args.source.resolve()
    out = args.output.resolve()
    if not root.is_dir():
        raise SystemExit(f"Source directory not found: {root}")
    if out == root or root in out.parents:
        raise SystemExit("Output must not be inside the installation directory")
    out.mkdir(parents=True, exist_ok=True)

    files = sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: str(p).lower())
    inventory = []
    rows: list[dict] = []
    for path in files:
        ext = path.suffix.lower()
        inventory.append({
            "file": str(path.relative_to(root)).replace("\\", "/"),
            "bytes": path.stat().st_size,
            "extension": ext,
            "tier": tier(path, root),
            "sha256": sha256(path),
            "scanned_as": "text" if ext in TEXT_EXTS else "binary" if ext in BINARY_EXTS else "inventory_only",
        })
        if ext in TEXT_EXTS:
            extract_text(root, path, rows)
        elif ext in BINARY_EXTS:
            extract_binary(root, path, rows)
        if ext in {".exe", ".dll", ".pyd"}:
            extract_pe_resources(root, path, rows)

    # Stable exact deduplication only for the translation view; exhaustive data stays intact.
    candidates = [r for r in rows if r["candidate"] == "yes"]
    seen = set()
    unique = []
    for r in candidates:
        key = r["source_text"]
        if key not in seen:
            seen.add(key)
            unique.append(r.copy())

    with (out / "file_inventory.csv").open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(inventory[0]))
        w.writeheader(); w.writerows(inventory)
    write_csv(out / "all_extracted_occurrences.csv", rows)
    write_csv(out / "ui_translation_candidates_all_occurrences.csv", candidates)
    write_csv(out / "ui_translation_unique.csv", unique)
    (out / "all_extracted_occurrences.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")

    counts = Counter(r["tier"] for r in rows)
    kinds = Counter(r["source_kind"] for r in rows)
    report = {
        "source": str(root), "output": str(out), "files": len(files),
        "bytes": sum(x["bytes"] for x in inventory), "occurrences": len(rows),
        "candidate_occurrences": len(candidates), "unique_candidates": len(unique),
        "occurrences_by_tier": dict(counts), "occurrences_by_source_kind": dict(kinds),
        "notes": [
            "all_extracted_occurrences files are the unfiltered safety net.",
            "ui_translation_unique.csv is deduplicated by exact source text and is intended for translation.",
            "Do not translate format placeholders, escape sequences, file extensions, or scripting/API identifiers.",
            "Inventory-only image/font/texture files are recorded but contain no directly extractable text.",
        ],
    }
    (out / "extraction_report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    (out / "README_中文.txt").write_text(
        "RizomUV 2025.0 UI 词条提取结果\n\n"
        "ui_translation_unique.csv：建议优先汉化，完全相同英文已去重。\n"
        "ui_translation_candidates_all_occurrences.csv：候选词条的全部出现位置。\n"
        "all_extracted_occurrences.csv/json：未过滤全集，用于查漏补缺。\n"
        "file_inventory.csv：安装目录全部文件、哈希、分层和扫描方式。\n"
        "extraction_report.json：数量统计。\n\n"
        "填写 CSV 的 zh_cn 列，不要修改 source_text、占位符（如 %s/%d/{0}）及转义字符。\n"
        "core=程序核心；associated=文档/联动脚本；runtime=运行时；third_party=第三方组件。\n",
        encoding="utf-8-sig",
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
