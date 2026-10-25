import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('install_state',Path(__file__).parents[1]/'packaging/install_state.py')
state=importlib.util.module_from_spec(spec);spec.loader.exec_module(state)


class InstalledProfile(unittest.TestCase):
    def fixture(self,base,name,active=False):
        data=base/name;folder=data/'projects/Native_Kitaplar';folder.mkdir(parents=True)
        queue=folder/'kuyruk.json';queue.write_text(json.dumps({'schema':2,'jobs':[{'id':'book'}]}))
        request=folder/'request-current.json';request.write_text(json.dumps({'queue':str(queue),'control':str(folder/'control.json')}))
        Path(str(request)+'.session.json').write_text(json.dumps({'state':'running' if active else 'finished','pid':42,'updated':100}))
        return {'name':name,'values':{r'unified\last_queue':str(queue)},'modified':1},data

    def test_active_portable_queue_and_data_survive_installed_profile(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp);legacy,data=self.fixture(base,state.APP+'-062',True)
            before={p:p.read_bytes() for p in data.rglob('*.json')}
            stable={'name':state.APP,'values':{},'modified':100}
            chosen=state.select_profile([stable,legacy],base,lambda pid:pid==42)
            self.assertEqual(chosen['name'],legacy['name']);self.assertEqual(Path(chosen['data_root']),data)
            self.assertTrue(chosen['values'][r'unified\last_request'].endswith('request-current.json'))
            self.assertEqual(before,{p:p.read_bytes() for p in before})

    def test_versioned_profile_can_reference_an_older_data_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp);profile,data=self.fixture(base,state.APP+'-062')
            profile['name']=state.APP+'-063'
            self.assertEqual(state.data_directory(profile,base),data)

    def test_future_upgrades_preserve_installed_settings(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp);legacy,_=self.fixture(base,state.APP+'-063',True)
            stable={'name':state.APP,'values':{r'unified\installed_profile_migrated':1,r'unified\ocr_languages':'deu'},'modified':0}
            self.assertEqual(state.select_profile([stable,legacy],base,lambda pid:True)['values'][r'unified\ocr_languages'],'deu')

    def test_stale_pid_and_corrupt_request_do_not_override_saved_queue(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp);stable,data=self.fixture(base,state.APP)
            legacy,old=self.fixture(base,state.APP+'-063',True)
            (old/'projects/Native_Kitaplar/request-current.json').write_text('{broken')
            chosen=state.select_profile([stable,legacy],base,lambda pid:False)
            self.assertEqual(chosen['name'],state.APP);self.assertFalse(chosen['active_at'])


if __name__=='__main__':unittest.main()
