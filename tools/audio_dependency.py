"""Verify/materialize the immutable kaj-fi source snapshot pinned by LightVM."""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SNAPSHOT = ROOT/'third_party/kaj-audio'


def manifest():
    data = json.loads((SNAPSHOT/'manifest.json').read_text())
    if data['revision'] != (ROOT/'kaj-audio.rev').read_text().strip():
        raise ValueError('Audio snapshot differs from kaj-audio.rev; regenerate the snapshot')
    for name in data['files']:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or str(path) != name:
            raise ValueError('Invalid audio snapshot path')
    return data


def verify(core):
    """Check bytes, including fixtures, FFT adapter and build script, not HEAD.

    A full kaj-fi checkout may have unrelated later commits; every consumed input
    must still equal the pinned snapshot. This is also valid without Git in CI.
    """
    data = manifest()
    for name, expected in data['files'].items():
        path = Path(core).parent/name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f'Audio dependency differs from {data["revision"]}: {name}')
    return data['revision']


def materialize(destination):
    destination = Path(destination)
    if destination.exists():
        verify(destination/'core')  # Never overwrite a modified dependency.
        return destination/'core'
    data = manifest()
    archive = (SNAPSHOT/'source.tar.gz').read_bytes()
    if hashlib.sha256(archive).hexdigest() != data['archive_sha256']:
        raise ValueError('Audio source archive checksum mismatch')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.kaj-audio-', dir=destination.parent) as directory:
        staging = Path(directory)/'source'
        staging.mkdir()
        with tarfile.open(fileobj=io.BytesIO(archive), mode='r:gz') as source:
            members = source.getmembers()
            if len(members) != len(data['files']) or {item.name for item in members} != set(data['files']):
                raise ValueError('Audio archive inventory differs from manifest')
            for item in members:
                if not item.isfile():
                    raise ValueError('Audio snapshot permits only ordinary source files')
                path = staging/item.name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(source.extractfile(item).read())
        verify(staging/'core')
        staging.rename(destination)
    return destination/'core'


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', type=Path, required=True)
    print(verify(parser.parse_args().verify))
