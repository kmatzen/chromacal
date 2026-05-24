// Native Windows file dialogs for the chromacal effect (Export .cube, Save/Load
// calibration preset) — the Win32 counterpart to save_dialog_mac.mm. Built on
// Windows only (see plugin/CMakeLists.txt); link comdlg32.
//
// Returns: 1 = user chose a path (written to outBuf), 0 = cancelled,
//         -1 = couldn't present -> caller should fall back.

#include <windows.h>

#include <commdlg.h>

#include <cstring>
#include <string>

namespace {
// Build a double-null-terminated Win32 filter: "<desc>\0*.<ext>\0All Files\0*.*\0\0".
std::string MakeFilter(const char* ext) {
    std::string f;
    if (ext && *ext) {
        f += "chromacal (*.";
        f += ext;
        f += ")";
        f.push_back('\0');
        f += "*.";
        f += ext;
        f.push_back('\0');
    }
    f += "All Files (*.*)";
    f.push_back('\0');
    f += "*.*";
    f.push_back('\0');
    f.push_back('\0'); // final terminator
    return f;
}
} // namespace

extern "C" int chromacal_choose_save_path(const char* suggestedName, const char* ext,
                                          char* outBuf, unsigned long outSize) {
    if (!outBuf || outSize == 0) return -1;
    char file[1024] = {0};
    if (suggestedName) {
        std::strncpy(file, suggestedName, sizeof(file) - 1);
        file[sizeof(file) - 1] = '\0';
    }
    std::string filter = MakeFilter(ext);

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = (ext && *ext) ? ext : nullptr;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;

    if (GetSaveFileNameA(&ofn)) {
        std::strncpy(outBuf, file, outSize - 1);
        outBuf[outSize - 1] = '\0';
        return 1;
    }
    return 0; // cancelled (or error)
}

extern "C" int chromacal_choose_open_path(char* outBuf, unsigned long outSize) {
    if (!outBuf || outSize == 0) return -1;
    char file[1024] = {0};

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrFilter = "All Files (*.*)\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        std::strncpy(outBuf, file, outSize - 1);
        outBuf[outSize - 1] = '\0';
        return 1;
    }
    return 0;
}
