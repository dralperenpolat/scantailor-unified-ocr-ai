"""Adaptive compression. Attempts always start from the source, never a lossy copy."""
from __future__ import annotations

import hashlib
import io
import json
import math
import os
from pathlib import Path
import shutil
import tempfile
import time

import cv2
import fitz
import numpy as np
from PIL import Image

from .pdf_integrity import file_digest, verify

PIPELINE_VERSION = 'adaptive-compression-060-3'
PROFILES = ('lossless', 'balanced', 'small', 'target', 'custom')


class CompressionCancelled(Exception):
    pass


def publish(temporary, destination):
    """Publish a verified adjacent file, never replacing an existing output."""
    temporary, destination = Path(temporary), Path(destination)
    if os.name == 'nt':
        # Unlike a hard link this also works on exFAT. Windows rename is exclusive.
        os.rename(temporary, destination)
    else:
        os.link(temporary, destination)


def configuration(profile, options=None):
    if profile not in PROFILES:
        raise ValueError('Bilinmeyen sıkıştırma profili.')
    options = dict(options or {})
    allowed = {'dpi', 'jpeg_quality', 'min_dpi', 'min_quality', 'target_mib',
               'color', 'protect_text', 'password', 'recompress_flate'}
    if set(options) - allowed:
        raise ValueError('Bilinmeyen sıkıştırma ayarı: ' + ', '.join(sorted(set(options) - allowed)))
    default_dpi, default_quality = (220, 84) if profile == 'small' else (300, 92)
    conf = {'dpi': default_dpi, 'jpeg_quality': default_quality, 'min_dpi': 150,
            'min_quality': 72, 'target_mib': 25.0, 'color': 'preserve', 'protect_text': True, 'recompress_flate': False,
            **options}
    for key, lower, upper in (('dpi', 72, 2400), ('min_dpi', 72, 2400),
                              ('jpeg_quality', 40, 100), ('min_quality', 40, 100)):
        number = float(conf[key])
        if not math.isfinite(number) or int(number) != number or not lower <= number <= upper:
            raise ValueError(f'Geçersiz sıkıştırma ayarı: {key}')
        conf[key] = int(number)
    target = float(conf['target_mib'])
    if not math.isfinite(target) or not 0 < target <= 1_000_000:
        raise ValueError('Hedef dosya boyutu sıfırdan büyük olmalı.')
    conf['target_mib'] = target
    if conf['color'] not in ('preserve', 'gray') or not all(isinstance(conf[key], bool) for key in ('protect_text', 'recompress_flate')):
        raise ValueError('Geçersiz renk veya küçük yazı koruma ayarı.')
    if profile == 'target' and (conf['min_dpi'] > conf['dpi'] or conf['min_quality'] > conf['jpeg_quality']):
        raise ValueError('Alt kalite sınırı başlangıç ayarından büyük olamaz.')
    if profile == 'lossless' and conf['color'] != 'preserve':
        raise ValueError('Griye dönüşüm kayıpsız sıkıştırma değildir.')
    return conf


def options_signature(profile, options=None):
    conf = configuration(profile, options)
    conf.pop('password', None)
    return hashlib.sha256(json.dumps([PIPELINE_VERSION, profile, conf], sort_keys=True).encode()).hexdigest()


