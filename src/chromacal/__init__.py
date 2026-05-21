"""
chromacal — ColorChecker camera calibration.

Detect a ColorChecker, solve for a color profile, apply it.

Usage::

    import chromacal
    import cv2

    image = cv2.imread("colorchecker.jpg")
    patches = chromacal.detect(image)
    patches = chromacal.filter_normal(patches)

    solver = chromacal.Solver()
    solver.solve(patches)

    lut = chromacal.create_lut(solver)
    corrected = chromacal.apply_lut(image, lut)
"""

__version__ = "0.1.0"

from _chromacal import (
    LUT,
    NormalityTestResults,
    PatchStatistics,
    Solver,
    apply_lut,
    create_lut,
    detect,
    filter_normal,
    write_cube,
)

__all__ = [
    "__version__",
    "detect",
    "filter_normal",
    "Solver",
    "create_lut",
    "apply_lut",
    "write_cube",
    "LUT",
    "NormalityTestResults",
    "PatchStatistics",
]
