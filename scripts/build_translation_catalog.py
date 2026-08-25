#!/usr/bin/env python3
"""Build deterministic translation catalogs from text-sniffer JSONL files."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from pathlib import Path


SHORTCUT_RE = re.compile(
    r"^(?:(?:Ctrl|Shift|Alt)(?:-(?:Ctrl|Shift|Alt))*-[A-Za-z0-9]+|F\d{1,2}|"
    r"(?:D|T|M|L|R)?L?M?R?B|BACK|SPACE|TAB|ESC|DEL|ENTER|[A-Z0-9])$",
    re.IGNORECASE,
)
NUMBER_RE = re.compile(r"^[+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[%x×]\d+(?:\.\d+)?)?$", re.IGNORECASE)
IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_.:/\\-]{2,}$")


def normalize_source(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n").strip("\x00")


def classify(text: str) -> str:
    stripped = text.strip()
    if not stripped:
        return "ignored_empty"
    if SHORTCUT_RE.fullmatch(stripped) or NUMBER_RE.fullmatch(stripped):
        return "ignored_shortcuts_numbers"
    if "$$" in text:
        return "tooltips"
    if "\n" in text or len(text) > 90 or stripped.endswith((".", "!", "?", ":")):
        return "descriptions"
    if IDENTIFIER_RE.fullmatch(stripped) and any(mark in stripped for mark in ("/", "\\", "::")):
        return "review_internal"
    return "interface_labels"


def write_translation_file(path: Path, catalog_id: str, description: str, sources: list[str]) -> None:
    payload = {
        "$schema": "rizomuv-localizer-translation-v1",
        "id": catalog_id,
        "language": "zh-CN",
        "description": description,
        "translations": {source: "" for source in sources},
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("logs", nargs="+", type=Path, help="RizomUV_text_sniffer.jsonl files")
    parser.add_argument("--output", type=Path, required=True, help="catalog output directory")
    parser.add_argument("--existing", nargs="*", type=Path, default=[], help="existing translation JSON files")
    args = parser.parse_args()

    existing_translations: dict[str, str] = {}
    for dictionary_path in args.existing:
        payload = json.loads(dictionary_path.read_text(encoding="utf-8-sig"))
        for source, translated in payload.get("translations", {}).items():
            if isinstance(source, str) and isinstance(translated, str) and translated.strip():
                existing_translations[source] = translated

    occurrences: Counter[str] = Counter()
    apis: dict[str, set[str]] = defaultdict(set)
    malformed: list[dict[str, object]] = []

    for log_path in args.logs:
        with log_path.open("r", encoding="utf-8-sig") as stream:
            for line_number, line in enumerate(stream, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    record = json.loads(line)
                    text = normalize_source(str(record.get("text", "")))
                    raw_apis = record.get("apis", record.get("api", "unknown"))
                    if isinstance(raw_apis, list):
                        record_apis = {str(api) for api in raw_apis}
                    else:
                        record_apis = {str(raw_apis)}
                    count = max(1, int(record.get("count", 1)))
                except (json.JSONDecodeError, TypeError, ValueError) as error:
                    malformed.append({"file": str(log_path), "line": line_number, "error": str(error)})
                    continue
                if not text:
                    continue
                occurrences[text] += count
                apis[text].update(record_apis)

    grouped: dict[str, list[str]] = defaultdict(list)
    for source in occurrences:
        grouped["translated_existing" if source in existing_translations else classify(source)].append(source)
    for values in grouped.values():
        values.sort(key=lambda value: (value.casefold(), value))

    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    entries = [
        {
            "source": source,
            "category": "translated_existing" if source in existing_translations else classify(source),
            "apis": sorted(apis[source]),
            "occurrences": occurrences[source],
            "length": len(source),
            "translation": existing_translations.get(source, ""),
        }
        for source in sorted(occurrences, key=lambda value: (value.casefold(), value))
    ]
    index = {
        "$schema": "rizomuv-localizer-catalog-v1",
        "sourceLogs": [str(path) for path in args.logs],
        "summary": {
            "uniqueEntries": len(entries),
            "malformedLines": len(malformed),
            "categories": {name: len(values) for name, values in sorted(grouped.items())},
            "existingDictionaryEntries": len(existing_translations),
            "existingEntriesSeenInCapture": len(grouped["translated_existing"]),
        },
        "entries": entries,
        "malformed": malformed,
    }
    (output / "english_text_catalog.json").write_text(
        json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    write_translation_file(
        output / "pending_interface_labels_zh-CN.json",
        "pending-interface-labels",
        "待翻译的短界面标签、按钮和菜单文字",
        grouped["interface_labels"],
    )
    write_translation_file(
        output / "pending_tooltips_zh-CN.json",
        "pending-tooltips",
        "待翻译的复合工具提示原文",
        grouped["tooltips"],
    )
    write_translation_file(
        output / "pending_descriptions_zh-CN.json",
        "pending-descriptions",
        "待翻译的长说明和句子",
        grouped["descriptions"],
    )
    write_translation_file(
        output / "review_internal_zh-CN.json",
        "review-internal",
        "疑似路径或内部标识，翻译前必须人工确认",
        grouped["review_internal"],
    )
    write_translation_file(
        output / "ignored_shortcuts_numbers.json",
        "ignored-shortcuts-numbers",
        "默认不翻译的快捷键与数值",
        grouped["ignored_shortcuts_numbers"],
    )

    print(json.dumps(index["summary"], ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
