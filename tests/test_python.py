"""Python binding smoke + regression tests.

Run with:  python -m pytest tests/test_python.py

Requires the ``_chromacal`` extension to be built
(cmake -DCHROMACAL_BUILD_PYTHON=ON, or the installed wheel).
"""

import numpy as np
import pytest

import chromacal
from chromacal import ChartType, LUT, Solver, apply_lut, create_lut, detect, write_cube


def test_charttype_exported():
    # Regression: ChartType was missing from the package's public imports.
    assert hasattr(chromacal, "ChartType")
    assert "ChartType" in chromacal.__all__
    # It is an enum with the two chart types.
    assert ChartType.Classic24 is not ChartType.SG140


def test_version():
    assert isinstance(chromacal.__version__, str)
    assert chromacal.__version__


def test_public_api_surface():
    # The names a user is expected to import must all exist.
    for name in chromacal.__all__:
        assert hasattr(chromacal, name), f"chromacal.{name} is missing"


def test_detect_rejects_wrong_dtype():
    # Regression: a float image was silently force-cast to uint8 (0/1 bytes)
    # because py::array_t<uint8_t> force-casts by default.
    img = np.zeros((10, 10, 3), dtype=np.float64)
    with pytest.raises(Exception):
        detect(img)


def test_detect_rejects_non_array():
    with pytest.raises(Exception):
        detect("not an image")


def test_detect_rejects_wrong_shape():
    # (H, W, 4) is not a valid (H, W, 3) image.
    img = np.zeros((10, 10, 4), dtype=np.uint8)
    with pytest.raises(Exception):
        detect(img)
