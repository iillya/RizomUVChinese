#!/usr/bin/env python3
"""Validate reviewed RizomUV translations and build one runtime dictionary."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path


PAIRS = (
    ("pending_interface_labels_zh-CN.json", "interface_labels_zh-CN.json"),
    ("pending_tooltips_zh-CN.json", "tooltips_zh-CN.json"),
    ("pending_descriptions_zh-CN.json", "descriptions_zh-CN.json"),
    ("review_internal_zh-CN.json", "internal_identifiers_zh-CN.json"),
)

TECHNICAL_TOKEN_RE = re.compile(
    r"^(?:[+-][UV]|\d+K|3D|_[Uu]_[Vv]|S\d+|UDIM|[XYZ][+-][XYZ][+-]|x\d+(?:\.\d+)?)$"
)


def read_translations(path: Path) -> dict[str, str]:
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    translations = payload.get("translations")
    if not isinstance(translations, dict):
        raise ValueError(f"{path}: translations 必须是对象")
    if not all(isinstance(key, str) and isinstance(value, str) for key, value in translations.items()):
        raise ValueError(f"{path}: translations 的键和值必须是字符串")
    return translations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--reviewed", type=Path, required=True)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--static-source-csv", type=Path)
    parser.add_argument("--allow-missing-reviewed", action="store_true")
    args = parser.parse_args()

    errors: list[str] = []
    merged = read_translations(args.base)
    preserved_sources = set(read_translations(args.catalog / "ignored_shortcuts_numbers.json"))
    reviewed_count = 0

    for catalog_name, reviewed_name in PAIRS:
        source = read_translations(args.catalog / catalog_name)
        reviewed_path = args.reviewed / reviewed_name
        if not reviewed_path.exists():
            if args.allow_missing_reviewed:
                print(f"暂停/未完成: {reviewed_name} ({len(source)} 条)")
                continue
            errors.append(f"缺少审核词库: {reviewed_path}")
            continue

        translated = read_translations(reviewed_path)
        missing = source.keys() - translated.keys()
        extra = translated.keys() - source.keys()
        empty = [key for key, value in translated.items() if not value.strip()]
        separators = [key for key in source.keys() & translated.keys() if key.count("$$") != translated[key].count("$$")]
        identity_allowed = catalog_name == "review_internal_zh-CN.json"
        unchanged = [] if identity_allowed else [
            key for key, value in translated.items()
            if key == value and key not in preserved_sources and not TECHNICAL_TOKEN_RE.fullmatch(key)
        ]

        for label, values in (
            ("缺失", missing), ("多余", extra), ("空译文", empty),
            ("$$ 分隔符不匹配", separators), ("未经说明的原文保留", unchanged),
        ):
            if values:
                errors.append(f"{reviewed_name}: {label} {len(values)} 条，例如 {sorted(values)[:3]}")

        for key, value in translated.items():
            previous = merged.get(key)
            if previous is not None and previous != value:
                errors.append(f"翻译冲突: {key!r} -> {previous!r} / {value!r}")
            merged[key] = value
        reviewed_count += len(translated)
        print(f"通过: {reviewed_name} ({len(translated)} 条)")

    static_reviewed_path = args.reviewed / "static_interface_labels_zh-CN.json"
    if args.static_source_csv and static_reviewed_path.exists():
        with args.static_source_csv.open("r", encoding="utf-8-sig", newline="") as stream:
            static_sources = {
                row.get("source_text", "") for row in csv.DictReader(stream)
                if row.get("source_text", "")
            }
        translated = read_translations(static_reviewed_path)
        extra = translated.keys() - static_sources
        empty = [key for key, value in translated.items() if not value.strip()]
        if extra:
            errors.append(
                f"{static_reviewed_path.name}: CSV 中不存在 {len(extra)} 条，例如 {sorted(extra)[:3]}"
            )
        if empty:
            errors.append(
                f"{static_reviewed_path.name}: 空译文 {len(empty)} 条，例如 {sorted(empty)[:3]}"
            )
        for key, value in translated.items():
            previous = merged.get(key)
            if previous is not None and previous != value:
                errors.append(f"翻译冲突: {key!r} -> {previous!r} / {value!r}")
            merged[key] = value
        reviewed_count += len(translated)
        print(f"通过: {static_reviewed_path.name} ({len(translated)} 条，来源 CSV 已核对)")

    if errors:
        print("\n校验失败:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(f"校验通过: 审核词条 {reviewed_count}，合并后词条 {len(merged)}")
    if args.output:
        payload = {
            "$schema": "rizomuv-localizer-translation-v1",
            "language": "zh-CN",
            "description": "RizomUV 2025.0.104 简体中文运行词库",
            "translations": dict(sorted(merged.items(), key=lambda item: (item[0].casefold(), item[0]))),
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"已生成: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
