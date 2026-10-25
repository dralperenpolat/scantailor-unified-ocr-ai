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
    # Splitting changes the logical page sequence. An ordinal in the old
    # sequence cannot safely skip the same ordinal in the expanded sequence.
    return ['--start-filter='+str(stage+1),*args],0 if stage==1 else page


def saved_checkpoint(project):
    """Read the cursor atomically saved with the project, never UI progress."""
    import xml.etree.ElementTree as ET
    node=ET.parse(project).getroot().find('batch-checkpoint')
    if node is None:return None
    stage,page,total=(int(node.get(k,'-1')) for k in ('stage','page','total'))
    if not 0<=stage<=5 or not 0<=page<=total:raise ValueError('Geçersiz proje devam noktası.')
    return {'stage':stage,'page':page,'total':total,'token':node.get('token','')}
