"""Changing page appearance/layout must reuse aligned OCR, with pixel checks."""
import json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
from PIL import Image,ImageDraw,ImageFont
import fitz
from scantailor_ocr_unified.pdf_export import export

ROOT=Path(__file__).resolve().parent.parent

class RecognitionReuse(unittest.TestCase):
    def test_new_appearance_and_paper_reuse_ocr_without_moving_text(self):
        with tempfile.TemporaryDirectory(prefix='unified_ocr_cache_') as directory:
            base=Path(directory)
            image=Image.new('L',(1240,1754),255);draw=ImageDraw.Draw(image)
            font=ImageFont.load_default(size=22)
            words='A synthetic page checks OCR reuse, small notes and coordinate alignment.'
            with fitz.open() as recognized:
                page=recognized.new_page(width=1240*72/300,height=1754*72/300)
                for n in range(30):
                    draw.text((80,110+n*40),words,fill=25,font=font)
                    page.insert_text((80*72/300,(110+n*40+20)*72/300),words,fontsize=5,render_mode=3)
                recognized_bytes=recognized.tobytes()
            image.save(base/'gray.tif',compression='tiff_deflate')
            image.convert('1').save(base/'binary.tif',compression='group4')
            image.save(base/'ocr.png')
            # The recognizer is isolated; the real export/cache/geometry are used.
            (base/'eng.traineddata').write_bytes(b'synthetic-recognizer-version')
            conf={'tessdata':str(base),'language':'eng','ocr_cache':str(base/'text-cache'),
                  'pages':[{'display_image':str(base/'gray.tif'),'ocr_image':str(base/'ocr.png'),'page_mm':[210,297]}]}
            config=base/'config.json';config.write_text(json.dumps(conf),encoding='utf8')
            with patch.object(fitz.Pixmap,'pdfocr_tobytes',return_value=recognized_bytes) as recognize:
                first=export(config,base/'first.pdf',notify=lambda _:None)
                self.assertEqual(recognize.call_count,1)
            conf['pages'][0].update(display_image=str(base/'binary.tif'),page_mm=[180,250])
            config.write_text(json.dumps(conf),encoding='utf8')
            with patch.object(fitz.Pixmap,'pdfocr_tobytes',side_effect=AssertionError('OCR unnecessarily repeated')):
                second=export(config,base/'second.pdf',notify=lambda _:None)
            self.assertGreater(first['checks'][0]['ocr_characters'],1000)
            self.assertEqual(first['checks'][0]['ocr_characters'],second['checks'][0]['ocr_characters'])
            self.assertTrue(second['checks'][0]['pixel_identical'])
            self.assertTrue(second['checks'][0]['ocr_visible_pixels_unchanged'])
            self.assertEqual(len(list((base/'text-cache').glob('*.pdf'))),1)
            with fitz.open(base/'first.pdf') as a,fitz.open(base/'second.pdf') as b:
                self.assertEqual(a[0].get_text(),b[0].get_text())
                # Same word/image coordinates after normalization to page geometry.
                wa=a[0].get_text('words');wb=b[0].get_text('words')
                self.assertEqual([v[4] for v in wa],[v[4] for v in wb])

if __name__=='__main__':unittest.main(verbosity=2)
