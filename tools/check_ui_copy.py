from __future__ import annotations

import argparse
from pathlib import Path


BANNED = (
    "ai-powered",
    "magic",
    "supercharge",
    "unlock unparalleled",
    "next-generation experience",
)
TEXT_SUFFIXES = {".qml", ".cpp", ".hpp", ".md", ".txt"}


def check(root: Path) -> list[str]:
    findings: list[str] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in TEXT_SUFFIXES:
            continue
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            lowered = line.casefold()
            for phrase in BANNED:
                if phrase in lowered:
                    findings.append(f"{path}:{line_number}: banned phrase: {phrase}")
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description="Check MiniStream UI copy.")
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    findings = check(args.root)
    if findings:
        print("\n".join(findings))
        return 1
    print(f"UI copy check passed: {args.root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
