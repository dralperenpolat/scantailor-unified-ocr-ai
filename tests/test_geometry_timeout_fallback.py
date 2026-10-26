import tempfile
import unittest
from pathlib import Path
import xml.etree.ElementTree as ET

from scantailor_ocr_unified.native_worker import use_rotation_after_timeout


class GeometryTimeoutFallback(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='stou_geo_retry_')
        self.project=Path(self.temp.name)/'kitap.ScanTailor'
        self.project.write_text('''<?xml version="1.0" encoding="utf-8"?>
<project><files><file id="1" name="çekim.JPG"/></files>
<images><image id="2" fileId="1"/></images>
<pages><page id="3" imageId="2" subPage="left"/><page id="4" imageId="2" subPage="right"/></pages>
<filters><deskew><page id="3"><params distortionType="warp"><warp mode="manual"/></params></page></deskew></filters></project>''',encoding='utf8')
        self.event={'stage':'Geometric Distortions','page':1,'detail':'çekim.JPG'}

    def tearDown(self):self.temp.cleanup()

    def test_changes_only_zero_based_active_page_and_keeps_backup(self):
        before=self.project.read_bytes()
        result=use_rotation_after_timeout(self.project,self.event)
        self.assertEqual(result['page'],2)
        self.assertEqual(Path(result['backup']).read_bytes(),before)
        root=ET.parse(self.project).getroot()
        self.assertEqual(root.find("./filters/deskew/page[@id='3']/params").get('distortionType'),'warp')
        self.assertEqual(root.find("./filters/deskew/page[@id='4']/params").get('distortionType'),'rotation')
        self.assertIsNone(use_rotation_after_timeout(self.project,self.event))

    def test_manual_geometry_and_unmatched_events_are_preserved(self):
        before=self.project.read_bytes()
        for changes in ({'page':0},{'detail':'another.JPG'},{'stage':'Output'},{'page':99}):
            self.assertIsNone(use_rotation_after_timeout(self.project,{**self.event,**changes}))
            self.assertEqual(self.project.read_bytes(),before)

    def test_forced_curves_never_silently_fall_back_to_rotation(self):
        tree=ET.parse(self.project);stage=tree.getroot().find('./filters/deskew')
        params=ET.SubElement(ET.SubElement(stage,'page',id='4'),'params',distortionType='warp',unifiedForceCurves='1')
        ET.SubElement(params,'warp',mode='auto');tree.write(self.project,encoding='utf-8')
        before=self.project.read_bytes()
        self.assertIsNone(use_rotation_after_timeout(self.project,self.event))
        self.assertEqual(self.project.read_bytes(),before)


if __name__=='__main__':unittest.main(verbosity=2)
