"""Transactional page checkpoints, with a small JSON view for the native UI.

The first migration retains the complete legacy JSON. Only the worker owns
SQLite writes. UI actions travel through immutable command files.
"""
from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import sqlite3
import time
import uuid
from contextlib import closing
from .settings import atomic_json


class Page(dict):
    def __init__(self, value, changed):
        super().__init__(value); self.changed = changed
    def __setitem__(self, key, value):
        super().__setitem__(key, value); self.changed(self)
    def update(self, *args, **kwargs):
        super().update(*args, **kwargs); self.changed(self)
    def __deepcopy__(self,memo):
        import copy
        return copy.deepcopy(dict(self),memo)


class Pages(list):
    def __init__(self, values=(), *, reset=False):
        self.dirty = {}; self.reset = reset
        super().__init__(Page(v, self.mark) for v in values)
        if reset:
            for value in self: self.mark(value)
    def mark(self, value): self.dirty[int(value['page'])] = value
    def append(self, value):
        value = Page(value, self.mark); super().append(value); self.mark(value)
    def __setitem__(self, index, value):
        if isinstance(index, slice): raise TypeError('Use a new Pages list for a slice replacement')
        value = Page(value, self.mark); super().__setitem__(index, value); self.mark(value)
    def clear(self):
        super().clear(); self.dirty.clear(); self.reset = True
    def __deepcopy__(self,memo):
        import copy
        return copy.deepcopy(list(self),memo)


