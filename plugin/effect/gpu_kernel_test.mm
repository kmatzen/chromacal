// Headless verification that chromacal_kernel.metal computes the specified math on
// a real GPU (run on this machine's Metal device). It does NOT exercise Premiere's
// SmartRender plumbing (that needs Premiere) — it confirms the *kernel* itself:
// indexing, the per-pixel tone+CCM+encode, and the BGRA layout, vs a CPU reference.
//
// Build + run (macOS):
//   xcrun metal -c plugin/effect/chromacal_kernel.metal -o /tmp/k.air
//   xcrun metallib /tmp/k.air -o /tmp/chromacal.metallib
//   clang++ -std=c++17 -ObjC++ plugin/effect/gpu_kernel_test.mm \
//     -I plugin/effect -framework Metal -framework Foundation -o /tmp/gpu_kernel_test
//   /tmp/gpu_kernel_test /tmp/chromacal.metallib
// Exit 0 = kernel matches the CPU reference.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "chromacal_gpu.h"

#include <cmath>
#include <cstdio>
#include <vector>

// CPU reference mirroring chromacal_kernel.metal (mode 0 / SDR), in FLOAT to match
// the GPU's 32-bit math: encode(c)=pow(c,1/gamma); tone=exp(poly(log(c+1e-6)));
// CCM; linear output. (Using double here would diverge from the float kernel.)
static float enc(float c, float g) { return c > 0 ? std::pow(c, 1.0f / g) : 0.0f; }
static float tone(float c, const float* p) {
    if (!(c > 0)) c = 0;
    float l = std::log(c + 1e-6f);
    float v = std::exp(p[0] + p[1] * l + p[2] * l * l + p[3] * l * l * l);
    return (v == v && v < 1e12f) ? v : 0.0f;
}

int main(int argc, char** argv) {
    const char* metallib = argc > 1 ? argv[1] : "/tmp/chromacal.metallib";
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) { printf("no Metal device\n"); return 1; }
        NSError* err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithURL:[NSURL fileURLWithPath:@(metallib)] error:&err];
        if (!lib) { printf("load metallib failed: %s\n", err.localizedDescription.UTF8String); return 1; }
        id<MTLComputePipelineState> pso =
            [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"chromacal_apply"] error:&err];
        if (!pso) { printf("pipeline failed\n"); return 1; }

        // A spread of test pixels (BGRA float), incl. extremes + mid tones.
        std::vector<float> in = {
            0.10f,0.20f,0.30f,1.0f,  0.80f,0.50f,0.20f,1.0f,  0.05f,0.95f,0.45f,1.0f,
            0.00f,0.00f,0.00f,1.0f,  1.00f,1.00f,1.00f,1.0f,  0.62f,0.34f,0.77f,0.5f,
        };
        int n = (int)in.size() / 4;

        ChromacalGPUParams p{};
        // Realistic, bounded coefficients (a real calibration; positive cubic so the
        // log-poly tone doesn't explode for dark pixels).
        const float luma[4] = {1.5f, 3.6f, 0.8f, 0.08f};
        const float ccm[9]  = {1.50f,-0.40f,-0.10f, -0.20f,1.40f,-0.20f, 0.02f,-0.30f,1.28f};
        for (int i=0;i<4;++i) p.luma[i]=luma[i];
        for (int i=0;i<9;++i) p.ccm[i]=ccm[i];
        p.gamma = 2.4f; p.mode = 0; p.white = 100.f;
        p.width = n; p.height = 1; p.inRowFloats = n*4; p.outRowFloats = n*4;

        id<MTLBuffer> ib = [dev newBufferWithBytes:in.data() length:in.size()*4 options:MTLResourceStorageModeShared];
        id<MTLBuffer> ob = [dev newBufferWithLength:in.size()*4 options:MTLResourceStorageModeShared];
        id<MTLBuffer> pb = [dev newBufferWithBytes:&p length:sizeof(p) options:MTLResourceStorageModeShared];
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
        [e setComputePipelineState:pso];
        [e setBuffer:ib offset:0 atIndex:0]; [e setBuffer:ob offset:0 atIndex:1]; [e setBuffer:pb offset:0 atIndex:2];
        [e dispatchThreadgroups:MTLSizeMake((n+15)/16,1,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];
        [e endEncoding]; [cb commit]; [cb waitUntilCompleted];
        if (cb.error) { printf("dispatch error\n"); return 1; }

        const float* gpu = (const float*)ob.contents;
        double maxrel = 0;
        for (int i = 0; i < n; ++i) {
            float B=in[i*4+0], G=in[i*4+1], R=in[i*4+2], A=in[i*4+3];
            float aR=tone(enc(R,2.4f),luma), aG=tone(enc(G,2.4f),luma), aB=tone(enc(B,2.4f),luma);
            float lR=ccm[0]*aR+ccm[1]*aG+ccm[2]*aB, lG=ccm[3]*aR+ccm[4]*aG+ccm[5]*aB, lB=ccm[6]*aR+ccm[7]*aG+ccm[8]*aB;
            float ref[4]={lB<0?0:lB, lG<0?0:lG, lR<0?0:lR, A};
            for (int c=0;c<4;++c) {
                double rel = std::abs(gpu[i*4+c]-ref[c]) / (std::abs(ref[c]) + 1.0);
                maxrel = std::max(maxrel, rel);
            }
        }
        // Relative tolerance: GPU and CPU use different float exp/pow, so allow a
        // small relative gap (this catches indexing/layout/math bugs, not ulps).
        printf("GPU kernel vs CPU reference: max relative diff = %.2e over %d pixels\n", maxrel, n);
        if (maxrel < 2e-3) { printf("GPU KERNEL OK\n"); return 0; }
        printf("GPU KERNEL MISMATCH\n"); return 1;
    }
}
