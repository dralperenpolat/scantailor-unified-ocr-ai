"""Restore one exact Linux release artifact on a GitHub Actions runner."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import zipfile

import requests

API = 'https://api.github.com/repos/dralperenpolat/scantailor-unified-ocr-ai'
COMMIT = 'fc147ccc0284493b54841453edca74afcdec4901'
NAME = 'scantailor-ocr-unified-0.5.0-linux-x64-full.tar.gz'
SIZE = 893375374
DIGEST = 'sha256:fd128c18d5c2738bb49e4c1f0c20b6a86b69b50cbb45101e9efae5bc2e5d796a'
ARTIFACT_DIGEST = 'sha256:6f4830f7279da22d1fe8c1a1cd76225d0ccf2a9d4fd20ba6e60798fc8f7dbaec'
client = requests.Session()
client.headers.update({'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN'],
    'Accept': 'application/vnd.github+json', 'X-GitHub-Api-Version': '2022-11-28'})


def api(method, path, **kwargs):
    response = client.request(method, API + path, timeout=60, **kwargs)
    if not response.ok:
        raise RuntimeError(f'GitHub HTTP {response.status_code}')
    return response.json() if response.content else None


def digest(path):
    with path.open('rb') as stream:
        return 'sha256:' + hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    release = api('GET', '/releases/415705521')
    assert release['tag_name'] == 'v0.5.0' and release['target_commitish'] == COMMIT
    assert not release['draft']
    expected_names = {NAME, 'scantailor-ocr-unified-0.5.0-windows-x64-full-setup.exe'}
    assert {a['name'] for a in release['assets']} <= expected_names
    notes = json.loads(Path('.github/restore-linux-notes.json').read_text('utf8'))
    assert release['body'] in (notes['before'], notes['after'])
    current = next((a for a in release['assets'] if a['name'] == NAME), None)
    if current:
        assert current['size'] == SIZE and current['digest'] == DIGEST and current['state'] == 'uploaded'
    else:
        artifact = api('GET', '/actions/artifacts/12035505329')
        assert artifact['name'] == 'release-linux-0.5.0-full' and not artifact['expired']
        assert artifact['digest'] == ARTIFACT_DIGEST
        assert artifact['workflow_run']['head_sha'] == COMMIT
        assert artifact['workflow_run']['id'] == 38873925078
        work = Path(os.environ['RUNNER_TEMP']) / 'restore-linux'
        work.mkdir(exist_ok=True)
        archive = work / 'artifact.zip'
        with client.get(API + '/actions/artifacts/12035505329/zip', stream=True, timeout=(30, 300)) as response:
            assert response.status_code == 200
            with archive.open('wb') as stream:
                for chunk in response.iter_content(4 * 1024 * 1024): stream.write(chunk)
        assert archive.stat().st_size == artifact['size_in_bytes'] and digest(archive) == ARTIFACT_DIGEST
        package = work / NAME
        with zipfile.ZipFile(archive) as bundle:
            names = [n for n in bundle.namelist() if n.split('/')[-1] == NAME]
            assert len(names) == 1
            with bundle.open(names[0]) as source, package.open('wb') as target:
                shutil.copyfileobj(source, target)
        assert package.stat().st_size == SIZE and digest(package) == DIGEST
        print('Original Linux package verified.', flush=True)
        upload = release['upload_url'].split('{', 1)[0]
        assert upload.startswith('https://uploads.github.com/repos/dralperenpolat/scantailor-unified-ocr-ai/releases/')
        with package.open('rb') as stream:
            response = client.post(upload, params={'name': NAME}, data=stream,
                headers={'Content-Type': 'application/gzip', 'Content-Length': str(SIZE)}, timeout=(30, 900))
        assert response.status_code == 201, f'Upload HTTP {response.status_code}'
        current = response.json()
        assert current['size'] == SIZE and current['digest'] == DIGEST
        print('Linux upload verified.', flush=True)
    release = api('GET', '/releases/415705521')
    assert release['body'] in (notes['before'], notes['after'])
    assert {a['name'] for a in release['assets']} == expected_names
    api('PATCH', '/releases/415705521', json={'body': notes['after']})
    print('Windows and Linux release restored; exactly two uploaded packages.', flush=True)


if __name__ == '__main__':
    main()