class QueueStore:
    def __init__(self, queue):
        self.queue = Path(queue).resolve()
        self.path = self.queue.with_suffix(self.queue.suffix + '.sqlite3')
        self.db = sqlite3.connect(self.path, timeout=15)
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.execute('PRAGMA synchronous=FULL')
        self.db.executescript('''
          CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
          CREATE TABLE IF NOT EXISTS jobs(id TEXT PRIMARY KEY, position INTEGER NOT NULL, body TEXT NOT NULL);
          CREATE TABLE IF NOT EXISTS pages(job TEXT NOT NULL, number INTEGER NOT NULL, body TEXT NOT NULL,
            PRIMARY KEY(job,number));
          CREATE TABLE IF NOT EXISTS commands(id TEXT PRIMARY KEY, completed REAL NOT NULL);
          CREATE TABLE IF NOT EXISTS reviews(job TEXT, number INTEGER, sha TEXT, body TEXT NOT NULL,
            PRIMARY KEY(job,number,sha));
        ''')
        self.metadata = {}; self.job_cache = {}

    def close(self): self.db.close()

    def load(self, legacy):
        if not self.db.execute("SELECT 1 FROM meta WHERE key='initialized'").fetchone():
            if self.queue.is_file():
                backup = self.queue.with_name(self.queue.name + '.pre-sqlite.json')
                if not backup.exists():
                    with backup.open('xb') as dest, self.queue.open('rb') as src: shutil.copyfileobj(src, dest)
            for job in legacy['jobs']: job['pages'] = Pages(job.get('pages', []), reset=True)
            self.save(legacy)
            return legacy
        data = json.loads(self.db.execute("SELECT value FROM meta WHERE key='queue'").fetchone()[0])
        data['jobs'] = []
        for identifier, body in self.db.execute('SELECT id,body FROM jobs ORDER BY position'):
            job = json.loads(body)
            job['pages'] = Pages(json.loads(row[0]) for row in self.db.execute('SELECT body FROM pages WHERE job=? ORDER BY number', (identifier,)))
            data['jobs'].append(job)
        # A stopped GUI can add books to the small JSON view. Existing job
        # records remain authoritative in SQLite; revisions use their own inbox.
        known = {j['id'] for j in data['jobs']}
        data['jobs'].extend(j for j in legacy.get('jobs', []) if j['id'] not in known)
        if legacy.get('output_root'): data['output_root'] = legacy['output_root']
        return data

    def save(self, data, dirty_jobs=None):
        meta = {k: v for k, v in data.items() if k not in ('jobs', 'storage', 'schema')}
        saved = []
        with self.db:
            ids=[j['id'] for j in data['jobs']]
            if ids:
                placeholders=','.join('?' for _ in ids)
                self.db.execute('DELETE FROM jobs WHERE id NOT IN ('+placeholders+')',ids)
                self.db.execute('DELETE FROM pages WHERE job NOT IN ('+placeholders+')',ids)
            else:self.db.execute('DELETE FROM jobs');self.db.execute('DELETE FROM pages')
            self.db.execute('INSERT OR REPLACE INTO meta VALUES(?,?)', ('queue', json.dumps(meta, ensure_ascii=False)))
            for position, job in enumerate(data['jobs']):
                pages = job.get('pages', [])
                if not isinstance(pages, Pages): pages = job['pages'] = Pages(pages, reset=True)
                if dirty_jobs is None or job['id'] in dirty_jobs or job['id'] not in self.job_cache or self.job_cache[job['id']][0]!=position:
                    body = json.dumps({k: v for k, v in job.items() if k not in ('pages', 'page_count')}, ensure_ascii=False, sort_keys=True)
                    identity = (position, body)
                    if self.job_cache.get(job['id']) != identity:
                        self.db.execute('INSERT OR REPLACE INTO jobs VALUES(?,?,?)', (job['id'], position, body))
                        saved.append((job['id'], identity))
                if pages.reset: self.db.execute('DELETE FROM pages WHERE job=?', (job['id'],))
                self.db.executemany('INSERT OR REPLACE INTO pages VALUES(?,?,?)',
                    ((job['id'], n, json.dumps(p, ensure_ascii=False)) for n, p in pages.dirty.items()))
            self.db.execute('INSERT OR REPLACE INTO meta VALUES(?,?)', ('initialized', '1'))
        # Clear dirty flags only after COMMIT. A failed transaction is retryable.
        self.job_cache.update(saved)
        self.job_cache={k:v for k,v in self.job_cache.items() if k in ids}
        for job in data['jobs']: job['pages'].dirty.clear(); job['pages'].reset = False

    def publish(self, data):
        view = {k: v for k, v in data.items() if k != 'jobs'}
        view.update(schema=2, storage='sqlite', jobs=[])
        for job in data['jobs']:
            summary={k:v for k,v in job.items() if k not in ('pages','native_cache','metrics')}
            view['jobs'].append({**summary, 'page_count': len(job.get('pages', [])), 'pages': job.get('pages', [])[-3:]})
        atomic_json(self.queue, view)

    def apply_commands(self, data):
        changed = []; errors = []
        inbox = Path(str(self.queue) + '.commands')
        for path in sorted(inbox.glob('*.request.json')):
            try:
                command = json.loads(path.read_text('utf8')); identifier = command['id']
                if self.db.execute('SELECT 1 FROM commands WHERE id=?', (identifier,)).fetchone():
                    path.unlink(missing_ok=True); continue
                kind = command['action']; known = {j['id']: j for j in data['jobs']}
                if kind == 'enqueue':
                    job = command['job']
                    if not all(job.get(k) for k in ('id','title')): raise ValueError('Eksik kitap alanları')
                    source=job.get('folder') or job.get('source_project')
                    if not source or not Path(source).exists(): raise FileNotFoundError(source)
                    if job['id'] not in known: data['jobs'].append(job); changed.append(job['id'])
                elif kind == 'remove':
                    data['jobs'][:]=[j for j in data['jobs'] if j['id']!=command['job_id']]
                elif kind == 'prioritize':
                    job = known[command['job_id']]; job['priority'] = int(command.get('priority', 1)); changed.append(job['id'])
                elif kind == 'review':
                    job = known[command['job_id']]
                    page = next(p for p in job['pages'] if p['page'] == int(command['page']))
                    page.update(reviewed=bool(command['reviewed']), review_note=str(command.get('note', ''))[:4000], reviewed_at=time.time())
                    with self.db:self.db.execute('INSERT OR REPLACE INTO reviews VALUES(?,?,?,?)',
                        (job['id'],page['page'],page.get('pdf_sha256',''),json.dumps({k:page[k] for k in ('reviewed','review_note','reviewed_at')})))
                else: raise ValueError('Bilinmeyen kuyruk komutu')
                # Persist records and command receipt in one outer transaction.
                # save() commits first; repeating enqueue/prioritize/review is
                # deliberately idempotent if power fails before the receipt.
                self.save(data)
                with self.db: self.db.execute('INSERT OR REPLACE INTO commands VALUES(?,?)', (identifier, time.time()))
                path.unlink()
            except (OSError, ValueError, KeyError, StopIteration) as exc:
                errors.append(f'{path.name}: {exc}')
                atomic_json(path.with_suffix('.error.json'), {'error': str(exc)})
                path.replace(path.with_suffix('.rejected.json'))
        return changed, errors


