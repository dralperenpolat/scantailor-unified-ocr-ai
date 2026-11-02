"""Native-resolution thresholding, with protection or explicit review warnings."""
import io

import numpy as np
from PIL import Image
from .pdf_bitonal_review import reasons


def encode(image, threshold, analysis, quality, checkpoint, *, guarded=True):
    checkpoint()
    if guarded:
        reason = next(reasons(image, threshold, analysis, checkpoint), None)
        if reason:
            return None, {'reason': reason}
        review = []
    else:
        review = list(reasons(image, threshold, analysis, checkpoint))
    if image.mode != 'L':
        image = image.convert('L')
    gray = np.asarray(image)
    binary = bool(np.all((gray == 0) | (gray == 255)))
    candidate = Image.fromarray(gray >= threshold)
    metrics = quality(image, candidate, analysis)
    # Thresholding intentionally changes gray edge brightness. JPEG's dark
    # pixel intensity-error limit is not a measure of missing binary strokes.
    # Retain the local patch limit and reject actual disappearance of dark ink.
    metrics['accepted'] = not guarded or (metrics['max_patch_error'] <= .10 and metrics['max_lost_dark_pixels'] <= .015)
    if not guarded and (metrics['max_patch_error'] > .10 or metrics['max_lost_dark_pixels'] > .015):
        review.append('quality_limit')
    if not metrics['accepted']:
        candidate.close()
        return None, {'reason': 'quality_limit', **metrics}
    stream = io.BytesIO()
    candidate.save(stream, format='PNG', optimize=True)
    candidate.close()
    return stream.getvalue(), {
        'kind': 'text', 'width': image.width, 'height': image.height,
        'bits_per_component': 1, 'threshold': threshold, 'scale': 1.,
        'pixels_changed': not binary, 'gray_protection': guarded,
        'review_reasons': review, **metrics,
    }
