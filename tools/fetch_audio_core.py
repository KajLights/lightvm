#!/usr/bin/env python3
"""Materialize the pinned audio source snapshot without network or credentials."""
from audio_dependency import ROOT, materialize

if __name__ == '__main__':
    print(materialize(ROOT/'.deps/kaj-fi'))