def _analysis(image):
    """Locate likely glyphs to constrain resampling, not to claim OCR accuracy."""
    small = image.convert('L')
    small.thumbnail((1800, 1800), Image.Resampling.LANCZOS)
    gray = np.asarray(small)
    if min(gray.shape) < 15:
        return {'kind': 'photo', 'small_glyph_height': None, 'patches': []}
    mask = cv2.adaptiveThreshold(gray, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
                                 cv2.THRESH_BINARY_INV, 31, 15)
    count, _, stats, _ = cv2.connectedComponentsWithStats(mask, 8)
    glyphs = [tuple(map(int, stat)) for stat in stats[1:count]
              if 4 <= stat[3] <= 70 and 1 <= stat[2] <= stat[3] * 1.7
              and 4 <= stat[4] <= stat[2] * stat[3] * .93]
    is_text = len(glyphs) >= 12 and float(np.mean(gray > 180)) > .45
    scale = image.height / small.height
    glyph_height = float(np.percentile([s[3] for s in glyphs], 20)) * scale if is_text else None
    patches = []
    if is_text:
        # Smallest glyphs plus distributed samples, rather than just one line.
        ordered = sorted(glyphs, key=lambda s: s[3])[:8]
        ordered += glyphs[::max(1, len(glyphs) // 12)][:12]
        for x, y, width, height, _ in ordered:
            cx, cy = (x + width / 2) * scale, (y + height / 2) * scale
            patches.append((max(0, int(cx - 96)), max(0, int(cy - 64)),
                            min(image.width, int(cx + 96)), min(image.height, int(cy + 64))))
    return {'kind': 'text' if is_text else 'photo', 'small_glyph_height': glyph_height, 'patches': patches}


def _image_quality(before, after, analysis):
    # Native-resolution text patches catch local damage hidden by a page average.
    patches = analysis['patches'] or [
        (int(before.width * x), int(before.height * y),
         min(before.width, int(before.width * x) + 256),
         min(before.height, int(before.height * y) + 256))
        for y in (0, .4, .8) for x in (0, .4, .8)]
    max_mean, max_ink_error, max_lost = 0.0, 0.0, 0.0
    for rect in patches:
        size = (rect[2] - rect[0], rect[3] - rect[1])
        if min(size) <= 0:
            continue
        left = np.asarray(before.crop(rect).convert('RGB'), dtype=np.int16)
        scaled = (rect[0] * after.width / before.width, rect[1] * after.height / before.height,
                  rect[2] * after.width / before.width, rect[3] * after.height / before.height)
        right = np.asarray(after.resize(size, Image.Resampling.LANCZOS, box=scaled).convert('RGB'), dtype=np.int16)
        difference = np.abs(left - right).mean(axis=2) / 255
        max_mean = max(max_mean, float(difference.mean()))
        dark = left.mean(axis=2) < 140
        if np.count_nonzero(dark) >= 10:
            max_ink_error = max(max_ink_error, float(difference[dark].mean()))
            max_lost = max(max_lost, float(np.mean(right.mean(axis=2)[dark] > 200)))
    text_ok = analysis['kind'] != 'text' or (max_ink_error <= .13 and max_lost <= .015)
    return {'accepted': max_mean <= .10 and text_ok, 'max_patch_error': max_mean,
            'max_ink_error': max_ink_error, 'max_lost_dark_pixels': max_lost}


def _encode(image, physical_size, conf, checkpoint):
    analysis = _analysis(image)
    width_pt, height_pt = physical_size
    factor = min(1., width_pt * conf['dpi'] / 72 / image.width,
                 height_pt * conf['dpi'] / 72 / image.height)
    quality = conf['jpeg_quality']
    if conf['protect_text'] and analysis['kind'] == 'text':
        factor = max(factor, min(1., 14 / analysis['small_glyph_height']))
        quality = max(quality, 90)
    color_image = image.convert('L') if conf['color'] == 'gray' else image
    # Retry at native resolution before abandoning an image. Both attempts use
    # the source pixels, never the already compressed first attempt.
    attempts = [(factor, quality), (1., max(quality, 94))]
    for scale, jpeg_quality in dict.fromkeys(attempts):
        checkpoint()
        size = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
        candidate = color_image.resize(size, Image.Resampling.LANCZOS) if size != image.size else color_image
        # OpenCV's growable encoder buffer avoids Pillow's fixed optimized-JPEG
        # estimate, which fails for high-entropy 4:4:4 scans at some qualities.
        pixels = np.asarray(candidate)
        if candidate.mode == 'RGB':
            pixels = cv2.cvtColor(pixels, cv2.COLOR_RGB2BGR)
        ok, encoded = cv2.imencode('.jpg', pixels, [cv2.IMWRITE_JPEG_QUALITY, jpeg_quality,
            cv2.IMWRITE_JPEG_OPTIMIZE, 1, cv2.IMWRITE_JPEG_SAMPLING_FACTOR,
            cv2.IMWRITE_JPEG_SAMPLING_FACTOR_444])
        if not ok:
            raise ValueError('JPEG görüntüsü kodlanamadı.')
        payload = encoded.tobytes()
        with Image.open(io.BytesIO(payload)) as decoded:
            # Explicit grayscale conversion is measured against the requested
            # gray source. The result is still labelled lossy.
            metrics = _image_quality(color_image, decoded, analysis)
        if metrics['accepted']:
            return payload, {'kind': analysis['kind'], 'width': size[0], 'height': size[1],
                             'jpeg_quality': jpeg_quality, 'scale': scale, **metrics}
    return None, {'kind': analysis['kind'], 'reason': 'quality_limit', **metrics}


def _check_memory(width, height):
    from .memory_budget import available_bytes
    available = available_bytes()
    required = width * height * 20 + 128 * 1024 ** 2
    if width * height > 150_000_000 or available is not None and required > available * .65:
        raise MemoryError('Bu görüntü için yeterli bellek yok; tamamlanan dosyalar korunuyor.')


def _attempt(source, destination, conf, selected, lossless, notify, checkpoint):
    changed, skipped = [], []
    changed_pages = set()
    with fitz.open(source) as doc:
        if doc.needs_pass and not doc.authenticate(conf.get('password', '')):
            raise ValueError('PDF şifreli; doğru şifre gerekli.')
        images = {}
        if not lossless:
            for index, page in enumerate(doc):
                checkpoint()
                for item in page.get_images(full=True):
                    xref = item[0]
                    row = images.setdefault(xref, {'info': item, 'users': set(), 'sizes': [], 'page': index})
                    row['users'].add(index)
                    # Source-axis lengths handle rotated/skewed placements.
                    for _, matrix in page.get_image_rects(xref, transform=True):
                        row['sizes'].append((math.hypot(matrix.a, matrix.b), math.hypot(matrix.c, matrix.d)))
            for number, (xref, row) in enumerate(images.items(), 1):
                checkpoint()
                xref, mask, width, height, bpc, *_ = row['info']
                if not row['users'] & selected:
                    continue
                reason = None
                if not row['users'] <= selected:
                    reason = 'shared_with_unselected_page'
                elif mask or doc.xref_get_key(xref, 'Mask')[0] != 'null':
                    reason = 'masked_image'
                elif bpc != 8:
                    reason = 'binary_or_special_image'
                elif not row['sizes'] or width * height < 16_384:
                    reason = 'small_or_unused_image'
                elif row['info'][5] not in ('DeviceGray', 'DeviceRGB', 'ICCBased'):
                    reason = 'managed_or_special_color'
                elif doc.xref_get_key(xref, 'Decode')[0] != 'null':
                    reason = 'custom_decode'
                if reason:
                    skipped.append({'pages': sorted(p + 1 for p in row['users']), 'reason': reason})
                    continue
                _check_memory(width, height)
                pix = fitz.Pixmap(doc, xref)
                if pix.alpha or pix.n not in (1, 3):
                    skipped.append({'pages': sorted(p + 1 for p in row['users']), 'reason': 'special_pixels'})
                    continue
                image = Image.frombytes('RGB' if pix.n == 3 else 'L', (width, height), pix.samples)
                del pix
                size = (max(value[0] for value in row['sizes']), max(value[1] for value in row['sizes']))
                payload, detail = _encode(image, size, conf, checkpoint)
                image.close()
                # Compare compressed streams, not an extracted PNG wrapper.
                original_size = len(doc.xref_stream_raw(xref))
                if payload and len(payload) < original_size:
                    retained = {key: doc.xref_get_key(xref, key) for key in
                                ('ColorSpace', 'Interpolate', 'Intent', 'OC')}
                    doc[row['page']].replace_image(xref, stream=payload)
                    # JPEG stores the source channel samples; retain the ICC
                    # interpretation and optional-content membership as well.
                    for key, (kind, value) in retained.items():
                        if kind != 'null' and (key != 'ColorSpace' or conf['color'] == 'preserve'):
                            doc.xref_set_key(xref, key, value)
                    changed_pages.update(row['users'])
                    changed.append({'pages': sorted(p + 1 for p in row['users']), **detail})
                else:
                    skipped.append({'pages': sorted(p + 1 for p in row['users']),
                                    'reason': detail.get('reason', 'already_compact')})
                notify({'stage': 'Sayfaya uygun sıkıştırma hazırlanıyor', 'page': number, 'total': len(images)})
                fitz.TOOLS.store_shrink(100)
        checkpoint()
        doc.save(destination, garbage=4, deflate=True, deflate_images=True,
                 deflate_fonts=True, use_objstms=1, encryption=fitz.PDF_ENCRYPT_KEEP)
    if lossless and conf['recompress_flate']:
        # qpdf can recompress existing Flate streams that MuPDF retains. It
        # does not quantize, downsample or JPEG-encode images with these flags.
        try:
            import pikepdf
        except ImportError:
            skipped.append({'reason': 'qpdf_not_available'})
        else:
            checkpoint()
            notify({'stage': 'Kayıpsız PDF yeniden paketleniyor', 'page': 0, 'total': 0})
            alternate = destination.with_suffix('.qpdf.pdf')
            def repack_progress(percent):
                checkpoint()
                notify({'stage': 'Kayıpsız PDF yeniden paketleniyor', 'page': percent, 'total': 100})
            # Each desktop request owns its worker process. Restore qpdf's
            # default compression level for later work in the queue process.
            pikepdf.settings.set_flate_compression_level(9)
            try:
                with pikepdf.Pdf.open(destination, password=conf.get('password', '')) as repacked:
                    repacked.save(alternate, recompress_flate=True,
                                  object_stream_mode=pikepdf.ObjectStreamMode.generate,
                                  fix_metadata_version=False,
                                  encryption=True if repacked.is_encrypted else None,
                                  progress=repack_progress)
            finally:
                pikepdf.settings.set_flate_compression_level(-1)
            checkpoint()
            if alternate.stat().st_size < destination.stat().st_size:
                alternate.replace(destination)
            else:
                alternate.unlink()
    return changed, changed_pages, skipped


def compress(source, destination, profile='balanced', notify=lambda event: None, *,
             options=None, pages='', cancelled=lambda: False):
    conf = configuration(profile, options)
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination or destination.exists():
        raise FileExistsError('Çıktı için yeni bir dosya adı gerekli.')
    start = time.monotonic()

    def checkpoint():
        if cancelled():
            raise CompressionCancelled('Sıkıştırma iptal edildi; kaynak ve önceki çıktılar korundu.')

    checkpoint()
    source_hash = file_digest(source)
    before_bytes = source.stat().st_size
    from .smart_pdf import page_range
    with fitz.open(source) as original:
        if not original.is_pdf:
            raise ValueError('PDF dosyası gerekli.')
        if original.needs_pass and not original.authenticate(conf.get('password', '')):
            raise ValueError('PDF şifreli; doğru şifre gerekli.')
        if original.get_sigflags() > 0:
            raise ValueError('İmzalı PDF yeniden sıkıştırılamaz; imzasız bir çalışma kopyası kullanın.')
        if not len(original):
            raise ValueError('PDF içinde sayfa yok.')
        count = len(original)
        selected = set(page_range(pages, count))
        for page in original:
            for item in page.get_images(full=True):
                _check_memory(item[2], item[3])
    destination.parent.mkdir(parents=True, exist_ok=True)
    target_bytes = int(conf['target_mib'] * 1024 ** 2) if profile == 'target' else None
    attempts, all_skipped = [], []
    with tempfile.TemporaryDirectory(prefix='.stou-compress-', dir=destination.parent) as folder:
        work = Path(folder)
        plans = [(conf, True)] if profile in ('lossless', 'target') else [(conf, False)]
        if profile == 'target':
            for fraction in (0., .34, .67, 1.):
                plans.append(({**conf,
                    'dpi': round(conf['dpi'] - (conf['dpi'] - conf['min_dpi']) * fraction),
                    'jpeg_quality': round(conf['jpeg_quality'] - (conf['jpeg_quality'] - conf['min_quality']) * fraction)}, False))
        best, best_size, best_details = source, before_bytes, ([], set(), [])
        best_validation = {'method': 'identical_source_copy'}
        for index, (attempt_conf, lossless) in enumerate(plans):
            checkpoint()
            candidate = work / f'attempt-{index}.pdf'
            notify({'stage': 'Sıkıştırma seçeneği deneniyor', 'page': index + 1, 'total': len(plans)})
            details = _attempt(source, candidate, attempt_conf, selected, lossless, notify, checkpoint)
            all_skipped.extend(details[2])
            candidate_size = candidate.stat().st_size
            attempts.append({'dpi': None if lossless else attempt_conf['dpi'],
                             'jpeg_quality': None if lossless else attempt_conf['jpeg_quality'],
                             'bytes': candidate_size, 'images_changed': len(details[0])})
            if candidate_size < best_size:
                # A smaller but invalid candidate cannot replace a verified one.
                validation = verify(source, candidate, details[1], notify, checkpoint,
                                    conf.get('password', ''), conf['color'] == 'gray')
                if best != source:
                    best.unlink()
                best, best_size, best_details = candidate, candidate_size, details
                best_validation = validation
            else:
                candidate.unlink()
            if target_bytes is not None and best_size <= target_bytes:
                break
        checkpoint()
        changed, changed_pages, skipped = best_details
        if best == source:
            skipped = list({json.dumps(item, sort_keys=True): item for item in all_skipped}.values())
        staged = work / 'verified.pdf'
        with best.open('rb') as reader, staged.open('xb') as writer:
            shutil.copyfileobj(reader, writer, length=1024 * 1024)
            writer.flush()
            os.fsync(writer.fileno())
        if file_digest(source) != source_hash:
            raise ValueError('Kaynak PDF işlem sırasında değişti; yeni çıktı yayımlanmadı.')
        output_hash = file_digest(staged)
        checkpoint()
        publish(staged, destination)
    target_met = target_bytes is not None and best_size <= target_bytes
    return {'pdf': str(destination), 'profile': profile, 'pages': count,
            'selected_pages': sorted(p + 1 for p in selected), 'before_bytes': before_bytes,
            'after_bytes': best_size, 'saved_percent': (1 - best_size / before_bytes) * 100,
            'images_changed': len(changed), 'changed_pages': sorted(p + 1 for p in changed_pages),
            'lossless': not changed_pages, 'image_pixels_identical': not changed_pages,
            'renders_identical': not changed_pages, 'text_identical': True,
            'text_positions_identical': True, 'navigation_identical': True,
            'source_sha256': source_hash, 'output_sha256': output_hash,
            'options_signature': options_signature(profile, options),
            'validation': best_validation,
            'options': {k: v for k, v in conf.items() if k != 'password'},
            'target_bytes': target_bytes, 'target_met': target_met if target_bytes is not None else None,
            'attempts': attempts, 'images': changed, 'skipped_images': skipped,
            'seconds': time.monotonic() - start,
            'note': ('Hedef boyut kalite sınırları içinde sağlanamadı; en küçük doğrulanmış sonuç kaydedildi.'
                     if target_bytes is not None and not target_met else
                     'Görüntü değişir; küçük yazılar ve renkler önizlemede kontrol edilmeli.'
                     if changed_pages else 'Görüntü pikselleri ve PDF metni korundu.')}


def preview(source, destination, pages='1-3', notify=lambda event: None, *,
            options=None, cancelled=lambda: False):
    from .smart_pdf import page_range, copy_navigation
    destination = Path(destination)
    if destination.exists():
        raise FileExistsError('Yeni önizleme klasörü seçin.')
    with fitz.open(source) as doc:
        if doc.needs_pass and not doc.authenticate((options or {}).get('password', '')):
            raise ValueError('PDF şifreli; doğru şifre gerekli.')
        indices = page_range(pages, len(doc), clip=True)
        if len(indices) > 20:
            raise ValueError('Önizleme için en fazla 20 temsilî sayfa seçin.')
        destination.mkdir(parents=True)
        sample = destination / 'sample.pdf'
        with fitz.open() as out:
            for index in indices:
                out.insert_pdf(doc, from_page=index, to_page=index)
            copy_navigation(doc, out, indices)
            out.save(sample)
            out[0].get_pixmap(dpi=150, alpha=False).save(destination / 'sample.png')
    reports = []
    for profile in ('lossless', 'balanced', 'small'):
        target = destination / (profile + '.pdf')
        # Compare named presets, not three copies of the custom settings.
        report = compress(sample, target, profile, notify, cancelled=cancelled)
        with fitz.open(target) as doc:
            doc[0].get_pixmap(dpi=150, alpha=False).save(destination / (profile + '.png'))
        report.update(preview=str(destination / (profile + '.png')))
        reports.append(report)
    return {'sample_pages': [i + 1 for i in indices], 'profiles': reports, 'sample': str(sample),
            'preview': str(destination / 'sample.png')}
