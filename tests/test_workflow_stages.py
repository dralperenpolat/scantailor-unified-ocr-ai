import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import fitz
import numpy as np
from PIL import Image, ImageDraw
from scantailor_ocr_unified import workflow_stages as wf


class OrderedWorkflow(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.rows = []
        for number in (1, 2):
            with fitz.open() as doc:
                p = doc.new_page(width=400, height=600)
                for line in range(32):
                    p.insert_text((45, 65+14*line), "Printed text 123 and footnotes, ABC xyz.", fontsize=10)
                pix = p.get_pixmap(matrix=fitz.Matrix(2, 2), colorspace=fitz.csGRAY)
                image = Image.frombytes("L", (pix.width, pix.height), pix.samples)
            draw = ImageDraw.Draw(image)
            draw.rectangle((0, 280, 28, 370), fill=0)
            source = self.root / ("page%d.tif" % number)
            image.save(source)
            image.save(str(source)+".ocr.tif")
            self.rows.append({"page": number, "source": str(source), "gray": str(source)+".ocr.tif"})
        work = self.root / "work"
        work.mkdir()
        self.req = {"pages": self.rows, "work": str(work), "output_dir": str(self.root / "pdf"),
                    "project_fingerprint": "current-settings", "appearance": "native",
                    "paper": "a4_text", "margin_mm": 10, "target_x_mm": 0, "title": "Turkce test",
                    "reserve_gib": 0, "threads": 1}
        self.originals = {p: wf.file_hash(p) for row in self.rows for p in (row["source"], row["gray"])}

    def reviewed(self):
        result = wf.cleanup(self.req, lambda event: None)
        self.req["cleanup_review"] = result["receipt"]
        return result

    def test_cleanup_is_a_proposal_and_preserves_text_and_sources(self):
        result = self.reviewed()
        self.assertFalse(result["applied"])
        self.assertEqual(len(result["pages"]), 2)
        self.assertTrue(result["pages"][0]["report"]["removed"], "test fixture must exercise actual removal")
        with Image.open(result["pages"][0]["mask"]) as im:
            mask = np.asarray(im)
        self.assertTrue(mask[:, :50].any())
        self.assertFalse(mask[120:1030, 100:700].any(), "body text must be protected")
        for path, stamp in self.originals.items():
            self.assertEqual(stamp, wf.file_hash(path))

    def test_apply_refuses_upstream_setting_changes_and_undo_remains_possible(self):
        result = self.reviewed()
        self.req["project_fingerprint"] = "manual-content-edit"
        with self.assertRaises(ValueError):
            wf.approve_cleanup(self.req, True)
        self.assertFalse(wf.approve_cleanup(self.req, False)["applied"])

    def test_apply_refuses_output_or_mask_mutation(self):
        result = self.reviewed()
        mask = Path(result["pages"][0]["mask"])
        mask.write_bytes(mask.read_bytes()+b"changed")
        with self.assertRaises(ValueError):
            wf.approve_cleanup(self.req, True)
        result = self.reviewed()
        with Image.open(self.rows[0]["source"]) as im:
            changed = im.copy()
            im.close()
        changed.putpixel((0, 0), 123)
        changed.save(self.rows[0]["source"])
        with self.assertRaises(ValueError):
            wf.approve_cleanup(self.req, True)

    def test_pdf_includes_only_approved_cleanup_and_leaves_tiffs_unchanged(self):
        self.reviewed()
        before = wf.create_pdf(self.req, lambda event: None)
        self.assertFalse(before["cleanup_applied"])
        wf.approve_cleanup(self.req, True)
        after = wf.create_pdf(self.req, lambda event: None)
        self.assertTrue(after["cleanup_applied"])
        self.assertNotEqual(before["pdf"], after["pdf"])
        with fitz.open(before["pdf"]) as a, fitz.open(after["pdf"]) as b:
            self.assertEqual(len(b), 2)
            self.assertFalse(b[0].get_text())
            self.assertNotEqual(a[0].get_pixmap().samples, b[0].get_pixmap().samples)
            self.assertAlmostEqual(b[0].rect.width, 210*72/25.4, places=3)
        for path, stamp in self.originals.items():
            self.assertEqual(stamp, wf.file_hash(path))

    def test_pdf_rejects_stale_applied_cleanup(self):
        self.reviewed()
        wf.approve_cleanup(self.req, True)
        self.req["project_fingerprint"] = "new-content-box"
        with self.assertRaises(ValueError):
            wf.create_pdf(self.req, lambda event: None)
        self.assertFalse(list((self.root/"pdf").glob("*.pdf")))

    def test_pdf_rejects_sources_changing_while_rendering(self):
        mutated = False
        def progress(event):
            nonlocal mutated
            if not mutated:
                mutated = True
                with Image.open(self.rows[0]["source"]) as im:
                    changed = im.copy()
                    im.close()
                changed.putpixel((50, 50), 1)
                changed.save(self.rows[0]["source"])
        with self.assertRaises(ValueError):
            wf.create_pdf(self.req, progress)
        self.assertFalse(list((self.root/"pdf").glob("*.pdf")))

    def test_pdf_content_fit_works_before_ocr(self):
        self.req.update(content_fit=True, paper="a4_fill")
        result = wf.create_pdf(self.req, lambda event: None)
        self.assertTrue(result["verification"]["image_pixels_identical"])
        for row in result["layout"]["pages"]:
            self.assertAlmostEqual(row["scale"], row["capacity"])
        with fitz.open(result["pdf"]) as doc:
            self.assertEqual(len(doc), 2)
            self.assertEqual(doc[0].get_text(), "")

    def test_pdf_matching_text_size_records_measurement_without_ocr(self):
        result = wf.create_pdf(self.req, lambda event: None)
        rows = result["layout"]["pages"]
        self.assertTrue(rows[0]["body_measurement"]["pixels"])
        self.assertAlmostEqual(rows[0]["body_mm"], rows[1]["body_mm"])
        self.assertFalse(result["verification"]["ocr"])

    def test_compression_preserves_master_and_incremental_page_reuse(self):
        self.req.update(compression_profile="small")
        first = wf.create_pdf(self.req, lambda event: None)
        self.assertNotEqual(first["reading_pdf"], first["pdf"])
        master_hash = wf.file_hash(first["pdf"])
        self.assertEqual(first["compressed"]["source_sha256"], master_hash)
        self.assertLessEqual(Path(first["reading_pdf"]).stat().st_size, Path(first["pdf"]).stat().st_size)
        self.req["previous_pdf"] = first["pdf"]
        self.req.update(compression_profile="balanced")
        with patch.object(wf, "render", side_effect=AssertionError("unchanged pages must not render again")):
            second = wf.create_pdf(self.req, lambda event: None)
        self.assertEqual(second["reused_pages"], [1, 2])
        self.assertEqual(wf.file_hash(first["pdf"]), master_hash)
        self.assertNotEqual(first["reading_pdf"], second["reading_pdf"])
        saved = json.loads(Path(second["pdf"]).with_suffix(".workflow.json").read_text("utf8"))
        self.assertEqual(saved["reading_pdf"], second["reading_pdf"])

    def test_binary_export_uses_ccitt_without_thresholding_gray_pages(self):
        with Image.open(self.rows[0]["source"]) as image:
            binary = image.convert("1")
            gray = image.convert("L")
            image.close()
        with fitz.open() as compact, fitz.open() as reference:
            a = compact.new_page(width=400, height=600)
            b = reference.new_page(width=400, height=600)
            wf.embed_image(a, a.rect, binary.convert("L"))
            stream = io.BytesIO();binary.save(stream, format="PNG")
            b.insert_image(b.rect, stream=stream.getvalue())
            images = a.get_images(full=True)
            self.assertEqual(images[0][4], 1)
            self.assertIn("/CCITTFaxDecode", compact.xref_get_key(images[0][0], "Filter")[1])
            self.assertEqual(a.get_pixmap().samples, b.get_pixmap().samples)
            c = compact.new_page(width=400, height=600)
            wf.embed_image(c, c.rect, gray)
            self.assertEqual(c.get_images(full=True)[0][4], 8)

    def test_deep_lossless_option_is_used_by_the_editor_export(self):
        self.req.update(compression_profile="lossless",compression_options={"recompress_flate":True})
        result=wf.create_pdf(self.req,lambda event: None)
        self.assertNotEqual(result["reading_pdf"],result["pdf"])
        self.assertTrue(result["compressed"]["options"]["recompress_flate"])
        self.assertTrue(result["compressed"]["lossless"])

    def test_invalid_compression_settings_fail_before_export(self):
        self.req.update(compression_profile="target",compression_options={"min_dpi":400})
        with self.assertRaises(ValueError):wf.create_pdf(self.req,lambda event: None)
        self.assertFalse(Path(self.req["output_dir"]).exists())

    def test_ocr_reading_copy_keeps_text_and_uses_lossless_master(self):
        base = wf.create_pdf(self.req, lambda event: None)
        with fitz.open() as layer:
            layer.new_page(width=595.2756, height=841.8898).insert_text((40, 65), "OCR 123 example", render_mode=3)
            payload = layer.tobytes()
        self.req.update(source_pdf=base["pdf"], compression_profile="small")
        with patch.object(fitz.Pixmap, "pdfocr_tobytes", return_value=payload):
            result = wf.add_ocr(self.req, lambda event: None)
        with fitz.open(result["pdf"]) as master, fitz.open(result["reading_pdf"]) as reading:
            self.assertIn("OCR 123", reading[0].get_text())
            self.assertEqual(master[0].get_text("words"), reading[0].get_text("words"))
        self.assertEqual(result["sha256"], wf.file_hash(result["pdf"]))

    def test_ocr_uses_final_pdf_preserves_visible_pixels_and_original(self):
        base = wf.create_pdf(self.req, lambda event: None)
        source = Path(base["pdf"])
        stamp = wf.file_hash(source)
        # Deterministic text engine adapter: real PDF overlay and pixel checks
        # still run. Real Tesseract is also covered by release/offline smoke.
        with fitz.open() as doc:
            p = doc.new_page(width=595.2756, height=841.8898)
            p.insert_text((40, 65), "Recognized test text 123", render_mode=3)
            layer = doc.tobytes()
        self.req["source_pdf"] = str(source)
        with patch.object(fitz.Pixmap, "pdfocr_tobytes", return_value=layer):
            result = wf.add_ocr(self.req, lambda event: None)
        self.assertEqual(stamp, wf.file_hash(source))
        with fitz.open(source) as a, fitz.open(result["pdf"]) as b:
            self.assertIn("Recognized", b[0].get_text())
            for x, y in zip(a, b):
                self.assertEqual(x.get_pixmap(dpi=120).samples, y.get_pixmap(dpi=120).samples)

    def test_review_can_cover_one_page_without_touching_others(self):
        self.req["pages"] = self.rows[:1]
        self.reviewed()
        wf.approve_cleanup(self.req, True)
        self.req["pages"] = self.rows
        result = wf.create_pdf(self.req, lambda event: None)
        self.assertEqual(result["pages"], 2)
        self.assertTrue(result["cleanup_applied"])

    def test_one_changed_page_reuses_other_page_and_ocr_layer(self):
        for i, row in enumerate(self.rows):
            row.update(key="page-"+str(i), input_stamp="original-"+str(i))
        base = wf.create_pdf(self.req, lambda event: None)
        self.req["source_pdf"] = base["pdf"]
        with fitz.open() as doc:
            p = doc.new_page(width=595.2756, height=841.8898)
            p.insert_text((40, 65), "Existing OCR 123", render_mode=3)
            layer = doc.tobytes()
        with patch.object(fitz.Pixmap, "pdfocr_tobytes", return_value=layer):
            old = wf.add_ocr(self.req, lambda event: None)
        self.req["previous_pdf"] = old["pdf"]
        self.rows[0]["input_stamp"] = "manual-content-correction"
        with Image.open(self.rows[0]["source"]) as im:
            changed_image = im.copy()
            im.close()
        ImageDraw.Draw(changed_image).rectangle((90, 100, 120, 110), fill=128)
        changed_image.save(self.rows[0]["source"])
        original_render = wf.render
        with patch.object(wf, "render", wraps=original_render) as rendered:
            revised = wf.create_pdf(self.req, lambda event: None)
        self.assertEqual(revised["changed_pages"], [1])
        self.assertEqual(revised["reused_pages"], [2])
        self.assertEqual(rendered.call_count, 1)
        with fitz.open(old["pdf"]) as a, fitz.open(revised["pdf"]) as b:
            self.assertEqual(a[1].get_pixmap(dpi=120).samples, b[1].get_pixmap(dpi=120).samples)
            self.assertEqual(a[1].get_text(), b[1].get_text())
            self.assertTrue(b[1].get_text().strip())
            self.assertEqual(b[0].get_text(), "")
        self.req["source_pdf"] = revised["pdf"]
        with patch.object(fitz.Pixmap, "pdfocr_tobytes", return_value=layer) as ocr:
            final = wf.add_ocr(self.req, lambda event: None)
        self.assertEqual(ocr.call_count, 1, "only changed page must receive OCR")
        self.assertEqual(final["changed_pages"], [1])
        self.assertEqual(final["reused_pages"], [2])
        self.assertEqual(final["records"][1]["layout"], old["records"][1]["layout"])

    def test_no_change_does_not_measure_render_or_recognize_any_page(self):
        base = wf.create_pdf(self.req, lambda event: None)
        self.req["previous_pdf"] = base["pdf"]
        with patch.object(wf, "render", side_effect=AssertionError("unexpected render")), patch("scantailor_ocr_unified.book_layout.plan", side_effect=AssertionError("unexpected measurement")):
            result = wf.create_pdf(self.req, lambda event: None)
        self.assertEqual(result["changed_pages"], [])
        self.assertEqual(result["reused_pages"], [1, 2])

    def test_global_paper_setting_intentionally_updates_both_pages(self):
        base = wf.create_pdf(self.req, lambda event: None)
        self.req.update(previous_pdf=base["pdf"], paper="native")
        result = wf.create_pdf(self.req, lambda event: None)
        self.assertEqual(result["changed_pages"], [1, 2])
        self.assertEqual(result["reused_pages"], [])

    def test_stale_cleanup_affects_only_edited_page(self):
        for i, row in enumerate(self.rows):
            row.update(key=str(i), input_stamp="old")
        self.reviewed()
        wf.approve_cleanup(self.req, True)
        first = wf.create_pdf(self.req, lambda event: None)
        self.req["previous_pdf"] = first["pdf"]
        self.rows[0]["input_stamp"] = "changed geometry"
        result = wf.create_pdf(self.req, lambda event: None)
        self.assertEqual(result["cleanup_needs_review"], [1])
        self.assertEqual(result["changed_pages"], [1])
        self.assertEqual(result["reused_pages"], [2])
        self.assertTrue(result["records"][1]["cleanup_hash"])

    def test_reviewing_one_page_keeps_other_pages_approved(self):
        for i, row in enumerate(self.rows):
            row.update(key=str(i), input_stamp="old")
        self.reviewed()
        wf.approve_cleanup(self.req, True)
        self.req["pages"] = self.rows[:1]
        self.rows[0]["input_stamp"] = "edited"
        result = self.reviewed()
        self.assertFalse(result["pages"][0]["applied"])
        self.assertTrue(result["pages"][1]["applied"])
        self.req["pages"] = self.rows
        wf.approve_cleanup(self.req, True)
        final = wf.create_pdf(self.req, lambda event: None)
        self.assertFalse(final["cleanup_needs_review"])

    def test_new_output_folder_keeps_previous_pdf_and_reuses_pages(self):
        first = wf.create_pdf(self.req, lambda event: None)
        self.req.update(previous_pdf=first["pdf"], output_dir=str(self.root/"other output"))
        result = wf.create_pdf(self.req, lambda event: None)
        self.assertEqual(Path(result["pdf"]).parent, self.root/"other output")
        self.assertTrue(Path(first["pdf"]).exists())
        self.assertEqual(result["reused_pages"], [1, 2])

    def test_cache_cleanup_does_not_force_unchanged_pages_to_render(self):
        for i, row in enumerate(self.rows):
            row.update(key=str(i), input_stamp="same")
        first = wf.create_pdf(self.req, lambda e: None)
        self.req["previous_pdf"] = first["pdf"]
        backups = {p: Path(p).read_bytes() for row in self.rows for p in (row["source"], row["gray"])}
        for path in backups:
            Path(path).unlink()
        with patch.object(wf, "render", side_effect=AssertionError("cache removal is not a page edit")):
            second = wf.create_pdf(self.req, lambda e: None)
        self.assertEqual(second["reused_pages"], [1, 2])
        self.req["previous_pdf"] = second["pdf"]
        self.rows[0]["input_stamp"] = "edited"
        with self.assertRaisesRegex(ValueError, "Regenerate"):
            wf.create_pdf(self.req, lambda e: None)
        for key in ("source", "gray"):
            path = self.rows[0][key]
            Path(path).write_bytes(backups[path])
        third = wf.create_pdf(self.req, lambda e: None)
        self.assertEqual(third["changed_pages"], [1])
        self.assertEqual(third["reused_pages"], [2])

    def test_cleanup_approval_remains_valid_when_native_cache_is_removed(self):
        for i, row in enumerate(self.rows):
            row.update(key=str(i), input_stamp="same")
        self.reviewed()
        wf.approve_cleanup(self.req, True)
        first = wf.create_pdf(self.req, lambda e: None)
        self.req["previous_pdf"] = first["pdf"]
        for row in self.rows:
            for key in ("source", "gray"):
                Path(row[key]).unlink()
        second = wf.create_pdf(self.req, lambda e: None)
        self.assertEqual(second["reused_pages"], [1, 2])
        self.assertEqual(second["cleanup_needs_review"], [])

    def test_changed_ocr_language_replaces_old_text_without_changing_pixels(self):
        base = wf.create_pdf(self.req, lambda e: None)
        def layer(text):
            with fitz.open() as doc:
                p = doc.new_page(width=595.2756, height=841.8898)
                p.insert_text((40, 65), text, render_mode=3)
                return doc.tobytes()
        self.req.update(source_pdf=base["pdf"], language="eng")
        with patch.object(fitz.Pixmap, "pdfocr_tobytes", return_value=layer("Old recognition")):
            old = wf.add_ocr(self.req, lambda e: None)
        self.req.update(source_pdf=old["pdf"], language="deu")
        with patch.object(fitz.Pixmap, "pdfocr_tobytes", return_value=layer("New recognition")) as recognized:
            new = wf.add_ocr(self.req, lambda e: None)
        self.assertEqual(recognized.call_count, 2)
        with fitz.open(old["pdf"]) as a, fitz.open(new["pdf"]) as b:
            for old_page, new_page in zip(a, b):
                self.assertEqual(old_page.get_pixmap().samples, new_page.get_pixmap().samples)
                self.assertNotIn("Old recognition", new_page.get_text())
                self.assertIn("New recognition", new_page.get_text())

    def test_project_ocr_cache_contains_only_text_and_can_be_removed(self):
        base = wf.create_pdf(self.req, lambda e: None)
        self.req.update(source_pdf=base["pdf"], ocr_cache=str(self.root/"ocr-cache"), language="eng")
        with fitz.open() as doc:
            p = doc.new_page(width=595.2756, height=841.8898)
            p.insert_text((40, 65), "Cached recognition", render_mode=3)
            layer = doc.tobytes()
        with patch("scantailor_ocr_unified.pdf_export.recognition_signature", return_value=["model-test"]), patch.object(fitz.Pixmap, "pdfocr_tobytes", return_value=layer) as recognize:
            first = wf.add_ocr(self.req, lambda e: None)
            calls = recognize.call_count
            second = wf.add_ocr(self.req, lambda e: None)
            self.assertEqual(recognize.call_count, calls)
            cache = list((self.root/"ocr-cache").glob("*.pdf"))
            self.assertTrue(cache)
            for path in cache:
                with fitz.open(path) as doc:
                    self.assertEqual(doc[0].get_images(), [])
            for path in (self.root/"ocr-cache").iterdir():
                path.unlink()
            wf.add_ocr(self.req, lambda e: None)
            self.assertGreater(recognize.call_count, calls)
        self.assertTrue(Path(first["pdf"]).is_file())
        self.assertTrue(Path(second["pdf"]).is_file())


if __name__ == "__main__":
    unittest.main()
