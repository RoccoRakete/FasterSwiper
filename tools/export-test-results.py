#!/usr/bin/env python3
"""Preserve Bazel test reports before the app build changes output symlinks."""
import json
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parent.parent
source = root / "bazel-testlogs"
reports = sorted(source.glob("**/test.xml"))
if not reports:
    raise SystemExit("No Bazel test XML reports found")

target = root / "artifacts/test-results"
summary = []
for report in reports:
    # These are trusted local GoogleTest outputs, not externally supplied XML.
    xml = ET.parse(report).getroot()
    cases = xml.findall(".//testcase")
    failures = xml.findall(".//failure") + xml.findall(".//error")
    if failures or xml.findall(".//skipped") or not cases:
        raise SystemExit(f"Failing or empty test report: {report}")
    summary.append({"target": str(report.parent.relative_to(source)), "cases": len(cases)})

if target.exists():
    shutil.rmtree(target)
for report in reports:
    output = target / report.relative_to(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(report, output)
    log = report.with_name("test.log")
    if log.is_file():
        shutil.copyfile(log, output.with_name("test.log"))

result = {"targets": summary, "total_cases": sum(item["cases"] for item in summary)}
(target / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
print(f"Preserved {len(reports)} passing test targets, {result['total_cases']} cases")
