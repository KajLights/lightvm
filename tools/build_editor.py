#!/usr/bin/env python3
"""Package the editor and canonical compiler as a directory of static files."""
import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def build_editor(destination=None):
    destination = Path(destination) if destination else ROOT / "build" / "web"
    destination.mkdir(parents=True, exist_ok=True)
    for source in (ROOT / "web").iterdir():
        if source.is_file() and source.suffix in {".html", ".css", ".mjs"}:
            shutil.copy2(source, destination / source.name)
    package = destination / "python" / "lightvm"
    package.mkdir(parents=True, exist_ok=True)
    for name in ("__init__.py", "compiler.py", "easing.py"):
        shutil.copy2(ROOT / "lightvm" / name, package / name)
    samples = destination / "samples"
    samples.mkdir(exist_ok=True)
    for source in (ROOT / "samples" / "python").glob("*.py"):
        shutil.copy2(source, samples / source.name)
    return destination.resolve()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    print(build_editor(args.output))
