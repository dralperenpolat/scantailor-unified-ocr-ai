"""Sample a predicted document mesh using UVDoc's align_corners convention.

The model predicts coordinates only. All output intensities come from the
original photograph. Process strips to bound memory at high output resolution.
"""
from __future__ import annotations
import cv2
import numpy as np

MAPPING_VERSION = 'uvdoc-align-corners-v1'


def sample_mesh(grid, x, y, output_size):
    grid = np.asarray(grid, dtype=np.float32)
    if grid.ndim != 3 or grid.shape[2] != 2 or min(grid.shape[:2]) < 2 or not np.isfinite(grid).all():
        raise ValueError('Geometri ağı geçersiz veya sonlu olmayan koordinat içeriyor.')
    width, height = output_size
    if min(width, height) < 2:
        raise ValueError('Dönüşüm boyutu en az 2 × 2 olmalı.')
    gx = np.clip(np.asarray(x, np.float32) * ((grid.shape[1]-1)/(width-1)), 0, grid.shape[1]-1)
    gy = np.clip(np.asarray(y, np.float32) * ((grid.shape[0]-1)/(height-1)), 0, grid.shape[0]-1)
    ix, iy = gx.astype(np.int32), gy.astype(np.int32)
    jx, jy = np.minimum(ix+1, grid.shape[1]-1), np.minimum(iy+1, grid.shape[0]-1)
    wx, wy = (gx-ix)[..., None], (gy-iy)[..., None]
    upper = grid[iy, ix]*(1-wx) + grid[iy, jx]*wx
    lower = grid[jy, ix]*(1-wx) + grid[jy, jx]*wx
    return (upper*(1-wy) + lower*wy).astype(np.float32)


def remap_mesh(source, grid, output_size=None, interpolation=cv2.INTER_LINEAR):
    source = np.asarray(source)
    sh, sw = source.shape[:2]
    width, height = output_size or (sw, sh)
    if min(width, height) < 2 or width*height > 80_000_000:
        raise ValueError('Geometri çıktı boyutu desteklenen sınırın dışında.')
    result = np.empty((height, width) + source.shape[2:], dtype=source.dtype)
    xx = np.arange(width, dtype=np.float32)[None, :]
    for row in range(0, height, 256):
        yy = np.arange(row, min(row+256, height), dtype=np.float32)[:, None]
        uv = sample_mesh(grid, xx, yy, (width, height))
        mx = ((uv[..., 0]+1)*(sw-1)/2).astype(np.float32)
        my = ((uv[..., 1]+1)*(sh-1)/2).astype(np.float32)
        result[row:row+len(yy)] = cv2.remap(source, mx, my, interpolation,
            borderMode=cv2.BORDER_CONSTANT, borderValue=(255, 255, 255, 255))
    return result
