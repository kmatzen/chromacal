# Algorithm

chromacal fits a camera color profile by minimizing the perceptually-weighted error between measured ColorChecker patches and their known reference colors.

## Model

The calibration model has **13 parameters**:

### Log-polynomial tone curve (4 parameters)

For each RGB channel independently:

```
output = exp(p0 + p1·ln(input) + p2·ln(input)² + p3·ln(input)³)
```

This operates in log-space, which is smooth and well-suited for modeling both gamma curves and camera-specific non-linearities. The identity curve is `p0=0, p1=1, p2=0, p3=0`. This is the same functional form used in Adobe's DNG color rendering.

### Color correction matrix (9 parameters)

A 3×3 linear transform applied after the tone curve:

```
[R']   [m00 m01 m02] [R_adjusted]
[G'] = [m10 m11 m12] [G_adjusted]
[B']   [m20 m21 m22] [B_adjusted]
```

Diagonal elements correct per-channel gain (white balance). Off-diagonal elements correct cross-channel crosstalk from the sensor's color filter array.

Bounds: diagonal ∈ [0.1, 3.0], off-diagonal ∈ [-1.5, 1.5].

## Cost function

For each ColorChecker patch, the cost function:

1. Applies the tone curve to the measured mean RGB
2. Applies the 3×3 color matrix
3. Normalizes by the exposure ratio (captured / reference)
4. Converts the reference Lab color (D50) to linear RGB via Lab → XYZ → Bradford D50→D65 → sRGB
5. Computes the RGB difference between predicted and reference
6. Weights by **Mahalanobis distance** (using the patch's pixel covariance matrix Σ^(-1/2))
7. Weights by **perceptual importance**: `(1 + chroma/100) / (luminance + 0.01)` — darker and more saturated colors get higher weight because cameras struggle with them most

## White balance constraint

Neutral patches (ColorChecker patches 18–23, the grayscale ramp) get an auxiliary constraint encouraging R ≈ G ≈ B after correction. This is weighted at 10% of the main cost to preserve white balance without over-constraining color accuracy.

## Robust estimation

The optimizer uses **Huber loss** (parameter 3.0) to reduce the influence of outliers — patches that are saturated, partially occluded, or have strong color cast from ambient light.

## Normality filtering

Before optimization, each patch is tested for multivariate normality:

- **Shapiro-Wilk** (per RGB channel): Tests if each channel's pixel distribution is Gaussian
- **Mardia's skewness + kurtosis**: Tests multivariate skewness and kurtosis
- **Henze-Zirkler**: An omnibus test for multivariate normality

A patch passes if **any** test category passes (lenient for real-world noisy data). Patches that fail — due to specular reflections, shading gradients, or motion blur — are removed before optimization.

## Color space details

- **Input**: Gamma-encoded RGB (BT.709 convention, as read from most cameras)
- **Reference colors**: CIE Lab under D50 illuminant (standard ColorChecker measurement conditions)
- **Internal conversion**: Linear sRGB → XYZ D65 → Bradford adaptation → XYZ D50 → Lab D50
- **Output**: Linear RGB (D65 white point, sRGB primaries)

The Bradford chromatic adaptation matrix bridges the gap between D50 (5000K calibration lighting where ColorChecker references are measured) and D65 (sRGB standard display white point).

## Example fit (GoPro Hero10)

Running chromacal on a GoPro Hero10 video frame with a ColorChecker in scene:

**Tone curve:** `p0=1.377, p1=3.479, p2=0.739, p3=0.072`

These are far from the identity (`0, 1, 0, 0`), reflecting the GoPro's aggressive built-in color processing. The large `p1` (slope in log-space) indicates a steep gamma-like curve.

**Color correction matrix:**
```
[[ 1.586  -0.638  -0.214]
 [-0.303   1.826  -0.294]
 [-0.014  -0.449   3.000]]
```

The blue channel diagonal hitting the upper bound (3.0) and the large off-diagonal entries show the GoPro sensor's color filter array produces significant cross-channel crosstalk, especially in blue. The negative off-diagonal values compensate for this by subtracting the leaked signal from neighboring channels.

**Optimization:** Converged in 99 iterations, reducing cost from 4.14e+04 to 3.58e+03 (91% reduction). The residual cost reflects the Huber-robust, Mahalanobis-weighted error across all 24 patches.

## Solver configuration

- **Algorithm**: Ceres Solver, Dense QR linear solver
- **Iterations**: Up to 100
- **Convergence**: function tolerance 1e-8, gradient tolerance 1e-10
- **Initialization**: Identity tone curve + identity CCM

## 3D LUT application

After solving, the calibration can be baked into an OpenColorIO 3D lookup table (default 129³ grid points). This precomputes the tone curve + CCM for every possible input RGB value, enabling fast per-pixel correction via trilinear interpolation.
