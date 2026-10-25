"""Resume only a verified, saved native preparation checkpoint."""
from pathlib import Path
from .project import file_hash

def resume_native_args(args,resume,job,request):
    project=Path(job['project']).resolve()
    stage,page=resume.get('stage'),resume.get('page')
    if (request.get('mode')!='prepare' or resume.get('job_id')!=job['id'] or
        type(stage) is not int or not 0<=stage<=4 or type(page) is not int or page<0 or
        Path(resume.get('project','')).resolve()!=project or not project.is_file() or
        file_hash(project)!=resume.get('sha256')):
        raise ValueError('Devam kaydı doğrulanamadı; kayıtlı proje değiştirilmedi.')
    args=list(map(str,args))
    if (not any(a.startswith('--output-project=') and Path(a.split('=',1)[1]).resolve()==project for a in args) or
        not any(not a.startswith('--') and Path(a).resolve()==project for a in args)):
        raise ValueError('Devam kaydı bu motor çağrısıyla eşleşmiyor.')
    args=[a for a in args if not a.startswith('--start-filter=')]
    return ['--start-filter='+str(stage+1),*args],page
