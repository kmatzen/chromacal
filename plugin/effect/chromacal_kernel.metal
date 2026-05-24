// chromacal GPU compute kernel (Metal) — per-pixel Apply, mirroring the CPU path
// in chromacal_effect.cpp: encode( CCM * toneCurve(input) ) (+ Rec.2020/PQ/HLG
// for HDR). Operates on PF GPU worlds, which are linear BGRA-float buffers
// (rowFloats = rowbytes/4), matching ChromacalGPUParams in chromacal_gpu.h.
//
// Compiles standalone: xcrun metal -c chromacal_kernel.metal && xcrun metallib …

#include <metal_stdlib>
using namespace metal;

struct ChromacalParams {
    float luma[4];
    float ccm[9];
    float gamma;
    int   mode;   // 0 SDR, 1 HLG, 2 PQ
    float white;
    int   width;
    int   height;
    int   inRowFloats;
    int   outRowFloats;
};

inline float cc_tone(float c, constant float* p) {
    if (!(c > 0.0f)) c = 0.0f;
    float l = log(c + 1e-6f);
    float v = exp(p[0] + p[1] * l + p[2] * l * l + p[3] * l * l * l);
    return (v == v && v < 1e12f) ? v : 0.0f;
}
inline float cc_srgb(float c) {
    if (!(c > 0.0f)) return 0.0f;
    return c <= 0.0031308f ? 12.92f * c : 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
}
inline float cc_pq(float c, float white) {
    if (c <= 0.0f) return 0.0f;
    float Y = min(c * white / 10000.0f, 1.0f);
    const float m1 = 0.1593017578125f, m2 = 78.84375f;
    const float c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
    float ym1 = pow(Y, m1);
    return pow((c1 + c2 * ym1) / (1.0f + c3 * ym1), m2);
}
inline float cc_hlg(float c) {
    if (c <= 0.0f) return 0.0f;
    c = min(c, 1.0f);
    const float a = 0.17883277f, b = 0.28466892f, cc = 0.55991073f;
    return c <= 1.0f / 12.0f ? sqrt(3.0f * c) : a * log(12.0f * c - b) + cc;
}
inline float cc_encode(float c, constant ChromacalParams& prm) {
    if (!(c > 0.0f)) return 0.0f;
    float o;
    if (prm.mode == 2)      o = cc_pq(c, prm.white);
    else if (prm.mode == 1) o = cc_hlg(c);
    else                    o = prm.gamma > 0.0f ? pow(c, 1.0f / prm.gamma) : cc_srgb(c);
    return (o == o) ? o : 0.0f;
}

kernel void chromacal_apply(device const float*       inBuf  [[buffer(0)]],
                            device float*             outBuf [[buffer(1)]],
                            constant ChromacalParams& prm    [[buffer(2)]],
                            uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= uint(prm.width) || gid.y >= uint(prm.height)) return;
    int inIdx = int(gid.y) * prm.inRowFloats + int(gid.x) * 4;
    int outIdx = int(gid.y) * prm.outRowFloats + int(gid.x) * 4;
    float B = inBuf[inIdx + 0], G = inBuf[inIdx + 1], R = inBuf[inIdx + 2], A = inBuf[inIdx + 3];
    // Premiere's GPU working space is LINEAR. The calibration was fit on
    // gamma-encoded (working-space) pixels, so re-encode the linear input to that
    // gamma before the tone curve, and output LINEAR — Premiere re-encodes for
    // the display. (The CPU path needs neither, since its buffers are encoded.)
    float Re = cc_encode(R, prm), Ge = cc_encode(G, prm), Be = cc_encode(B, prm);
    float aR = cc_tone(Re, prm.luma);
    float aG = cc_tone(Ge, prm.luma);
    float aB = cc_tone(Be, prm.luma);
    constant float* m = prm.ccm;
    float lR = m[0] * aR + m[1] * aG + m[2] * aB;
    float lG = m[3] * aR + m[4] * aG + m[5] * aB;
    float lB = m[6] * aR + m[7] * aG + m[8] * aB;
    if (prm.mode != 0) { // Rec.709 -> Rec.2020 for HDR
        float r2 = 0.6274f * lR + 0.3293f * lG + 0.0433f * lB;
        float g2 = 0.0691f * lR + 0.9195f * lG + 0.0114f * lB;
        float b2 = 0.0164f * lR + 0.0880f * lG + 0.8956f * lB;
        lR = r2; lG = g2; lB = b2;
    }
    outBuf[outIdx + 0] = max(lB, 0.0f); // B (linear; Premiere encodes for display)
    outBuf[outIdx + 1] = max(lG, 0.0f); // G
    outBuf[outIdx + 2] = max(lR, 0.0f); // R
    outBuf[outIdx + 3] = A;
}
