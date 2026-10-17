"""Bounded stage metrics and conservative resource profiles."""
import os
import time
from .memory_budget import available_bytes, bounded_workers


def resources(profile='balanced', requested=4):
    fraction={'fast':1.,'balanced':.65,'quiet':.3}[profile]
    cpu=max(1,min(int(requested),round((os.cpu_count() or 1)*fraction)))
    workers=bounded_workers(min(cpu,4 if profile=='fast' else 2 if profile=='balanced' else 1))
    return {'threads':cpu,'workers':workers,'available_bytes':available_bytes(),'profile':profile}


class Metrics:
    def __init__(self):
        self.stage=None;self.started=time.monotonic();self.stages={};self.peak_rss=0
    def event(self,stage):
        now=time.monotonic()
        if self.stage:
            row=self.stages.setdefault(self.stage,{'seconds':0.,'transitions':0})
            row['seconds']+=now-self.started
            if stage!=self.stage:row['transitions']+=1
        self.stage=stage;self.started=now
        try:
            import psutil
            process=psutil.Process();rss=process.memory_info().rss
            for child in process.children(recursive=True):
                try:rss+=child.memory_info().rss
                except psutil.Error:pass
            self.peak_rss=max(self.peak_rss,rss)
        except Exception:pass  # Optional telemetry cannot interrupt processing.
    def report(self):
        return {'stages':self.stages,'peak_process_tree_rss_bytes':self.peak_rss,'available_bytes':available_bytes(),
                'measurement':'stage wall time; sampled process tree RSS, not GPU memory'}
