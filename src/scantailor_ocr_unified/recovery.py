"""Classified failures and bounded retries; resource pressure stops the queue."""
import errno
import time
from .storage import StoragePressure
from .memory_budget import is_memory_failure

def classify(error):
    text=str(error).lower()
    if (isinstance(error,StoragePressure) or getattr(error,'errno',None)==errno.ENOSPC
        or getattr(error,'winerror',None) in (39,112) or getattr(error,'sqlite_errorcode',None)==13
        or any(message in text for message in ('disk is full','no space left on device','not enough space on the disk'))):return 'disk'
    if is_memory_failure(error):return 'memory'
    if isinstance(error,FileNotFoundError):return 'temporary_missing' if 'stou_pdf_' in text else 'source_missing'
    if isinstance(error,PermissionError):return 'file_busy'
    if 'cuda' in text or 'gpu' in text:return 'device'
    if isinstance(error,TimeoutError):return 'timeout'
    return 'processing'

def retry(operation, *, attempts=2, notify=lambda message:None, checkpoint=lambda:None):
    for attempt in range(attempts):
        checkpoint()
        try:return operation()
        except Exception as error:
            kind=classify(error)
            if kind not in ('temporary_missing','file_busy') or attempt+1>=attempts:raise
            notify(f'Geçici sorun yeniden deneniyor ({attempt+1}/{attempts-1}): {kind}')
            time.sleep(.2*(attempt+1))
