"""Editing a processed book preserves its old edition and resumes durably."""
import copy,json,tempfile,unittest
from pathlib import Path
from unittest.mock import Mock,patch
from scantailor_ocr_unified.project import file_hash
from scantailor_ocr_unified.native_worker import Worker,write_json,load_queue,Cancelled,RevisionPending
from scantailor_ocr_unified.revision_queue import accept_revisions,revision_export_options

class RevisionsTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.queue=self.root/'queue.json'
        self.old=self.root/'previous.pdf';self.old.write_bytes(b'previous edition')
        self.project=self.root/'previous.ScanTailor';self.project.write_text('<project/>')
        self.inbox=Path(str(self.queue)+'.edits');self.inbox.mkdir()
        self.snapshot=self.inbox/'edited.ScanTailor';self.snapshot.write_text('<project><manual value="keep"/></project>')
        self.job={'id':'edited-book','title':'Book','pdf':str(self.old),'pdf_hash':file_hash(self.old),
                  'project':str(self.project),'work':str(self.root),'signature':'obsolete','native_cache':{'old':True}}
        self.data={'schema':1,'output_root':str(self.root/'books'),'jobs':[self.job,{'id':'next','title':'Next book'}]}
        (self.root/'books').mkdir();self.save=lambda:write_json(self.queue,self.data)
        self.save()
    def request(self,token='revision-1',**kw):
        path=self.inbox/(token+'.request.json')
        write_json(path,{'id':token,'job_id':'edited-book','snapshot':str(self.snapshot),'sha256':file_hash(self.snapshot),**kw})
        return path
    def worker(self,revision_only=False):
        worker=object.__new__(Worker);worker.request={'mode':'export','revision_only':revision_only,'paper':'a4_text','margin_mm':10}
        worker.settings={'paper':'a4_text','margin_mm':10};worker.queue_path=self.queue;worker.data=self.data;worker.job=None
        worker.control=self.root/'control.json';write_json(worker.control,{'state':'run'})
        worker.save=self.save;worker.checkpoint=Mock();worker.emit=Mock();worker.finalize_layout=Mock();worker.export=Mock()
        worker.prepare=Mock(return_value=(self.root,self.project,self.root))
        return worker
    def test_only_target_changes_and_previous_edition_stays_intact(self):
        before=copy.deepcopy(self.data['jobs'][1]);old_hash=file_hash(self.old);source_hash=file_hash(self.project)
        path=self.request();ids,errors=accept_revisions(self.data,self.queue,self.save)
        self.assertEqual((ids,errors),(['edited-book'],[]));self.assertFalse(path.exists())
        self.assertEqual(self.job['previous_editions'][0]['pdf'],str(self.old))
        self.assertEqual(file_hash(self.old),old_hash);self.assertEqual(file_hash(self.project),source_hash)
        self.assertTrue(Path(self.job['source_project']).samefile(self.snapshot));self.assertNotIn('signature',self.job)
        self.assertEqual(self.data['jobs'][1],before);self.assertEqual(load_queue(self.queue),self.data)
        self.request();self.assertEqual(accept_revisions(self.data,self.queue,self.save),([],[]))
        self.assertEqual(len(self.job['previous_editions']),1)
    def test_tampered_snapshot_is_rejected_without_job_change(self):
        before=copy.deepcopy(self.data);self.request();self.snapshot.write_text('<tampered/>')
        ids,errors=accept_revisions(self.data,self.queue,self.save)
        self.assertEqual(ids,[]);self.assertTrue(errors);self.assertEqual(self.data,before)
        self.assertTrue(list(self.inbox.glob('*.rejected.json')))
    def test_failed_save_keeps_request_and_rolls_back_memory(self):
        before=copy.deepcopy(self.data);path=self.request()
        with self.assertRaises(PermissionError):accept_revisions(self.data,self.queue,Mock(side_effect=PermissionError('disk error')))
        self.assertTrue(path.exists());self.assertEqual(self.data,before)
        self.assertEqual(accept_revisions(self.data,self.queue,self.save),(['edited-book'],[]))
    def test_acknowledgment_failure_is_idempotent_after_restart(self):
        path=self.request()
        with patch('scantailor_ocr_unified.revision_queue.os.replace',side_effect=PermissionError('reader lock')):
            ids,errors=accept_revisions(self.data,self.queue,lambda:None)
        self.assertEqual(ids,['edited-book']);self.assertTrue(errors);self.assertTrue(path.exists())
        self.assertEqual(accept_revisions(self.data,self.queue,self.save),([],[]))
        self.assertEqual(len(self.job['previous_editions']),1)
    def test_accepted_edit_resumes_after_worker_restart_only_once(self):
        self.request();accept_revisions(self.data,self.queue,self.save)
        worker=self.worker(True);worker.request['resume_at_job']='not-needed-for-edits'
        self.assertEqual(worker.run(),0);worker.export.assert_called_once()
        self.assertEqual(self.job['edit_completed_id'],'revision-1')
        worker.export.reset_mock();self.assertEqual(worker.run(),0);worker.export.assert_not_called()
    def test_new_edit_is_consumed_between_books(self):
        worker=self.worker();seen=[]
        def exported(*args):
            seen.append((worker.job['id'],worker.request['paper']))
            if len(seen)==1:self.request()
        worker.export.side_effect=exported
        self.assertEqual(worker.run(),0)
        self.assertEqual(seen,[('edited-book','a4_text'),('edited-book','a4'),('next','a4_text')])
        self.assertEqual(worker.request['paper'],'a4_text');self.assertEqual(worker.settings['paper'],'a4_text')
    def test_pdf_revision_exports_during_prepare_and_next_book_keeps_prepare_mode(self):
        worker=self.worker();worker.request['mode']='prepare';worker.native=Mock();seen=[]
        original=copy.deepcopy(worker.request)
        self.request(export_options={'ocr_timing':'after'})
        worker.export.side_effect=lambda *args:seen.append((worker.job['id'],worker.request['mode'],worker.request['ocr_timing']))
        self.assertEqual(worker.run(),0)
        self.assertEqual(seen,[('edited-book','export','after')])
        worker.native.assert_called_once()
        self.assertEqual(self.job['edit_completed_id'],'revision-1')
        self.assertEqual(worker.request,original)
        self.assertEqual(revision_export_options(original,self.job)['mode'],'prepare')
    def test_prepare_runs_new_and_interrupted_projects_once(self):
        source=self.root/'images';source.mkdir();(source/'1.png').write_bytes(b'fixture')
        for interrupted in (False,True):
            with self.subTest(interrupted=interrupted):
                worker=self.worker();worker.request['mode']='prepare';worker.cli=Path('cli')
                worker.data['jobs']=[self.job];self.job['folder']=str(source)
                worker.prepare=lambda:Worker.prepare(worker)
                def processed(args):
                    Path(self.job['project']).write_text('<project/>')
                worker.native=Mock(side_effect=processed)
                if interrupted:self.job['preparing']=True
                self.assertEqual(worker.run(),0)
                worker.native.assert_called_once();self.assertNotIn('preparing',self.job)
                worker.export.assert_not_called()
    def test_prepare_resume_skips_completed_books_and_preserves_previous_error(self):
        worker=self.worker();worker.request.update(mode='prepare',resume_at_job='next');worker.native=Mock()
        self.job.update(status='Düzenlemeye hazır');self.job.pop('pdf')
        failed={'id':'failed','title':'Previous error','status':'Hata','error':'Saved failure'}
        worker.data['jobs'].insert(1,failed);before=copy.deepcopy(worker.data['jobs'][:2])
        self.assertEqual(worker.run(),1)
        worker.prepare.assert_called_once();worker.native.assert_called_once()
        self.assertEqual(worker.data['jobs'][:2],before)
    def test_prepare_resume_does_not_skip_waiting_books(self):
        worker=self.worker();worker.request.update(mode='prepare',resume_at_job='next')
        self.job.pop('pdf');self.job['status']='Bekliyor'
        with self.assertRaisesRegex(ValueError,'tamamlanmamış'):worker.run()
        worker.prepare.assert_not_called()
    def test_interrupted_revision_stays_pending_for_restart(self):
        self.request();worker=self.worker(True);worker.export.side_effect=Cancelled()
        self.assertEqual(worker.run(),2);self.assertNotIn('edit_completed_id',self.job)
        worker.export.side_effect=None;self.assertEqual(worker.run(),0)
        self.assertEqual(self.job['edit_completed_id'],'revision-1')

    def test_first_book_quality_checkpoint_precedes_next_export(self):
        worker=self.worker();worker.request['pause_after_job']='edited-book';seen=[]
        def checkpoint():
            if worker.job['id']=='next':
                self.assertEqual(worker.control_state(),'pause')
                self.assertEqual(seen,['edited-book'])
                raise Cancelled()
        worker.checkpoint.side_effect=checkpoint
        worker.export.side_effect=lambda *args:seen.append(worker.job['id'])
        self.assertEqual(worker.run(),2)
        self.assertEqual(seen,['edited-book'])
        self.assertTrue(worker._qa_pause_done)

    def test_memory_failure_stops_at_current_book_and_resumes_without_losing_cache(self):
        worker=self.worker();next_before=copy.deepcopy(self.data['jobs'][1])
        self.job['page_cache']={'completed':410}
        worker.export.side_effect=MemoryError()
        self.assertEqual(worker.run(),3)
        self.assertEqual(worker.session_state,'stopped')
        worker.export.assert_called_once()
        self.assertEqual(self.data['jobs'][1],next_before)
        self.assertEqual(load_queue(self.queue)['jobs'][0]['page_cache'],{'completed':410})
        self.assertTrue(self.job['resource_retry'])
        self.assertEqual(self.job['error'],'MemoryError')
        worker.export.reset_mock();worker.export.side_effect=None
        self.assertEqual(worker.run(),0)
        self.assertEqual(worker.export.call_count,2)
        self.assertNotIn('resource_retry',self.job)
        self.assertNotIn('error',self.job)

    def test_native_bad_alloc_also_stops_before_next_book(self):
        worker=self.worker();worker.export.side_effect=RuntimeError('native failed: std::bad_alloc')
        self.assertEqual(worker.run(),3)
        worker.export.assert_called_once()
        self.assertNotIn('error',self.data['jobs'][1])
    def test_manual_margins_are_not_recropped_and_opt_out_remains_available(self):
        request={'paper':'a4_text','margin_mm':10,'content_fit':True}
        self.assertIs(revision_export_options(request,self.job),request)
        self.job['edit_request_id']='revision-1'
        options=revision_export_options(request,self.job)
        self.assertEqual(options['paper'],'a4');self.assertEqual(options['margin_mm'],0);self.assertFalse(options['content_fit'])
        self.job['preserve_edit_margins']=False
        self.assertIs(revision_export_options(request,self.job),request)

    def test_repeated_save_of_identical_snapshot_does_not_restart_or_duplicate_history(self):
        self.request('click-1');self.request('click-2');self.request('click-3')
        ids,errors=accept_revisions(self.data,self.queue,self.save)
        self.assertEqual((ids,errors),(['edited-book'],[]))
        self.assertEqual(len(self.job['previous_editions']),1)
        self.assertEqual(len(self.job['accepted_edits']),3)
        self.assertEqual(len(list(self.inbox.glob('*.request.json'))),0)

    def test_output_only_change_is_a_new_revision_and_scoped_to_that_book(self):
        self.request('click-1',export_options={'ocr_timing':'none','output_finish':{'white':False}})
        accept_revisions(self.data,self.queue,self.save)
        self.request('click-2',export_options={'ocr_timing':'after','output_finish':{'white':True}})
        ids,errors=accept_revisions(self.data,self.queue,self.save)
        self.assertEqual((ids,errors),(['edited-book'],[]))
        self.assertEqual(self.job['edit_request_id'],'click-2')
        source={'paper':'a4_text','ocr_timing':'during'}
        changed=revision_export_options(source,self.job)
        self.assertEqual(changed['ocr_timing'],'after');self.assertTrue(changed['output_finish']['white'])
        self.assertEqual(source,{'paper':'a4_text','ocr_timing':'during'})

    def test_revision_interrupts_ordinary_book_at_checkpoint_then_resumes_it(self):
        worker=self.worker();seen=[];worker.data['jobs'].reverse()
        def exported(*args):
            seen.append(worker.job['id'])
            if len(seen)==1:self.request();raise RevisionPending()
        worker.export.side_effect=exported
        self.assertEqual(worker.run(),0)
        self.assertEqual(seen[:3],['next','edited-book','next'])

    def test_paused_book_yields_to_explicit_revision_but_stop_is_respected(self):
        worker=self.worker();worker.job=self.data['jobs'][1];worker.control=self.root/'control.json'
        write_json(worker.control,{'state':'pause'});self.request()
        with self.assertRaises(RevisionPending):Worker.checkpoint(worker)
        accept_revisions(self.data,self.queue,self.save);worker.job=self.job
        worker._revision_pause_bypass=True
        Worker.checkpoint(worker)  # explicit revision may run; ordinary queue remains paused
        self.assertEqual(json.loads(worker.control.read_text())['state'],'pause')
        write_json(worker.control,{'state':'run'});self.assertEqual(worker.effective_control_state(),'run')
        write_json(worker.control,{'state':'pause'});self.assertEqual(worker.effective_control_state(),'pause')
        write_json(worker.control,{'state':'cancel'})
        with self.assertRaises(Cancelled):Worker.checkpoint(worker)

if __name__=='__main__':unittest.main()
