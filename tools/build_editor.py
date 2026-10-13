#!/usr/bin/env python3
"""Build the React workspace and package its static assets."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def build_editor(destination=None):
    subprocess.run(["npm", "run", "build"], cwd=ROOT, check=True)
    built = ROOT / "build" / "web"
    destination = Path(destination).resolve() if destination else built.resolve()
    if destination != built.resolve():
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(built, destination)
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    print(build_editor(args.output))
