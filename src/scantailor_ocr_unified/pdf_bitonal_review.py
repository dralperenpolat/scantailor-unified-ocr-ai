"""Warnings for irreversible monochrome conversion, never a claim of OCR accuracy."""
import cv2
import numpy as np

REVIEW_VERSION = 'mono-review-1'


def reasons(image, threshold, analysis, checkpoint=lambda: None):
    """Yield the same guards used by mono_text, in their original priority order."""
    checkpoint()
    if image.mode != 'L':
        yield 'color_or_managed_image'
    gray = np.asarray(image.convert('L') if image.mode != 'L' else image)
    if bool(np.all((gray == 0) | (gray == 255))):
        return
    if analysis['kind'] != 'text':
        yield 'photo_or_unclassified'
    faint = ((gray >= threshold) & (gray < 235)).astype('uint8')
    core = (gray < threshold).astype('uint8')
    near_core = cv2.dilate(core, np.ones((5, 5), np.uint8))
    isolated = faint & (1 - near_core)
    count, _, stats, _ = cv2.connectedComponentsWithStats(isolated, 8)
    marks = [s for s in stats[1:count] if s[4] >= 12 and s[2] >= 2 and s[3] >= 6]
    faint_text = any(s[2] >= s[3] * 2 and s[4] >= 40 for s in marks)
    for s in marks:
        neighbours = [t for t in marks if abs((s[1] + s[3]/2) - (t[1] + t[3]/2)) <= max(s[3], t[3]) * .4
                      and abs(s[0] - t[0]) <= max(s[3], t[3]) * 6]
        if len(neighbours) >= 3:
            faint_text = True
            break
    if faint_text:
        yield 'faint_marks'
    checkpoint()
    middle = ((gray > 40) & (gray < 230)).astype('uint8')
    interior = cv2.erode(middle, np.ones((3, 3), np.uint8))
    _, _, regions, _ = cv2.connectedComponentsWithStats(interior, 8)
    broad_gray = any(s[4] >= 256 and min(s[2], s[3]) >= 20 for s in regions[1:])
    if not broad_gray:
        for y in range(0, gray.shape[0], 128):
            for x in range(0, gray.shape[1], 128):
                tile = interior[y:y+128, x:x+128]
                if tile.size >= 4096 and float(tile.mean()) > .20:
                    broad_gray = True
                    break
            if broad_gray:
                break
    if broad_gray:
        yield 'gray_illustration_or_shading'


def page_warnings(images):
    pages = {}
    for item in images:
        for page in item['pages']:
            pages.setdefault(page, set()).update(item.get('review_reasons', []))
    return [{'page': page, 'reasons': sorted(flags)} for page, flags in sorted(pages.items()) if flags]
