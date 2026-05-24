// Native macOS "Save As…" / "Open…" panels for the chromacal effect's Export
// .cube and Save/Load calibration buttons.
//
// The PF/Pr effect SDK exposes no file-chooser, so we use NSSavePanel/NSOpenPanel
// directly. They must run on the main thread (UserChangedParam, a UI event, is on
// it).
//
// Returns: 1 = user chose a path (written to outBuf), 0 = user cancelled,
//         -1 = couldn't present (e.g. not main thread) -> caller should fall back.

#import <AppKit/AppKit.h>

#include <cstring>

extern "C" int chromacal_choose_save_path(const char* suggestedName, const char* ext,
                                          char* outBuf, unsigned long outSize) {
    if (!outBuf || outSize == 0) return -1;
    if (![NSThread isMainThread]) return -1;

    int result = 0;
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.nameFieldStringValue =
            [NSString stringWithUTF8String:(suggestedName ? suggestedName : "chromacal")];
        if (ext && *ext)
            panel.allowedFileTypes = @[ [NSString stringWithUTF8String:ext] ];
        panel.extensionHidden = NO;
        panel.title = @"Save";

        if ([panel runModal] == NSModalResponseOK) {
            const char* p = panel.URL.path.fileSystemRepresentation;
            if (p && *p) {
                std::strncpy(outBuf, p, outSize - 1);
                outBuf[outSize - 1] = '\0';
                result = 1;
            }
        }
    }
    return result;
}

extern "C" int chromacal_choose_open_path(char* outBuf, unsigned long outSize) {
    if (!outBuf || outSize == 0) return -1;
    if (![NSThread isMainThread]) return -1;

    int result = 0;
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.title = @"Open";

        if ([panel runModal] == NSModalResponseOK) {
            const char* p = panel.URLs.firstObject.path.fileSystemRepresentation;
            if (p && *p) {
                std::strncpy(outBuf, p, outSize - 1);
                outBuf[outSize - 1] = '\0';
                result = 1;
            }
        }
    }
    return result;
}
