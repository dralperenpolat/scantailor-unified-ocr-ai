"""Read available RAM/commit headroom without changing system settings."""
import ctypes
import os
from pathlib import Path

def available_bytes():
    if os.name!='nt':
        try:
            fields=dict(line.split(':',1) for line in Path('/proc/meminfo').read_text().splitlines())
            available=int(fields['MemAvailable'].split()[0])*1024
            # Respect container/cgroup limits as well as host free RAM.
            cg=Path('/sys/fs/cgroup')
            limit=(cg/'memory.max').read_text().strip() if (cg/'memory.max').exists() else 'max'
            if limit!='max':available=min(available,max(0,int(limit)-int((cg/'memory.current').read_text())))
            return available
        except (OSError,KeyError,ValueError):return None
    class Status(ctypes.Structure):
        _fields_=[('length',ctypes.c_ulong),('load',ctypes.c_ulong),
                  *[(name,ctypes.c_ulonglong) for name in ('total_phys','avail_phys','total_commit','avail_commit','total_virtual','avail_virtual','extended')]]
    value=Status();value.length=ctypes.sizeof(value)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(value)):return None
    return min(value.avail_phys,value.avail_commit)

def is_memory_failure(error):
    text=str(error).lower()
    return isinstance(error,MemoryError) or any(s in text for s in ('std::bad_alloc','unable to allocate','out of memory','not enough memory'))

def bounded_workers(requested,bytes_per_worker=2*1024**3,reserve=2*1024**3):
    available=available_bytes()
    return max(1,min(int(requested),int(max(0,available-reserve)//bytes_per_worker))) if available is not None else max(1,int(requested))
