#!/usr/bin/env python3
"""Build a focused RizomUV UI corpus from the exhaustive extraction."""
import csv
import hashlib
import json
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parents[4]
SOURCE = WORKSPACE / "dependencies" / "reference" / "full_installation_extraction"
OUTPUT = WORKSPACE / "dependencies" / "reference" / "core_executable_extraction"
TARGETS = {"rizomuv.exe", "ZomScience.dll"}
FIELDS = ["id", "file", "tier", "source_kind", "offset_or_line", "encoding",
          "candidate", "classification", "source_text", "zh_cn", "translator_note"]


import pefile


def executable_ranges():
    install = Path(r"C:\Program Files\Rizom Lab\RizomUV 2025.0")
    result = {}
    for name in TARGETS:
        pe = pefile.PE(str(install / name), fast_load=True)
        result[name] = [
            (s.PointerToRawData, s.PointerToRawData + s.SizeOfRawData)
            for s in pe.sections if s.Characteristics & 0x20000000
        ]
        pe.close()
    return result


EXECUTABLE_RANGES = executable_ranges()


def is_noncode_candidate(row):
    if not row["source_kind"].startswith("binary_"):
        return True
    try:
        offset = int(row["offset_or_line"])
    except ValueError:
        return True
    return not any(start <= offset < end for start, end in EXECUTABLE_RANGES[row["file"]])


def filtered_csv(src: Path, dst: Path, unique=False, noncode_only=False):
    count = 0
    seen = set()
    with src.open(encoding="utf-8-sig", newline="") as fi, dst.open("w", encoding="utf-8-sig", newline="") as fo:
        reader = csv.DictReader(fi)
        writer = csv.DictWriter(fo, fieldnames=FIELDS)
        writer.writeheader()
        for row in reader:
            if row["file"] not in TARGETS:
                continue
            if noncode_only and not is_noncode_candidate(row):
                continue
            if unique:
                key = row["source_text"]
                if key in seen:
                    continue
                seen.add(key)
            count += 1
            row["id"] = count
            writer.writerow({k: row.get(k, "") for k in FIELDS})
    return count


def main():
    OUTPUT.mkdir(exist_ok=True)
    all_count = filtered_csv(SOURCE / "all_extracted_occurrences.csv", OUTPUT / "01_两文件未过滤全集.csv")
    candidate_count = filtered_csv(SOURCE / "ui_translation_candidates_all_occurrences.csv", OUTPUT / "02_UI候选_全部位置.csv", noncode_only=True)
    unique_count = filtered_csv(SOURCE / "ui_translation_candidates_all_occurrences.csv", OUTPUT / "03_UI候选_去重翻译表.csv", unique=True, noncode_only=True)

    # PE resources are especially high-confidence Windows UI/resource material.
    pe_count = 0
    with (OUTPUT / "02_UI候选_全部位置.csv").open(encoding="utf-8-sig", newline="") as fi, \
         (OUTPUT / "04_PE资源词条.csv").open("w", encoding="utf-8-sig", newline="") as fo:
        reader = csv.DictReader(fi); writer = csv.DictWriter(fo, fieldnames=FIELDS); writer.writeheader()
        for row in reader:
            if row["source_kind"].startswith("pe_"):
                pe_count += 1; row["id"] = pe_count; writer.writerow(row)

    install = Path(r"C:\Program Files\Rizom Lab\RizomUV 2025.0")
    files = []
    for name in sorted(TARGETS):
        p = install / name
        files.append({"file": name, "bytes": p.stat().st_size,
                      "sha256": hashlib.sha256(p.read_bytes()).hexdigest()})
    with (OUTPUT / "05_源文件校验.csv").open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["file", "bytes", "sha256"]); w.writeheader(); w.writerows(files)

    report = {"included_files": sorted(TARGETS), "raw_occurrences": all_count,
              "candidate_occurrences": candidate_count, "unique_candidates": unique_count,
              "pe_resource_occurrences": pe_count}
    (OUTPUT / "提取说明.txt").write_text(
        "RizomUV 精简核心提取\n\n"
        "仅包含 rizomuv.exe 与自有核心库 ZomScience.dll。\n"
        "03_UI候选_去重翻译表.csv：主要汉化工作表，填写 zh_cn 列。\n"
        "翻译表已经排除 PE 可执行代码段中的机器码假词。\n"
        "02_UI候选_全部位置.csv：查看同一词条的全部来源位置。\n"
        "04_PE资源词条.csv：Windows 资源区域中的高相关词条。\n"
        "01_两文件未过滤全集.csv：查漏底稿，含机器码误识别内容，不可整表翻译。\n"
        "请保留 %s、%d、%llu、{0}、\\n 等占位符和转义符。\n",
        encoding="utf-8-sig")
    (OUTPUT / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
