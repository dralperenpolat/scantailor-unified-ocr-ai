"""Promote exact, successful CI artifacts to a draft release without rebuilding."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
from urllib.parse import quote
import zipfile

import requests

REPO = 'dralperenpolat/scantailor-unified-ocr-ai'
API = 'https://api.github.com/repos/' + REPO
COMMIT = 'fc147ccc0284493b54841453edca74afcdec4901'
ARTIFACTS = (
    ('windows', 'minimal', 38873814859, 12035090939, 'ed631dc90de4496fdaf3b42c7deb19909e17aa64cdc0158f4130cf9e95ec8abe'),
    ('windows', 'full', 38873814859, 12036190063, 'b6bc662aa4f8d50d1365f1f80170d1a8f656ec7919b44095ccd29d190bb57311'),
    ('linux', 'minimal', 38873925078, 12034868403, '1e166cb3e2a4aac3e4712d40ca9ebe729b907fd4ba1e83dc78e528eba0dc3ba7'),
    ('linux', 'full', 38873925078, 12035505329, '6f4830f7279da22d1fe8c1a1cd76225d0ccf2a9d4fd20ba6e60798fc8f7dbaec'),
)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    source, work = args.source.resolve(), args.work.resolve()
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip() == COMMIT
    stage = work / 'assets'
    stage.mkdir(parents=True, exist_ok=True)
    session = requests.Session()
    session.headers.update({'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN'],
        'Accept': 'application/vnd.github+json', 'User-Agent': 'ScanTailor-release050',
        'X-GitHub-Api-Version': '2022-11-28'})

    def api(method, path, **kwargs):
        response = session.request(method, API + path, timeout=60, **kwargs)
        if not response.ok:
            raise RuntimeError(f'GitHub {response.status_code}: {response.json().get("message", "request failed")}')
        return response.json() if response.content else None

    runs = {}
    for run_id in {item[2] for item in ARTIFACTS} | {38873819262}:
        run = api('GET', f'/actions/runs/{run_id}')
        assert run['head_sha'] == COMMIT and run['conclusion'] == 'success'
        jobs = api('GET', f'/actions/runs/{run_id}/jobs')['jobs']
        assert jobs and all(job['conclusion'] == 'success' for job in jobs)
        runs[str(run_id)] = {'url': run['html_url'], 'source_commit': COMMIT,
            'result': 'success', 'jobs': [{'name': j['name'], 'result': j['conclusion']} for j in jobs]}

    acceptance = {'version': '0.5.0', 'source_commit': COMMIT, 'ci': runs, 'packages': {},
        'scope': 'Synthetic package acceptance; real books require visual and OCR review.'}
    for platform, variant, run_id, artifact_id, expected in ARTIFACTS:
        label = f'{platform}-{variant}'
        metadata = api('GET', f'/actions/artifacts/{artifact_id}')
        assert metadata['name'] == f'release-{platform}-0.5.0-{variant}'
        assert metadata['workflow_run']['head_sha'] == COMMIT
        assert metadata['workflow_run']['id'] == run_id
        assert metadata['digest'] == 'sha256:' + expected and not metadata['expired']
        archive = work / (label + '.zip')
        if not archive.exists():
            response = session.get(API + f'/actions/artifacts/{artifact_id}/zip', stream=True, timeout=(30, 300))
            if response.status_code != 200:
                raise RuntimeError(f'Artifact download failed: {label}, HTTP {response.status_code}')
            with archive.open('wb') as stream:
                for chunk in response.iter_content(1024 * 1024):
                    stream.write(chunk)
        assert archive.stat().st_size == metadata['size_in_bytes'] and digest(archive) == expected
        folder = work / label
        folder.mkdir(exist_ok=True)
        with zipfile.ZipFile(archive) as bundle:
            for name in bundle.namelist():
                assert not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts
            bundle.extractall(folder)
        install = json.loads((folder / 'install-result.json').read_text())
        assert install['install'] and install['uninstall'] and install['user_project_preserved']
        assert install['upgrade_from'] == '0.4.5' and install['manifest_files_verified'] > 0
        if platform == 'windows':
            package_name = f'scantailor-ocr-unified-0.5.0-windows-x64-{variant}-setup.exe'
            result = json.loads((folder / 'cpu-acceptance.json').read_text())
            assert result['pages'] == 2 and result['source_unchanged']
            assert len(result['ocr_chars']) == 2 and min(result['ocr_chars']) > 0
            content_type = 'application/vnd.microsoft.portable-executable'
        else:
            package_name = f'scantailor-ocr-unified-0.5.0-linux-x64-{variant}.tar.gz'
            result = json.loads((folder / 'linux-acceptance-0.5.0.json').read_text())
            assert result['source_commit'] == COMMIT and result['native_ui'] == 'passed'
            assert result['pdf_import']['result'] == 'passed' and result['cpu']['source_unchanged']
            assert min(result['cpu']['ocr_chars']) > 0 and result['manifest_files_verified'] > 0
            content_type = 'application/gzip'
        package = folder / package_name
        shutil.copy2(package, stage / package_name)
        sbom_name = f'sbom-0.5.0-{label}.cdx.json'
        sbom = json.loads((folder / 'sbom.cdx.json').read_text())
        assert sbom['bomFormat'] == 'CycloneDX' and sbom['components']
        shutil.copy2(folder / 'sbom.cdx.json', stage / sbom_name)
        acceptance['packages'][label] = {'artifact_id': artifact_id, 'artifact_sha256': expected,
            'package': package_name, 'sha256': digest(package), 'bytes': package.stat().st_size,
            'content_type': content_type, 'acceptance': result, 'install': install, 'sbom': sbom_name}
        print('Verified ' + label, flush=True)

    # Export every tracked source/test file, including the new workflow test headers.
    # Replace the historical source manifest with checksums for this exact commit.
    tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=source).decode().split('\0')
    tracked = sorted(name for name in tracked if name and name != 'SOURCE-SHA256.json')
    manifest = {}
    source_zip = stage / 'scantailor-unified-ocr-ai-0.5.0-source.zip'
    prefix = 'scantailor-unified-ocr-ai-0.5.0-source/'
    with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for name in tracked:
            payload = (source / name).read_bytes()
            manifest[name] = hashlib.sha256(payload).hexdigest()
            item = zipfile.ZipInfo(prefix + name, date_time=(2026, 10, 16, 8, 27, 24))
            item.compress_type = zipfile.ZIP_DEFLATED
            item.external_attr = (0o100755 if os.access(source / name, os.X_OK) else 0o100644) << 16
            bundle.writestr(item, payload)
        item = zipfile.ZipInfo(prefix + 'SOURCE-SHA256.json', date_time=(2026, 10, 16, 8, 27, 24))
        item.compress_type = zipfile.ZIP_DEFLATED
        item.external_attr = 0o100644 << 16
        bundle.writestr(item, json.dumps(manifest, indent=2) + '\n')
    with zipfile.ZipFile(source_zip) as bundle:
        assert len(bundle.namelist()) == len(manifest) + 1
        for name, expected in manifest.items():
            assert hashlib.sha256(bundle.read(prefix + name)).hexdigest() == expected
    acceptance['source_archive'] = {'files_verified': len(manifest), 'source_commit': COMMIT}
    (stage / 'acceptance-results-0.5.0.json').write_text(json.dumps(acceptance, indent=2) + '\n')
    assets = sorted(stage.iterdir())
    (stage / 'SHA256SUMS').write_text(''.join(f'{digest(path)}  {path.name}\n' for path in assets))
    assets = sorted(stage.iterdir())

    releases = api('GET', '/releases')
    release = next((item for item in releases if item['tag_name'] == 'v0.5.0'), None)
    if release is None:
        release = api('POST', '/releases', json={'tag_name': 'v0.5.0', 'target_commitish': COMMIT,
            'name': 'ScanTailor OCR Unified 0.5.0', 'draft': True, 'prerelease': False,
            'body': Path(__file__).with_name('RELEASE-NOTES-0.5.0.md').read_text('utf8')})
    assert release['draft'] and release['target_commitish'] == COMMIT
    upload_url = release['upload_url'].split('{', 1)[0]
    assert upload_url.startswith('https://uploads.github.com/repos/' + REPO + '/releases/')
    existing = {item['name']: item for item in api('GET', f'/releases/{release["id"]}/assets')}
    verification = {'release_id': release['id'], 'source_commit': COMMIT, 'draft': True, 'assets': []}
    for path in assets:
        expected = 'sha256:' + digest(path)
        asset = existing.get(path.name)
        if asset is None:
            with path.open('rb') as stream:
                response = session.post(upload_url + '?name=' + quote(path.name), data=stream,
                    headers={'Content-Type': 'application/octet-stream', 'Content-Length': str(path.stat().st_size)},
                    timeout=(30, 900))
            if response.status_code != 201:
                raise RuntimeError(f'Asset upload failed: {path.name}, HTTP {response.status_code}')
            asset = response.json()
        assert asset['size'] == path.stat().st_size and asset['digest'] == expected
        verification['assets'].append({'name': path.name, 'bytes': asset['size'], 'digest': expected})
        print('Uploaded and verified ' + path.name, flush=True)
    (work / 'publication-verification.json').write_text(json.dumps(verification, indent=2) + '\n')
    print(json.dumps({'release_id': release['id'], 'draft': True, 'verified_assets': len(assets)}), flush=True)


if __name__ == '__main__':
    main()
