"""Render one existing PDF page for the editor; never modify the document."""
import sys
from pathlib import Path
import fitz

def render(source,page_index,destination):
    destination=Path(destination)
    if destination.exists():raise FileExistsError(destination)
    with fitz.open(source) as pdf:
        page=pdf[int(page_index)]
        # Bound unusual poster-size documents while retaining 300 DPI on A4.
        scale=min(300/72,(50_000_000/(page.rect.width*page.rect.height))**.5)
        page.get_pixmap(matrix=fitz.Matrix(scale,scale),colorspace=fitz.csGRAY,alpha=False).save(destination)

if __name__=='__main__':render(sys.argv[1],int(sys.argv[2]),sys.argv[3])
