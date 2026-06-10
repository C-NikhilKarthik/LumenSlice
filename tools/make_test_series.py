#!/usr/bin/env python3
"""Generate a small synthetic CT series for LumenSlice ingestion testing.

Writes an uncompressed (Explicit VR Little Endian) axial CT series of a sphere
phantom: a bony shell (~700 HU) around a soft-tissue interior (~40 HU) in air
(-1000 HU). Geometry tags (Image Position/Orientation, Pixel Spacing) are filled
so the loader's geometric Z-sort has real data to work with.

Usage: python3 tools/make_test_series.py [out_dir] [n_slices] [size]
"""
import os
import sys

import numpy as np
import pydicom
from pydicom.dataset import Dataset, FileMetaDataset
from pydicom.uid import ExplicitVRLittleEndian, CTImageStorage, generate_uid


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "testdata/phantom"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 48
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 128
    os.makedirs(out_dir, exist_ok=True)

    spacing = 1.5  # in-plane mm
    thickness = 2.0  # mm between slices
    slope, intercept = 1.0, -1024.0  # stored = HU - intercept

    series_uid = generate_uid()
    study_uid = generate_uid()

    cx = cy = size / 2.0
    cz = n / 2.0
    r_outer = 0.40 * size
    r_inner = 0.34 * size

    yy, xx = np.meshgrid(np.arange(size), np.arange(size), indexing="ij")

    # Shuffle file order on disk to prove the geometric sort (not filename order).
    order = list(range(n))
    rng = np.random.default_rng(0)
    rng.shuffle(order)

    for out_idx, z in enumerate(order):
        dz = (z - cz) * (thickness / spacing)
        rr = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2 + dz * dz)
        hu = np.full((size, size), -1000.0, dtype=np.float32)  # air
        hu[rr <= r_outer] = 700.0   # bone shell
        hu[rr <= r_inner] = 40.0    # soft tissue
        stored = np.clip(hu - intercept, 0, 4095).astype(np.uint16)

        fm = FileMetaDataset()
        fm.MediaStorageSOPClassUID = CTImageStorage
        fm.MediaStorageSOPInstanceUID = generate_uid()
        fm.TransferSyntaxUID = ExplicitVRLittleEndian

        ds = Dataset()
        ds.file_meta = fm
        ds.SOPClassUID = CTImageStorage
        ds.SOPInstanceUID = fm.MediaStorageSOPInstanceUID
        ds.StudyInstanceUID = study_uid
        ds.SeriesInstanceUID = series_uid
        ds.Modality = "CT"
        ds.PatientName = "PHANTOM^SPHERE"
        ds.PatientID = "LUMEN-TEST"
        ds.InstanceNumber = z + 1

        ds.Rows = size
        ds.Columns = size
        ds.PixelSpacing = [spacing, spacing]
        ds.SliceThickness = thickness
        ds.ImageOrientationPatient = [1, 0, 0, 0, 1, 0]
        ds.ImagePositionPatient = [
            -cx * spacing, -cy * spacing, z * thickness
        ]
        ds.SamplesPerPixel = 1
        ds.PhotometricInterpretation = "MONOCHROME2"
        ds.BitsAllocated = 16
        ds.BitsStored = 12
        ds.HighBit = 11
        ds.PixelRepresentation = 0
        ds.RescaleSlope = slope
        ds.RescaleIntercept = intercept
        ds.PixelData = stored.tobytes()

        ds.is_little_endian = True
        ds.is_implicit_VR = False
        # Deliberately non-geometric filenames to exercise the sort.
        ds.save_as(os.path.join(out_dir, f"slice_{out_idx:03d}.dcm"), write_like_original=False)

    print(f"Wrote {n} slices ({size}x{size}) to {out_dir}")


if __name__ == "__main__":
    main()
