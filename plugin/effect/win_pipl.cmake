# Generates the Windows PiPL resource (chromacal_effect.rc) from chromacal_effect.r,
# replicating the After Effects SDK's 3-step flow (see the SDK's Examples/*/Win
# .vcxproj CustomBuild step):
#   1) cl /EP        <.r>   -> <.rr>   (C-preprocess the PiPL source)
#   2) PiPLtool      <.rr>  -> <.rrc>  (Adobe's resource encoder)
#   3) cl /D MSWindows /EP <.rrc> -> <.rc>
# rc.exe then compiles the .rc and the linker embeds it in chromacal.aex (CMake
# does the rc.exe + link automatically once the .rc is a target source).
#
# Invoked via `cmake -P` with: CL, PIPLTOOL, R, RR, RRC, RC, INC1, INC2, INC3.
# Uses execute_process(OUTPUT_FILE ...) so redirection is portable (no shell).

execute_process(
    COMMAND "${CL}" /nologo /I "${INC1}" /I "${INC2}" /I "${INC3}" /EP "${R}"
    OUTPUT_FILE "${RR}"
    RESULT_VARIABLE _r1)
if(_r1)
    message(FATAL_ERROR "PiPL step 1 (cl /EP on ${R}) failed: ${_r1}")
endif()

execute_process(
    COMMAND "${PIPLTOOL}" "${RR}" "${RRC}"
    RESULT_VARIABLE _r2)
if(_r2)
    message(FATAL_ERROR "PiPL step 2 (PiPLtool) failed: ${_r2}")
endif()

execute_process(
    COMMAND "${CL}" /nologo /D MSWindows /EP "${RRC}"
    OUTPUT_FILE "${RC}"
    RESULT_VARIABLE _r3)
if(_r3)
    message(FATAL_ERROR "PiPL step 3 (cl /EP on ${RRC}) failed: ${_r3}")
endif()
