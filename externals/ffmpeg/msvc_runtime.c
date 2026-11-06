// SPDX-License-Identifier: GPL-3.0-or-later

// MinGW builds link the MSVC-built AV1 FFmpeg package (FFmpeg + dav1d, static .lib archives).
// Those objects expect a few pieces of the MSVC runtime that mingw-w64's UCRT does not provide:
// the /GS stack cookie and its checks, the /GS exception filter, _fltused, __isa_available and
// __chkstk. Everything else they use comes from the UCRT.

#include <stdint.h>
#include <stdlib.h>
#include <windows.h>

uintptr_t __security_cookie = 0x00002B992DDFA232ULL;
int _fltused = 0x9875;
// MSVC's vectorised helpers pick their code path from this; 0 selects the SSE2 baseline.
int __isa_available = 0;

void __security_check_cookie(uintptr_t cookie) {
    if (cookie != __security_cookie) {
        abort();
    }
}

void __report_rangecheckfailure(void) {
    abort();
}

// Personality routine of /GS frames that also have SEH data. FFmpeg raises no exceptions, so
// there is nothing to check: let the search continue.
EXCEPTION_DISPOSITION __GSHandlerCheck(void* record, void* frame, void* context, void* dispatcher) {
    (void)record;
    (void)frame;
    (void)context;
    (void)dispatcher;
    return ExceptionContinueSearch;
}

// MSVC's __chkstk (x64) probes RAX bytes below RSP without moving RSP, the contract of
// compiler-rt's ___chkstk_ms.
__asm__(".text\n"
        ".globl __chkstk\n"
        "__chkstk:\n"
        "    jmp ___chkstk_ms\n");
