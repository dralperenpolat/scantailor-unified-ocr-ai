"""Long Windows paths for MuPDF's native file API."""
import os
from pathlib import Path


def native_path(value, force=False):
    value = str(Path(value).resolve())
    if os.name == 'nt' and (force or len(value) >= 240) and not value.startswith('\\\\?\\'):
        return '\\\\?\\UNC\\' + value[2:] if value.startswith('\\\\') else '\\\\?\\' + value
    return value


def display_path(value):
    value=str(value)
    if value.startswith('\\\\?\\UNC\\'):
        return '\\\\'+value[8:]
    return value[4:] if value.startswith('\\\\?\\') else value
