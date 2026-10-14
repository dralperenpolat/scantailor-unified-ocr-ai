"""One writer per queue; a crashed worker releases the OS lock automatically."""
from contextlib import contextmanager
from pathlib import Path
import os

@contextmanager
def exclusive_queue(path):
    path=Path(str(path)+'.lock');path.parent.mkdir(parents=True,exist_ok=True)
    handle=path.open('a+b')
    if path.stat().st_size==0:handle.write(b'0');handle.flush()
    handle.seek(0)
    try:
        try:
            if os.name=='nt':
                import msvcrt
                msvcrt.locking(handle.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(handle,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except OSError as exc:
            raise RuntimeError('Bu kuyruk başka bir işçi tarafından işleniyor; ikinci işlem başlatılmadı.') from exc
        yield
    finally:
        handle.close()
