import unittest
import numpy as np
import torch
import torch.nn.functional as F
from scantailor_ocr_unified.mesh_geometry import sample_mesh, remap_mesh
from scantailor_ocr_unified.engine import sample_original


def identity_mesh(h=45,w=31):
    x,y=np.meshgrid(np.linspace(-1,1,w,dtype=np.float32),np.linspace(-1,1,h,dtype=np.float32))
    return np.stack([x,y],axis=-1)


class MeshGeometryTest(unittest.TestCase):
    def test_original_pixels_survive_identity_including_borders(self):
        rng=np.random.default_rng(10)
        source=rng.integers(0,256,(419,293,3),dtype=np.uint8)
        np.testing.assert_array_equal(remap_mesh(source,identity_mesh()),source)

    def test_grid_sampling_matches_official_align_corners_convention(self):
        grid=identity_mesh(17,13)
        grid[:,:,1]+=.08*np.sin(grid[:,:,0]*3)
        actual=sample_mesh(grid,np.arange(293,dtype=np.float32)[None,:],np.arange(419,dtype=np.float32)[:,None],(293,419))
        expected=F.interpolate(torch.from_numpy(grid.transpose(2,0,1))[None],size=(419,293),mode='bilinear',align_corners=True)[0].numpy().transpose(1,2,0)
        np.testing.assert_allclose(actual,expected,atol=5e-7,rtol=0)

    def test_invalid_mesh_is_rejected(self):
        bad=identity_mesh();bad[3,4,0]=np.nan
        with self.assertRaises(ValueError):remap_mesh(np.zeros((20,30),np.uint8),bad)

    def test_final_render_uses_the_same_mapping_as_preview(self):
        import tempfile
        from pathlib import Path
        source=np.arange(120*90,dtype=np.uint8).reshape(120,90)
        with tempfile.TemporaryDirectory() as d:
            grid=Path(d)/'grid.npy';np.save(grid,identity_mesh())
            result={'sampling':{'base_size':[90,120],'crop_box':[0,0,90,120],'kind':'dewarp','grid':str(grid),'rotation':0,'angle':0}}
            actual=sample_original(source,{},result,[0,0,90,120],(90,120))
            np.testing.assert_array_equal(actual,source)


if __name__=='__main__':unittest.main()
