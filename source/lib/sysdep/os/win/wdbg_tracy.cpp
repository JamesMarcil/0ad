/* Copyright (C) 2026 Wildfire Games.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * dbghelp serialization for the Tracy client.
 *
 * dbghelp.dll is documented as single-threaded: all calls into it must be
 * serialized per process. wdbg_sym.cpp already does that via WDBG_SYM_CS, but
 * Tracy is a second, independent consumer - its symbol worker thread calls
 * SymInitialize, SymLoadModuleEx, SymFromAddr, SymGetLineFromAddrW64 and the
 * inline-frame APIs from client/TracyCallstack.cpp. Both sides pass
 * GetCurrentProcess(), i.e. the same (HANDLE)-1 pseudo-handle, so dbghelp sees
 * one shared process context with no isolation between them.
 *
 * Defining TRACY_DBGHELP_LOCK=wdbg_tracy_Sym (see build/premake/premake5.lua)
 * makes Tracy wrap every one of those calls in the three functions below, which
 * hand it WDBG_SYM_CS. Without this the races are only latent - Tracy resolves
 * symbols just once at startup - but ETW sampling generates call stacks at the
 * sampling frequency (8 kHz by default), so the symbol worker then hits dbghelp
 * continuously, concurrently with the crash handler's use of it.
 *
 * The lock must be recursive, and WDBG_SYM_CS is: wutil_Lock() is a plain
 * EnterCriticalSection and Win32 critical sections are reentrant for the owning
 * thread. Tracy relies on that - DecodeCallstackPtr() takes the lock and then
 * calls GetModuleNameAndPrepareSymbols(), which takes it again.
 */

#include "precompiled.h"

#if defined(TRACY_ENABLE) && TRACY_ENABLE

#include "lib/sysdep/os/win/wutil.h"

// Names must match the TRACY_DBGHELP_LOCK prefix; Tracy declares them with C
// linkage in client/TracyCallstack.cpp.
extern "C" void wdbg_tracy_SymInit()
{
	// Nothing to do: wutil_Init() creates the critical sections during winit,
	// which runs long before CProfiler2::Initialise() calls TRACY_STARTUP() and
	// therefore before Tracy's symbol worker thread exists. Tracy calls this
	// once, immediately before its first wdbg_tracy_SymLock().
}

extern "C" void wdbg_tracy_SymLock()
{
	wutil_Lock(WDBG_SYM_CS);
}

extern "C" void wdbg_tracy_SymUnlock()
{
	wutil_Unlock(WDBG_SYM_CS);
}

#endif // TRACY_ENABLE
