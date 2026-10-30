"""The application release and its same-major minimum-release contract.

Build metadata identifies development builds without changing compatibility.
BASELINE is the first supported milestone; all current features belong to 0.1.0.
Individual development changes do not advance either value.
"""
import json
from pathlib import Path
import re

_APPLICATION = json.loads(Path(__file__).with_name('release.json').read_text())
VERSION = _APPLICATION['version']
BASELINE = _APPLICATION['baseline']
_CORE = r'(0|[1-9][0-9]*)'
_RELEASE = re.compile(rf'{_CORE}(?:\.{_CORE}(?:\.{_CORE})?)?(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?\Z')


def parse_release(value, *, shorthand=True):
    match = _RELEASE.fullmatch(value) if isinstance(value, str) and len(value) <= 95 else None
    if not match:
        raise ValueError('invalid LightVM release: expected a major, major.minor or semantic version')
    major, minor, patch, pre, build = match.groups()
    if (not shorthand or pre or build) and patch is None:
        raise ValueError('prerelease/build versions require major.minor.patch')
    core = tuple(int(part or 0) for part in (major, minor, patch))
    if any(part > 0xffffffff for part in core) or any(part.isascii() and part.isdigit() and len(part) > 1 and part[0] == '0' for part in (pre or '').split('.')):
        raise ValueError('invalid LightVM release number')
    return core, tuple(pre.split('.')) if pre else (), build


def normalize_release(value):
    core, pre, build = parse_release(value)
    return '.'.join(map(str, core)) + ('-' + '.'.join(pre) if pre else '') + ('+' + build if build else '')


def _precedence(value):
    core, pre, _ = parse_release(value)
    # Stable releases sort after prereleases; numeric identifiers before words.
    return core, not pre, tuple((0, int(p)) if p.isdigit() else (1, p) for p in pre)


def release_error(version, required):
    try:
        current = parse_release(version, shorthand=False)[0]
        minimum = parse_release(required)[0]
    except ValueError as error:
        return str(error)
    if current[0] != minimum[0]:
        return f'program requires LightVM {required} within major {minimum[0]}; device runs {version}'
    if _precedence(version) < _precedence(required):
        return f'program requires LightVM {required} or later within major {minimum[0]}; device runs {version}'
    return None


parse_release(VERSION, shorthand=False)
parse_release(BASELINE, shorthand=False)
assert release_error(VERSION, BASELINE) is None, 'application version precedes its baseline'