def submit(queue, action, **values):
    identifier = uuid.uuid4().hex
    target = Path(str(Path(queue).resolve()) + '.commands') / (identifier + '.request.json')
    atomic_json(target, {'id': identifier, 'action': action, **values})
    return identifier


def read_pages(queue, job, offset=0, limit=100, problems=False):
    path = Path(queue).with_suffix(Path(queue).suffix + '.sqlite3')
    if not path.is_file():
        data = json.loads(Path(queue).read_text('utf8'))
        values = next(j for j in data['jobs'] if j['id'] == job).get('pages', [])
    else:
        # Read-only connection never migrates or acquires the worker lock.
        with closing(sqlite3.connect(path.resolve().as_uri() + '?mode=ro', uri=True, timeout=5)) as db:
            query = "SELECT p.body,r.body FROM pages p LEFT JOIN reviews r ON p.job=r.job AND p.number=r.number AND r.sha=COALESCE(json_extract(p.body,'$.pdf_sha256'),'') WHERE p.job=?"
            args = [job]
            if problems: query += " AND json_extract(p.body,'$.warning')=1 AND COALESCE(json_extract(r.body,'$.reviewed'),0)=0"
            total = db.execute('SELECT COUNT(*) FROM ('+query+')', args).fetchone()[0]
            values = [{**json.loads(r[0]),**(json.loads(r[1]) if r[1] else {})} for r in db.execute(query+' ORDER BY p.number LIMIT ? OFFSET ?', args+[max(1,min(500,int(limit))),max(0,int(offset))])]
            return {'total': total, 'offset': offset, 'pages': values}
    if problems: values = [p for p in values if p.get('warning') and not p.get('reviewed')]
    return {'total': len(values), 'offset': offset, 'pages': values[max(0,offset):max(0,offset)+max(1,min(500,limit))]}


def review_page(queue,job,number,sha,reviewed,note=''):
    path=Path(queue).with_suffix(Path(queue).suffix+'.sqlite3')
    if not path.is_file():
        return {'pending':True,'command':submit(queue,'review',job_id=job,page=number,reviewed=reviewed,note=note)}
    with closing(sqlite3.connect(path,timeout=15)) as db,db:
        row=db.execute('SELECT body FROM pages WHERE job=? AND number=?',(job,int(number))).fetchone()
        if not row or json.loads(row[0]).get('pdf_sha256','')!=sha:raise ValueError('Sayfa değişmiş; listeyi yenileyin.')
        value={'reviewed':bool(reviewed),'review_note':str(note)[:4000],'reviewed_at':time.time()}
        db.execute('INSERT OR REPLACE INTO reviews VALUES(?,?,?,?)',(job,int(number),sha,json.dumps(value,ensure_ascii=False)))
    return value
