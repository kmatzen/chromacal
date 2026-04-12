# ColorChecker Reference Values

chromacal uses the **X-Rite ColorChecker Classic (24-patch)** reference values measured under D50 illumination.

## Patch layout

```
 ┌────┬────┬────┬────┬────┬────┐
 │  0 │  1 │  2 │  3 │  4 │  5 │  Row 1: Saturated colors
 ├────┼────┼────┼────┼────┼────┤
 │  6 │  7 │  8 │  9 │ 10 │ 11 │  Row 2: Saturated colors
 ├────┼────┼────┼────┼────┼────┤
 │ 12 │ 13 │ 14 │ 15 │ 16 │ 17 │  Row 3: Saturated colors
 ├────┼────┼────┼────┼────┼────┤
 │ 18 │ 19 │ 20 │ 21 │ 22 │ 23 │  Row 4: Grayscale ramp
 └────┴────┴────┴────┴────┴────┘
```

## Reference Lab values (D50)

| Patch | Name | L* | a* | b* |
|-------|------|----|----|----|
| 0 | Dark skin | 37.986 | 13.555 | 14.059 |
| 1 | Light skin | 65.711 | 18.130 | 17.810 |
| 2 | Blue sky | 49.927 | -4.880 | -21.925 |
| 3 | Foliage | 43.139 | -13.095 | 21.905 |
| 4 | Blue flower | 55.112 | 8.844 | -25.399 |
| 5 | Bluish green | 70.719 | -33.397 | -0.199 |
| 6 | Orange | 62.661 | 36.067 | 57.096 |
| 7 | Purplish blue | 40.020 | 10.410 | -45.964 |
| 8 | Moderate red | 51.124 | 48.239 | 16.248 |
| 9 | Purple | 30.325 | 22.976 | -21.587 |
| 10 | Yellow green | 72.532 | -23.709 | 57.255 |
| 11 | Orange yellow | 71.941 | 19.363 | 67.857 |
| 12 | Blue | 28.778 | 14.179 | -50.297 |
| 13 | Green | 55.261 | -38.342 | 31.370 |
| 14 | Red | 42.101 | 53.378 | 28.190 |
| 15 | Yellow | 81.733 | 4.039 | 79.819 |
| 16 | Magenta | 51.935 | 49.986 | -14.574 |
| 17 | Cyan | 51.038 | -28.631 | -28.638 |
| 18 | White | 96.539 | -0.425 | 1.186 |
| 19 | Neutral 8 | 81.257 | -0.638 | -0.335 |
| 20 | Neutral 6.5 | 66.766 | -0.734 | -0.504 |
| 21 | Neutral 5 | 50.867 | -0.153 | -0.270 |
| 22 | Neutral 3.5 | 35.656 | -0.421 | -1.231 |
| 23 | Black | 20.461 | -0.079 | -0.973 |

## Notes

- Patches 18–23 are the grayscale ramp, used for the white balance constraint
- Reference values are for the **2014 revision** of the ColorChecker Classic
- All values measured under CIE D50 illumination (5000K)
- The solver converts these to linear RGB (D65) internally using Bradford chromatic adaptation
