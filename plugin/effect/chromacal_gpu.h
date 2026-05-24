// Shared definitions for the chromacal GPU (Metal) render path. Built only when
// CHROMACAL_GPU is defined. The struct layout MUST match ChromacalParams in
// chromacal_kernel.metal exactly (all scalar float/int, tightly packed).
#pragma once

struct ChromacalGPUParams {
    float luma[4];   // log-poly tone-curve coefficients
    float ccm[9];    // row-major 3x3 CCM
    float gamma;     // SDR pure-power gamma (>0) or <=0 for sRGB
    int   mode;      // 0 SDR, 1 HDR HLG, 2 HDR PQ
    float white;     // HDR graphics-white nits
    int   width;
    int   height;
    int   inRowFloats;  // floats per row of the input buffer  (rowbytes / 4)
    int   outRowFloats; // floats per row of the output buffer (rowbytes / 4)
};

// Metal host (chromacal_gpu_mac.mm). Pointers are id<MTL…> passed as void*.
extern "C" {
void* chromacal_gpu_make_pipeline(void* mtlDevice);   // -> retained holder, or null
void  chromacal_gpu_dispose(void* holder);
int   chromacal_gpu_run(void* holder, void* mtlCommandQueue, void* inBuf,
                        void* outBuf, const struct ChromacalGPUParams* params); // 0 = ok
}
