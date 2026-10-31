#!/usr/bin/env python3
"""Print a short Markdown summary of a JUnit XML file (for $GITHUB_STEP_SUMMARY)."""
import os
import sys
import xml.etree.ElementTree as ET


def main(path: str) -> None:
    if not os.path.exists(path):
        print(f"### Unit tests: no {path} (the build or the run failed earlier)")
        return

    cases = list(ET.parse(path).getroot().iter("testcase"))
    bad = [c for c in cases if any(ch.tag in ("failure", "error") for ch in c)]
    skipped = [c for c in cases if any(ch.tag == "skipped" for ch in c)]
    passed = len(cases) - len(bad) - len(skipped)
    print(f"### Unit tests (QEMU): {passed} passed, {len(bad)} failed, {len(skipped)} skipped")

    if bad:
        print()
        print("| Test | Message |")
        print("|---|---|")
        for c in bad:
            msg = ""
            for ch in c:
                if ch.tag in ("failure", "error"):
                    lines = (ch.attrib.get("message") or ch.text or "").strip().splitlines()
                    msg = lines[0] if lines else ""
            print(f"| {c.attrib.get('name')} | {msg.replace('|', '/')} |")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "test_apps/unit/results.xml")