"""Create an allowlisted release source tree; exclude books, secrets and builds."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,zipfile

def main():
    p=argparse.ArgumentParser();p.add_argument('--destination',required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[1];dest=Path(a.destination).resolve();dest.mkdir(parents=True,exist_ok=True)
    tree=dest/'scantailor-unified-ocr-ai-0.6.2-source';tree.mkdir(exist_ok=True)
    files=[]
    for name in ('README.md','README.tr.md','LICENSE','THIRD_PARTY_NOTICES.md','pyproject.toml','.gitignore'):
        files.append(root/name)
    for folder in ('src','packaging','native/scantailor-experimental','.github'):
        base=root/folder
        if base.exists():files.extend(x for x in base.rglob('*') if x.is_file() and not any(s in ('.git','__pycache__','.cache') or s.endswith('.egg-info') for s in x.relative_to(base).parts) and x.suffix not in ('.pyc','.pyo'))
    # Pure/synthetic regression tests. Local book integrations stay local.
    for name in ('test_adaptive_compression.py','test_pdf_tools.py','test_release_runtime.py','test_native_launcher.py','test_workflow_stages.py','test_workflow_storage.py','test_workflow_finalize.py','test_paddle_memory_profile.py','test_native_execution.py','test_memory_budget.py','test_mesh_geometry.py','test_ocr_cache.py','test_pdf_optimize.py','test_pdf_layout.py','test_queue_lock.py','test_revision_queue.py','test_worker_atomic_write.py','test_native_watchdog.py','test_geometry_timeout_fallback.py','test_workspace.py','test_quality_workflow.py'):
        files.append(root/'tests'/name)
    for name in ('native_fast_geometry.cpp','native_geometry_pilot.cpp','native_compression.cpp','native_review_state.cpp','native_stage_tools.cpp','native_i18n.cpp','native_workflow.h'):
        files.append(root/'tests'/name)
    for name in ('geometry_words_pilot.py','geometry_uvdoc_pilot.py'):
        files.append(root/'tests'/name)
    denied=re.compile(r'(?i)(C:[\\/]Users[\\/](?!Public)|OneDrive_'+'DUZENLENECEK'+r'|BEGIN (?:RSA |OPENSSH )?PRIVATE KEY|ghp_[A-Za-z0-9]{30,})')
    checksums={}
    for source in files:
        relative=source.relative_to(root)
        if source.suffix.lower() in ('.py','.cpp','.h','.md','.json','.yml','.yaml','.ps1','.sh','.txt','.toml'):
            if denied.search(source.read_text('utf8',errors='replace')):raise RuntimeError('Private path/credential marker in '+str(relative))
        payload=source.read_bytes()
        target=tree/relative;target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists() or target.read_bytes()!=payload:target.write_bytes(payload)
        checksums[relative.as_posix()]=hashlib.sha256(payload).hexdigest()
    (tree/'SOURCE-SHA256.json').write_text(json.dumps(checksums,indent=2),'utf8')
    archive=dest/'scantailor-unified-ocr-ai-0.6.2-source.zip'
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for relative in (*checksums, 'SOURCE-SHA256.json'):
            source=tree/relative
            z.write(source,source.relative_to(dest))
    print(json.dumps({'archive':str(archive),'files':len(checksums),'bytes':archive.stat().st_size}))

if __name__=='__main__':main()
