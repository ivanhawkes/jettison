#!/usr/bin/env python3
"""Puts blank lines above every function and method declaration, its comment and template line
included: one in a header, two in a source file (docs/coding-standards.md). With --check it fails
naming the files that lack them.

clang-format cannot: SeparateDefinitionBlocks leaves declarations alone and collapses two blank
lines to one. So clang-query finds the declarations, in every tracked unit of the clang-debug
compile database and the project headers they include; a header no unit includes is not seen.

Usage: scripts/space-declarations.py [--check] [--build build/clang-debug]
"""

import argparse
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Kept in step with scripts/format-check.sh: generated, and checked byte for byte.
EXCLUDES = {"source/engine/script/tests/scripts/jettison_script.h"}
MATCHER = (
    'match functionDecl(isExpansionInFileMatching("{}/source/"), unless(hasAncestor(functionDecl())))'
)
BINDS = re.compile(r'^(/[^:]+):(\d+):\d+: note: "root" binds here$')
# The lines that belong to the declaration below them.
ATTACHED = re.compile(r"^\s*(//|template\s*<|\[\[.*\]\]\s*$)")
# After these the formatter removes blank lines, and a block's first member needs none.
OPENER = re.compile(r"(\{|^\s*(public|protected|private)\s*:)\s*$")


def tracked() -> set[str]:
    out = subprocess.run(
        ["git", "ls-files", "*.cpp", "*.h", "*.hpp"], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout
    return {path for path in out.splitlines() if path not in EXCLUDES}


def declarations(unit: str, build: Path) -> set[tuple[str, int]]:
    result = subprocess.run(
        [
            "clang-query", "-p", str(build),
            "-c", "set traversal IgnoreUnlessSpelledInSource",
            "-c", MATCHER.format(re.escape(str(ROOT))),
            unit,
        ],
        capture_output=True, text=True,
    )
    found = set()
    for line in result.stdout.splitlines():
        if match := BINDS.match(line):
            found.add((os.path.relpath(match[1], ROOT), int(match[2])))
    if not found and result.returncode != 0:
        sys.exit(f"space-declarations: clang-query failed on {unit}:\n{result.stderr[-2000:]}")
    return found


def spaced(lines: list[str], starts: set[int], blank: int) -> list[str]:
    """The lines with `blank` blank lines above each declaration's block (0-based line indices)."""
    blocks = set()
    for start in starts:
        while start > 0 and ATTACHED.match(lines[start - 1]):
            start -= 1
        blocks.add(start)
    out: list[str] = []
    for index, line in enumerate(lines):
        if index in blocks:
            while out and not out[-1].strip():
                out.pop()
            previous = out[-1] if out else ""
            if out and not OPENER.search(previous) and not previous.endswith("\\"):
                out += [""] * blank
        out.append(line)
    return out


def self_check() -> None:
    before = ["class A", "{", "  public:", "\tA();", "\t// Why.", "\tvoid F();", "", "", "", "\tint G();", "};"]
    after = ["class A", "{", "  public:", "\tA();", "", "", "\t// Why.", "\tvoid F();", "", "", "\tint G();", "};"]
    assert spaced(before, {3, 5, 9}, 2) == after, spaced(before, {3, 5, 9}, 2)
    header = ["class A", "{", "  public:", "\tA();", "", "\t// Why.", "\tvoid F();", "", "\tint G();", "};"]
    assert spaced(before, {3, 5, 9}, 1) == header, spaced(before, {3, 5, 9}, 1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--build", type=Path, default=ROOT / "build/clang-debug")
    arguments = parser.parse_args()
    self_check()

    files = tracked()
    database = json.loads((arguments.build / "compile_commands.json").read_text())
    units = sorted({os.path.relpath(entry["file"], ROOT) for entry in database} & files)
    found: dict[str, set[int]] = {}
    with ThreadPoolExecutor(os.cpu_count()) as pool:
        for result in pool.map(lambda unit: declarations(unit, arguments.build), units):
            for path, line in result:
                if path in files:
                    found.setdefault(path, set()).add(line - 1)

    changed = []
    for path, starts in sorted(found.items()):
        text = (ROOT / path).read_text()
        lines = text.split("\n")
        new = "\n".join(spaced(lines, starts, 1 if path.endswith((".h", ".hpp")) else 2))
        if new != text:
            changed.append(path)
            if not arguments.check:
                (ROOT / path).write_text(new)
    verb = "need" if arguments.check else "were given"
    print(f"space-declarations: {len(changed)} of {len(found)} files {verb} the blank lines")
    for path in changed if arguments.check else []:
        print(f"  {path}")
    return 1 if arguments.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
