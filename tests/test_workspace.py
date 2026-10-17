import errno
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from scantailor_ocr_unified import settings,storage
from scantailor_ocr_unified.queue_store import QueueStore,read_pages,submit,review_page
from scantailor_ocr_unified.recovery import retry,classify


class QueueTransactions(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.queue=self.root/'queue.json'
        self.data={'schema':1,'output_root':str(self.root),'jobs':[{'id':'book','title':'Book','folder':str(self.root),
            'pages':[{'page':i,'chars':120,'warning':i%10==0,'pdf_sha256':'old'} for i in range(1,1001)]}]}
        settings.atomic_json(self.queue,self.data);self.original=self.queue.read_bytes()
        self.store=QueueStore(self.queue);self.addCleanup(self.store.close);self.data=self.store.load(self.data)
    def test_migration_keeps_original_and_publishes_small_view(self):
        self.store.publish(self.data)
        self.assertEqual(self.queue.with_name('queue.json.pre-sqlite.json').read_bytes(),self.original)
        self.assertEqual(len(json.loads(self.queue.read_text('utf8'))['jobs'][0]['pages']),3)
        self.assertLess(self.queue.stat().st_size,len(self.original)/8)
        page=read_pages(self.queue,'book',500,100);self.assertEqual(page['total'],1000);self.assertEqual(page['pages'][0]['page'],501)
    def test_committed_page_survives_stale_json_and_uncommitted_process_crash(self):
        self.data['jobs'][0]['pages'][37]['chars']=999;self.store.save(self.data)
        code='import sqlite3,os,sys; d=sqlite3.connect(sys.argv[1]);d.execute("BEGIN");d.execute("DELETE FROM pages");os._exit(8)'
        run=subprocess.run([sys.executable,'-c',code,str(self.store.path)],creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(run.returncode,8)
        self.assertEqual(self.store.load(json.loads(self.original))['jobs'][0]['pages'][37]['chars'],999)
        self.assertEqual(read_pages(self.queue,'book')['total'],1000)
    def test_only_changed_page_is_written_and_review_follows_exact_pdf(self):
        statements=[];self.store.db.set_trace_callback(statements.append)
        self.data['jobs'][0]['pages'][9].update(chars=999);self.store.save(self.data)
        self.assertEqual(sum(s.startswith('INSERT OR REPLACE INTO pages') for s in statements),1)
        review_page(self.queue,'book',10,'old',True,'Checked against photograph')
        self.assertEqual(read_pages(self.queue,'book',problems=True)['total'],99)
        self.data['jobs'][0]['pages'][9].update(pdf_sha256='new');self.store.save(self.data)
        self.assertEqual(read_pages(self.queue,'book',problems=True)['total'],100)
        with self.assertRaises(ValueError):review_page(self.queue,'book',10,'old',True)
    def test_inbox_enqueue_prioritize_remove_is_durable_and_idempotent(self):
        submit(self.queue,'enqueue',job={'id':'second','title':'Next','folder':str(self.root)})
        submit(self.queue,'prioritize',job_id='book',priority=10)
        added,errors=self.store.apply_commands(self.data);self.assertFalse(errors);self.assertEqual(set(added),{'second','book'})
        self.assertEqual(self.store.apply_commands(self.data),([],[]))
        submit(self.queue,'remove',job_id='second');self.store.apply_commands(self.data)
        self.assertEqual([j['id'] for j in self.data['jobs']],['book'])


class StorageRecovery(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.env=patch.dict(os.environ,{'STOU_DATA':str(self.root)});self.env.start();self.addCleanup(self.env.stop)
    def test_quota_never_deletes_unregistered_or_pinned_or_changed_files(self):
        source=self.root/'source.tif';source.write_bytes(b'original')
        protected=self.root/'manual.png';protected.write_bytes(b'manual')
        preview=self.root/'preview.png';preview.write_bytes(b'reproducible')
        changed=self.root/'changed.png';changed.write_bytes(b'old')
        storage.register(protected,pinned=True,owner=source);storage.register(preview,owner=source);storage.register(changed,owner=source)
        changed.write_bytes(b'new manual edit')
        result=storage.apply_cleanup(storage.cleanup_plan(quota_gib=0))
        self.assertEqual(result['files'],1);self.assertFalse(preview.exists())
        self.assertTrue(source.exists() and protected.exists() and changed.exists())
    def test_cleanup_plan_invalidates_after_pin(self):
        p=self.root/'cache';p.write_bytes(b'preview');storage.register(p)
        plan=storage.cleanup_plan(quota_gib=0);storage.pin(p)
        with self.assertRaises(ValueError):storage.apply_cleanup(plan)
        self.assertTrue(p.exists())
    def test_disk_pressure_and_failed_workspace_cleanup(self):
        with patch('shutil.disk_usage',return_value=type('Usage',(),{'free':1024})()):
            with self.assertRaises(storage.StoragePressure):storage.ensure_space([self.root],reserve_gib=1)
        with self.assertRaises(RuntimeError):
            with storage.workspace({'root':str(self.root),'temp_root':str(self.root/'work'),'reserve_gib':0}) as folder:
                (folder/'partial.pdf').write_bytes(b'partial');raise RuntimeError('crash simulation')
        self.assertFalse(folder.exists())
    def test_completed_book_cleanup_checks_pdf_hash_and_keeps_edits(self):
        import hashlib
        from scantailor_ocr_unified.work_cleanup import reclaim
        output=self.root/'outputs';folder=output/'book';out=folder/'out';out.mkdir(parents=True)
        cache=folder/'.ocr-cache/1';cache.mkdir(parents=True)
        final=folder/'book.pdf';final.write_bytes(b'verified-pdf-fixture')
        page=cache/'page.pdf';page.write_bytes(final.read_bytes())
        project=folder/'book.ScanTailor';project.write_text('manual project settings')
        original=self.root/'source.png';original.write_bytes(b'original')
        manual=cache/'manual-review.json';manual.write_text('protected review')
        targets=[out/'page.tif',out/'page.tif.ocr.tif',cache/'display.tif',cache/'display-final.tif',cache/'ocr.png']
        for target in targets:target.write_bytes(b'reproducible')
        Path(str(project)+'.outputs.json').write_text(json.dumps({'pages':[str(targets[0])]}))
        job={'pdf':str(final),'pdf_hash':'wrong','project':str(project),'verification':{'all_pages_render_identical':True},'pages':[{'page_pdf':str(page)}]}
        with self.assertRaises(ValueError):reclaim(folder,output,job)
        self.assertTrue(all(p.exists() for p in targets))
        job['pdf_hash']=hashlib.sha256(final.read_bytes()).hexdigest()
        self.assertEqual(reclaim(folder,output,job)['files'],5)
        self.assertTrue(all(p.exists() for p in (original,manual,project,page,final)))
    def test_retry_is_bounded_and_missing_sources_are_not_retried(self):
        calls=[]
        def transient():
            calls.append(1)
            if len(calls)==1:raise FileNotFoundError('stou_pdf_abc/page.pdf')
            return 7
        self.assertEqual(retry(transient),7);self.assertEqual(len(calls),2)
        self.assertEqual(classify(OSError(errno.ENOSPC,'disk full')),'disk')
        calls.clear()
        def missing():calls.append(1);raise FileNotFoundError('source.tif')
        with self.assertRaises(FileNotFoundError):retry(missing)
        self.assertEqual(len(calls),1)


if __name__=='__main__':unittest.main()
