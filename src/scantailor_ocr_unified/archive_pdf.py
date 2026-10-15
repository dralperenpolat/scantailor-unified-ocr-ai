"""PDF/A is delivered only after an independent veraPDF pass."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid
import xml.etree.ElementTree as ET
import fitz
from .pdf_profiles import publish
from .settings import atomic_json
from .storage import workspace


def executable(value,name):
    path=Path(value) if value else Path(shutil.which(name) or '')
    if not path.is_file():raise FileNotFoundError(f'{name} bulunamadı. Araç yolunu gelişmiş ayarlardan seçin.')
    return str(path.resolve())


def validation_report(text):
    root=ET.fromstring(text)
    reports=[e for e in root.iter() if e.tag.split('}')[-1]=='validationReport']
    return {'compliant':bool(reports) and all(e.get('isCompliant')=='true' for e in reports),
            'reports':len(reports),'profiles':[e.get('profileName','') for e in reports]}


def convert_pdfa(source,destination,conf=None):
    conf=conf or {};source,destination=Path(source).resolve(),Path(destination).resolve()
    if destination.exists():raise FileExistsError(destination)
    ocr=executable(conf.get('ocrmypdf'),'ocrmypdf');validator=executable(conf.get('verapdf'),'verapdf')
    destination.parent.mkdir(parents=True,exist_ok=True)
    staged=destination.with_name('.pdfa-'+uuid.uuid4().hex+'.pdf')
    try:
        with workspace(conf) as work:
            env={**os.environ,'TMP':str(work),'TEMP':str(work),'TMPDIR':str(work)}
            converted=subprocess.run([ocr,'--skip-text','--output-type','pdfa-2','--optimize','0','--jobs','1',str(source),str(staged)],
                env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf8',errors='replace',timeout=7200,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            if converted.returncode:raise RuntimeError('PDF/A dönüşümü başarısız: '+converted.stdout[-4000:])
            checked=subprocess.run([validator,'--format','xml',str(staged)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                text=True,encoding='utf8',errors='replace',timeout=7200,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            report=validation_report(checked.stdout)
            if checked.returncode or not report['compliant']:raise ValueError('veraPDF doğrulaması geçmedi; PDF/A çıktısı yayımlanmadı.')
            with fitz.open(source) as before,fitz.open(staged) as after:
                if len(before)!=len(after):raise ValueError('PDF/A sayfa sayısı değişti.')
                for a,b in zip(before,after):
                    if a.get_text().split()!=b.get_text().split():raise ValueError('PDF/A metni değişti.')
            publish(staged,destination)
            destination.with_suffix('.verapdf.xml').write_text(checked.stdout,encoding='utf8')
            report.update(pdf=str(destination),type='PDF/A-2',text_words_identical=True)
            atomic_json(destination.with_suffix('.verification.json'),report)
            return report
    finally:staged.unlink(missing_ok=True)
