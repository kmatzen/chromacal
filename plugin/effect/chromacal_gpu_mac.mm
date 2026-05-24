// Metal host for the chromacal GPU render path (built only when CHROMACAL_GPU).
// Loads the bundled chromacal.metallib (next to this dylib, in Contents/Resources),
// builds the compute pipeline, and dispatches the kernel over PF GPU-world buffers.
// Compiled as Objective-C++ under MRC (no ARC).

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "chromacal_gpu.h"

#include <dlfcn.h>

namespace {
// .../Contents/MacOS/chromacal  ->  .../Contents/Resources/chromacal.metallib
NSString* MetallibPath() {
    Dl_info info;
    if (!dladdr(reinterpret_cast<const void*>(&MetallibPath), &info) || !info.dli_fname)
        return nil;
    NSString* macho = [NSString stringWithUTF8String:info.dli_fname];
    NSString* contents = [[macho stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
    return [contents stringByAppendingPathComponent:@"Resources/chromacal.metallib"];
}

struct Holder {
    id<MTLComputePipelineState> pso;
};
} // namespace

extern "C" void* chromacal_gpu_make_pipeline(void* mtlDevice) {
    if (!mtlDevice) return nullptr;
    @autoreleasepool {
        id<MTLDevice> dev = (id<MTLDevice>)mtlDevice;
        NSString* path = MetallibPath();
        if (!path) return nullptr;
        NSError* err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithFile:path error:&err];
        if (!lib) return nullptr;
        id<MTLFunction> fn = [lib newFunctionWithName:@"chromacal_apply"];
        if (!fn) { [lib release]; return nullptr; }
        id<MTLComputePipelineState> pso = [dev newComputePipelineStateWithFunction:fn error:&err];
        [fn release];
        [lib release];
        if (!pso) return nullptr;
        Holder* h = new Holder();
        h->pso = pso; // owned (newComputePipelineState… returns +1 under MRC)
        return h;
    }
}

extern "C" void chromacal_gpu_dispose(void* holder) {
    Holder* h = static_cast<Holder*>(holder);
    if (!h) return;
    [h->pso release];
    delete h;
}

extern "C" int chromacal_gpu_run(void* holder, void* mtlCommandQueue, void* inBuf,
                                 void* outBuf, const ChromacalGPUParams* params) {
    Holder* h = static_cast<Holder*>(holder);
    if (!h || !h->pso || !mtlCommandQueue || !inBuf || !outBuf || !params) return 1;
    @autoreleasepool {
        id<MTLCommandQueue> queue = (id<MTLCommandQueue>)mtlCommandQueue;
        id<MTLBuffer> in = (id<MTLBuffer>)inBuf;
        id<MTLBuffer> out = (id<MTLBuffer>)outBuf;
        id<MTLDevice> dev = [queue device];
        id<MTLBuffer> pbuf = [dev newBufferWithBytes:params
                                              length:sizeof(*params)
                                             options:MTLResourceStorageModeShared];
        if (!pbuf) return 2;

        id<MTLCommandBuffer> cb = [queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        [enc setComputePipelineState:h->pso];
        [enc setBuffer:in offset:0 atIndex:0];
        [enc setBuffer:out offset:0 atIndex:1];
        [enc setBuffer:pbuf offset:0 atIndex:2];
        MTLSize tg = MTLSizeMake(16, 16, 1);
        MTLSize groups = MTLSizeMake((params->width + 15) / 16, (params->height + 15) / 16, 1);
        [enc dispatchThreadgroups:groups threadsPerThreadgroup:tg];
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        [pbuf release];
        return cb.error ? 3 : 0;
    }
}
