#!/usr/bin/env python3
"""Runs clang-tidy over the project's own translation units, in parallel, skipping the ones that came
out clean before and have not changed since.

A translation unit is skipped when its key is in the cache. The key is a digest of everything that
decides what clang-tidy says about it: clang-tidy's version, .clang-tidy, the unit's compile command,
and the contents of every file the unit includes, as the build's dependency record lists them
(`ninja -t deps`). Only clean units are cached, so a diagnostic is reported on every run until it is
fixed. A unit with no dependency record, or with a file changed since its object was built (whose
includes the record may not list), is always checked and never cached.

Paths under the repository are made relative before they are hashed, and the cache lives outside
the tree (~/.cache/jettison/tidy), so a worktree of the same sources reuses another's results.
Paths under /nix/store are hashed by name alone: the store is immutable, so the name is the content.

Prints what tidy.sh printed: the diagnostics, then a summary. Exit 0 when clean, 1 with
diagnostics, and clang-tidy's own status when it fails without any.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OWN = re.compile(r"source/(engine|tools|samples|test)/.*\.(cpp|h)$")
DIAGNOSTIC = re.compile(r"^[^ ].*: (warning|error): .*\[([a-z0-9.,-]+)\]$")


def cache_directory():
    override = os.environ.get("JETTISON_TIDY_CACHE")
    if override:
        return Path(override)
    base = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    return Path(base) / "jettison" / "tidy"


def relative(text):
    return text.replace(str(ROOT) + "/", "<root>/")


def dependencies(build_dir):
    """Object path to the files it was built from, for every record ninja holds as valid."""
    output = subprocess.run(
        ["ninja", "-C", str(build_dir), "-t", "deps"], capture_output=True, text=True, check=True
    ).stdout
    records = {}
    current = None
    for line in output.splitlines():
        if not line.strip():
            current = None
            continue
        if not line.startswith(" "):
            target, _, status = line.partition(": #deps")
            current = [] if status.rstrip().endswith("(VALID)") else None
            if current is not None:
                records[str((build_dir / target).resolve())] = current
        elif current is not None:
            current.append(line.strip())
    return records


class Digests:
    """Each file's name, made relative to the repository, and a digest of its contents, worked out
    once however many units include it."""

    def __init__(self, build_dir):
        self.build_dir = build_dir
        self.known = {}
        self.times = {}

    def modified(self, name):
        """When the file last changed, or nothing when it is gone."""
        if name not in self.times:
            path = Path(name) if os.path.isabs(name) else (self.build_dir / name)
            try:
                self.times[name] = path.stat().st_mtime_ns
            except OSError:
                self.times[name] = None
        return self.times[name]

    def of(self, name):
        if name not in self.known:
            path = Path(name) if os.path.isabs(name) else (self.build_dir / name)
            resolved = str(path.resolve())
            if resolved.startswith("/nix/store/"):
                digest = resolved
            else:
                try:
                    digest = hashlib.sha256(path.read_bytes()).hexdigest()
                except OSError:
                    digest = "missing"
            self.known[name] = relative(resolved) + "\0" + digest
        return self.known[name]


def unit_key(entry, deps, digests, preamble):
    digest = hashlib.sha256(preamble)
    command = entry.get("arguments") or entry["command"]
    digest.update(relative(json.dumps(command)).encode())
    digest.update(relative(entry["file"]).encode())
    for dep in deps:
        digest.update(digests.of(dep).encode())
    return digest.hexdigest()


def check(file, build_dir):
    result = subprocess.run(
        ["clang-tidy", "-p", str(build_dir), "--quiet", file], capture_output=True, text=True
    )
    return result.returncode, result.stdout + result.stderr


def main():
    parser = argparse.ArgumentParser(description="clang-tidy over the units that changed since they were clean")
    parser.add_argument("--build-dir", default="build/clang-debug")
    parser.add_argument("--summary", action="store_true")
    parser.add_argument("--all", action="store_true", help="check every unit, cached or not")
    parser.add_argument("--jobs", type=int, default=int(os.environ.get("JETTISON_TIDY_JOBS", os.cpu_count() or 2)))
    arguments = parser.parse_args()

    build_dir = (ROOT / arguments.build_dir).resolve()
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        print(f"tidy: {arguments.build_dir} has no compile_commands.json; run 'cmake --preset clang-debug'", file=sys.stderr)
        return 1

    entries = [entry for entry in json.loads(database.read_text()) if OWN.search(entry["file"])]
    version = subprocess.run(["clang-tidy", "--version"], capture_output=True, text=True, check=True).stdout
    preamble = (version + (ROOT / ".clang-tidy").read_text()).encode()
    records = dependencies(build_dir)
    digests = Digests(build_dir)
    cache = cache_directory()
    cache.mkdir(parents=True, exist_ok=True)

    pending = []
    skipped = 0
    for entry in entries:
        output = str(Path(entry["directory"], entry["output"]).resolve()) if "output" in entry else None
        deps = records.get(output) if output else None
        # The record lists what the unit included when it was last built. A file changed since then
        # may include one the record does not name, so such a unit is checked and not cached.
        if deps is not None:
            built = digests.modified(output)
            changed = [digests.modified(dep) for dep in deps]
            if built is None or any(time is None or time > built for time in changed):
                deps = None
        key = unit_key(entry, deps, digests, preamble) if deps is not None else None
        if key and not arguments.all and (cache / key).exists():
            skipped += 1
            continue
        pending.append((entry["file"], key))

    diagnostics = []
    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, arguments.jobs)) as pool:
        futures = {pool.submit(check, file, build_dir): (file, key) for file, key in pending}
        for future in concurrent.futures.as_completed(futures):
            file, key = futures[future]
            status, output = future.result()
            found = [line for line in output.splitlines() if DIAGNOSTIC.match(line)]
            if found:
                diagnostics.append((file, output, found))
            elif status != 0:
                failures.append((file, status, output))
            elif key:
                (cache / key).touch()

    checked = len(pending)
    if failures:
        for file, status, output in failures:
            print(output, file=sys.stderr)
            print(f"tidy: clang-tidy failed on {file} (exit {status})", file=sys.stderr)
        return failures[0][1]

    if not arguments.summary:
        for _, output, _ in sorted(diagnostics):
            print(output)

    # A header's diagnostic is reported by every unit that includes it: counted once.
    unique = sorted({line for _, _, found in diagnostics for line in found})
    scope = f"{checked} of {len(entries)} units checked, {skipped} unchanged since clean"
    if not unique:
        print(f"tidy: clean (.clang-tidy over {arguments.build_dir}; {scope})")
        return 0
    counts = {}
    for line in unique:
        for check_name in DIAGNOSTIC.match(line).group(2).split(","):
            counts[check_name] = counts.get(check_name, 0) + 1
    for check_name, count in sorted(counts.items(), key=lambda item: (-item[1], item[0])):
        print(f"tidy: {count:6d} [{check_name}]")
    print(f"tidy: {len(unique)} diagnostics; see above (checks in .clang-tidy; {scope})")
    return 1


if __name__ == "__main__":
    sys.exit(main())
