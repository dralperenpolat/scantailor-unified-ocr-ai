"""Optional display-only tonal profiles; recognition keeps its original gray copy.

No geometric resampling, sharpening or generated detail. Local statistics are
evaluated in overlapping strips to avoid several full-page float64 buffers.
"""
import cv2
import numpy as np

LOCAL_SOFT_VERSION = 'local-soft-sauvola-1'


def local_soft_monochrome(gray):
    gray = np.asarray(gray)
    if gray.dtype != np.uint8 or gray.ndim != 2:
        raise ValueError('Expected an 8-bit grayscale page')
    if not gray.size or int(gray.max()) - int(gray.min()) < 2:
        return gray.copy()
    height, width = gray.shape
    radius = max(15, min(150, int(min(width, height) * .03 + .5)))
    window = 2 * radius + 1
    result = np.empty_like(gray)
    block = max(16, min(256, 4_000_000 // width - 2 * radius))
    for start in range(0, height, block):
        end = min(height, start + block)
        top, bottom = max(0, start - radius), min(height, end + radius)
        values = gray[top:bottom].astype(np.float64)
        mean = cv2.boxFilter(values, -1, (window, window), borderType=cv2.BORDER_REFLECT)
        variance = cv2.boxFilter(values * values, -1, (window, window), borderType=cv2.BORDER_REFLECT) - mean * mean
        threshold = mean * (1 + .18 * (np.sqrt(np.maximum(0, variance)) / 128 - 1))
        rendered = np.clip(127.5 + (values - threshold) * 5.5, 0, 255)
        # Local statistics alone would turn the inside of a large, solid black
        # mark gray. Keep genuinely dark source pixels dark.
        rendered = np.where(values < 32, np.minimum(rendered, values), rendered)
        result[start:end] = rendered[start - top:end - top].astype(np.uint8)
    return result
