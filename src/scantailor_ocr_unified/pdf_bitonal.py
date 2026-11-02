"""Native-resolution text-only thresholding; never advertised as lossless.

The threshold matches the user's Antigravity example. Photos, colour, broad
gray areas and faint isolated marks are retained instead of forcing a ratio.
"""
import io

import cv2
import numpy as np
from PIL import Image


def encode(image, threshold, analysis, quality, checkpoint, *, guarded=True):
    checkpoint()
    if image.mode != 'L' and guarded:
        return None, {'reason': 'color_or_managed_image'}
    if image.mode != 'L':
        image = image.convert('L')
    gray = np.asarray(image)
    binary = bool(np.all((gray == 0) | (gray == 255)))
    if not binary and guarded:
        if analysis['kind'] != 'text':
            return None, {'reason': 'photo_or_unclassified'}
        # Gray strokes with no dark core could disappear at the chosen
        # threshold. Check connected components at native resolution.
        faint = ((gray >= threshold) & (gray < 235)).astype('uint8')
        core = (gray < threshold).astype('uint8')
        near_core = cv2.dilate(core, np.ones((5, 5), np.uint8))
        isolated = faint & (1 - near_core)
        count, _, stats, _ = cv2.connectedComponentsWithStats(isolated, 8)
        marks = [s for s in stats[1:count] if s[4] >= 12 and s[2] >= 2 and s[3] >= 6]
        # A connected gray word or a row of several gray glyphs matters;
        # isolated scanner specks should not prevent compression of a page.
        faint_text = any(s[2] >= s[3] * 2 and s[4] >= 40 for s in marks)
        for s in marks:
            neighbours = [t for t in marks if abs((s[1] + s[3]/2) - (t[1] + t[3]/2)) <= max(s[3], t[3]) * .4
                          and abs(s[0] - t[0]) <= max(s[3], t[3]) * 6]
            if len(neighbours) >= 3:
                faint_text = True
                break
        if faint_text:
            return None, {'reason': 'faint_marks'}
        # Continuous gray interiors distinguish illustrations / gray shading
        # from the narrow antialiased edges of printed text.
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
            return None, {'reason': 'gray_illustration_or_shading'}
    candidate = Image.fromarray(gray >= threshold)
    metrics = quality(image, candidate, analysis)
    # Thresholding intentionally changes gray edge brightness. JPEG's dark
    # pixel intensity-error limit is not a measure of missing binary strokes.
    # Retain the local patch limit and reject actual disappearance of dark ink.
    metrics['accepted'] = not guarded or (metrics['max_patch_error'] <= .10 and metrics['max_lost_dark_pixels'] <= .015)
    if not metrics['accepted']:
        candidate.close()
        return None, {'reason': 'quality_limit', **metrics}
    stream = io.BytesIO()
    candidate.save(stream, format='PNG', optimize=True)
    candidate.close()
    return stream.getvalue(), {
        'kind': 'text', 'width': image.width, 'height': image.height,
        'bits_per_component': 1, 'threshold': threshold, 'scale': 1.,
        'pixels_changed': not binary, 'gray_protection': guarded, **metrics,
    }
