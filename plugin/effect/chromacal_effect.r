// PiPL resource for the chromacal native effect.
//
// Compiled by the SDK build (Rez on macOS / PiPLtool on Windows) and embedded
// in the plugin bundle so Premiere/AE can discover the effect. Mirrors the
// SDK_ProcAmp sample, with chromacal's name and the out-flags this effect sets:
//   AE_Effect_Global_OutFlags   = USE_OUTPUT_EXTENT (1<<6) | FORCE_RERENDER (1<<22) = 0x400040
//   AE_Effect_Global_OutFlags_2 = PRESERVES_FULLY_OPAQUE_PIXELS (1<<8)              = 0x100

#include "AEConfig.h"
#include "AE_EffectVers.h"
#include "AE_General.r"

resource 'PiPL' (16000) {
    {
        Kind {
            AEEffect
        },
        Name {
            "chromacal"
        },
        Category {
            "chromacal"
        },

#ifdef AE_OS_WIN
        CodeWin64X86 {"EffectMain"},
#else
        CodeMacARM64 {"EffectMain"},
        CodeMacIntel64 {"EffectMain"},
#endif

        AE_PiPL_Version {
            2,
            0
        },
        AE_Effect_Spec_Version {
            PF_PLUG_IN_VERSION,
            PF_PLUG_IN_SUBVERS
        },
        AE_Effect_Version {
            524289   /* 0x00080001 — bump on releases */
        },
        AE_Effect_Info_Flags {
            0
        },
        AE_Effect_Global_OutFlags {
            0x00400140
        },
        AE_Effect_Global_OutFlags_2 {
            0x00000100
        },
        AE_Effect_Match_Name {
            "chromacal ColorChecker"
        },
        AE_Reserved_Info {
            8
        }
    }
};
