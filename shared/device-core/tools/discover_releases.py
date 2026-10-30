#!/usr/bin/env python3
"""Verify public published firmware releases for a website build (never a device)."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import quote

from release_contract import published_releases, read_json, validate, verify_assets


def api(endpoint):
    return json.loads(subprocess.check_output(['gh', 'api', endpoint]))


def resolve_tag(repository, tag):
    obj = api('repos/' + repository + '/git/ref/tags/' + quote(tag, safe=''))['object']
    for _ in range(8):
        if obj['type'] == 'commit':
            return obj['sha']
        if obj['type'] != 'tag':
            break
        obj = api('repos/' + repository + '/git/tags/' + obj['sha'])['object']
    raise ValueError('Tag does not resolve to a source commit')


def validate_published(manifest, release, repository, revision):
    validate(manifest, 'release')
    if release['draft'] or not release['published_at'] or manifest['channel'] == 'development':
        raise ValueError('Draft/unpublished/development build is not an installable release')
    if manifest['repository'] != repository or manifest['tag'] != release['tag_name'] or manifest['tag'] != 'v' + manifest['version']:
        raise ValueError('Release source/tag does not match its manifest')
    if manifest['product'] != {'KajLights/lightvm': 'lightvm', 'KajLights/kaj-fi': 'kaj-fi'}[repository]:
        raise ValueError('Product is not owned by the registered source')
    if manifest['source_revision'] != revision:
        raise ValueError('Source tag moved or resolves to a different commit')
    if manifest['distribution'] != 'public' or not manifest['provenance']['attestation_required']:
        raise ValueError('Public distribution requires signed build provenance')
    if bool(release['prerelease']) != (manifest['channel'] == 'prerelease'):
        raise ValueError('Prerelease channel disagreement')
    identities = set()
    for artifact in manifest['artifacts']:
        build = artifact['build']
        fw = build['firmware']
        if (build['product'], build['version'], fw['source_revision'], fw['release_tag'], fw['source_dirty']) != (
                manifest['product'], manifest['version'], revision, manifest['tag'], False):
            raise ValueError('Conflicting artifact identity or unreleased source')
        if artifact['id'] in identities:
            raise ValueError('Duplicate firmware target')
        identities.add(artifact['id'])
    return manifest


def discover(repository, include_prereleases=False, on_verified=None, release_ids=None):
    """Optionally hand verified files to a website-owned transactional mirror.

    The callback runs only after provenance and every asset have passed, while
    the temporary directory still exists. It must not mutate the signed record.
    A callback failure aborts acquisition just like a verification failure.
    """
    if api('repos/' + repository)['private']:
        raise ValueError('Private sources require a separately approved public distribution route')
    releases = published_releases(lambda page, count: api(f'repos/{repository}/releases?per_page={count}&page={page}'), include_prereleases)
    if release_ids is not None:
        wanted = set(release_ids)
        releases = [release for release in releases if release['id'] in wanted]
        if {release['id'] for release in releases} != wanted:
            raise ValueError('A locked release is missing or no longer published')
    verified = []
    for release in releases:
        tag = release['tag_name']
        with tempfile.TemporaryDirectory(prefix='kajlights-release-') as directory:
            work = Path(directory)
            def download(name, expected=None):
                assets = [a for a in release['assets'] if a['name'] == name and a.get('state') == 'uploaded']
                limit = 1048576 if name == 'release.json' else 16777216 if name == 'release.sigstore.json' else 536870912
                if len(assets) != 1 or assets[0]['size'] > limit or expected and assets[0]['size'] != expected['bytes']:
                    raise ValueError('Missing/duplicate/inconsistent release asset: ' + name)
                if expected and assets[0].get('digest') not in (None, 'sha256:' + expected['sha256']):
                    raise ValueError('GitHub asset digest disagrees with the signed manifest')
                subprocess.run(['gh', 'release', 'download', tag, '--repo', repository,
                                '--dir', str(work), '--pattern', name], check=True)
            download('release.json')
            manifest = validate_published(read_json(work / 'release.json'), release, repository, resolve_tag(repository, tag))
            download('release.sigstore.json')
            subprocess.run(['gh', 'attestation', 'verify', str(work / 'release.json'),
                '--bundle', str(work / 'release.sigstore.json'), '--repo', repository,
                '--signer-workflow', repository + '/.github/workflows/release.yml',
                '--source-ref', 'refs/tags/' + tag, '--source-digest', manifest['source_revision'],
                '--deny-self-hosted-runners'], check=True)
            names = set()
            for artifact in manifest['artifacts']:
                for asset in artifact['assets']:
                    if asset['name'] in names:
                        raise ValueError('Conflicting release assets')
                    names.add(asset['name'])
                    download(asset['name'], asset)
                verify_assets(artifact, work)
            record = {'release_id': release['id'], 'published_at': release['published_at'],
                      'url': release['html_url'], 'manifest': manifest}
            if on_verified is not None:
                on_verified(record, work)
            verified.append(record)
    return {'schema': 'kajlights.verified_releases', 'repository': repository, 'releases': verified}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True, choices=['KajLights/lightvm', 'KajLights/kaj-fi'])
    parser.add_argument('--include-prereleases', action='store_true')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = discover(args.repository, args.include_prereleases)
    # Replace the catalogue only after the entire acquisition succeeds.
    temporary = args.output.with_suffix('.tmp')
    temporary.write_text(json.dumps(result, indent=2) + '\n')
    temporary.replace(args.output)
