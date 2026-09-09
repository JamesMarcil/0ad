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

#include "precompiled.h"

#include "Superluminal.h"

#if SUPERLUMINAL_ENABLED

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX

#include <windows.h>
#include "ps/CLogger.h"
#include <Superluminal/PerformanceAPI_loader.h>
#include <cstdlib>
#include <cwchar>

namespace Superluminal
{
	// Static state for the loaded Superluminal API
	static PerformanceAPI_Functions g_Functions = {};
	static PerformanceAPI_ModuleHandle g_Module = nullptr;

	bool Initialise()
	{
		// Try to load the DLL in order of preference
		const wchar_t* dllPath = nullptr;
		wchar_t envBuffer[MAX_PATH] = {0};
		wchar_t programFilesBuffer[MAX_PATH] = {0};

		// Option 1: Try environment variable
		const wchar_t* envVarPath = _wgetenv(L"PYROGENESIS_SUPERLUMINAL_DLL");
		if (envVarPath)
		{
			g_Module = PerformanceAPI_LoadFrom(envVarPath, &g_Functions);
			if (g_Module)
			{
				LOGMESSAGE("Superluminal: Loaded from PYROGENESIS_SUPERLUMINAL_DLL");
				return true;
			}
		}

		// Option 2: Try exe directory
		// Get the path to the running executable
		wchar_t exePath[MAX_PATH] = {0};
		if (GetModuleFileNameW(nullptr, exePath, MAX_PATH))
		{
			// Find the last backslash to get the directory
			wchar_t* lastBackslash = wcsrchr(exePath, L'\\');
			if (lastBackslash)
			{
				*lastBackslash = L'\0'; // Terminate at the directory separator
				wcsncpy_s(envBuffer, MAX_PATH, exePath, MAX_PATH - 1);
				wcsncat_s(envBuffer, MAX_PATH, L"\\PerformanceAPI.dll", MAX_PATH - wcslen(envBuffer) - 1);
				g_Module = PerformanceAPI_LoadFrom(envBuffer, &g_Functions);
				if (g_Module)
				{
					LOGMESSAGE("Superluminal: Loaded from exe directory");
					return true;
				}
			}
		}

		// Option 3: Try %ProgramFiles%/Superluminal/Performance/API/dll/<arch>/
		const wchar_t* programFiles = _wgetenv(L"ProgramFiles");
		if (programFiles)
		{
			// Determine architecture
			const wchar_t* arch = L"x64";
#if defined(_M_ARM64)
			arch = L"arm64";
#endif

			wcsncpy_s(programFilesBuffer, MAX_PATH, programFiles, MAX_PATH - 1);
			wcsncat_s(programFilesBuffer, MAX_PATH, L"\\Superluminal\\Performance\\API\\dll\\", MAX_PATH - wcslen(programFilesBuffer) - 1);
			wcsncat_s(programFilesBuffer, MAX_PATH, arch, MAX_PATH - wcslen(programFilesBuffer) - 1);
			wcsncat_s(programFilesBuffer, MAX_PATH, L"\\PerformanceAPI.dll", MAX_PATH - wcslen(programFilesBuffer) - 1);

			g_Module = PerformanceAPI_LoadFrom(programFilesBuffer, &g_Functions);
			if (g_Module)
			{
				LOGMESSAGE("Superluminal: Loaded from Program Files");
				return true;
			}
		}

		// Failed to load from all paths; this is expected when Superluminal is not installed
		LOGMESSAGE("Superluminal: Not found (Superluminal not installed)");
		return false;
	}

	void Shutdown()
	{
		// Intentionally do NOT call PerformanceAPI_Free / FreeLibrary.
		// If any thread is still executing inside a profiled region at shutdown time,
		// freeing the module would cause a crash as the thread exits the instrumented code.
		// The module leak is acceptable here because:
		// 1. It only happens once at shutdown
		// 2. The OS will reclaim it when the process exits
		// 3. It prevents rare shutdown races
		//
		// Instead, we just zero out the function table so subsequent calls are harmless no-ops.
		g_Functions = {};
	}

	void BeginEvent(const char* id)
	{
		BeginEvent(id, nullptr);
	}

	void BeginEvent(const char* id, const char* data)
	{
		if (g_Functions.BeginEvent)
		{
			// Use default color for consistency
			g_Functions.BeginEvent(id, data, 0xFFFFFFFF);
		}
	}

	void EndEvent()
	{
		if (g_Functions.EndEvent)
		{
			// Store the return value to prevent tail call optimization.
			// The SDK's PerformanceAPI_SuppressTailCallOptimization struct exists
			// precisely for this reason - the function returns it via sret (x64 ABI),
			// and that process itself prevents the call from being optimized to a jmp.
			PerformanceAPI_SuppressTailCallOptimization result = g_Functions.EndEvent();
			(void)result; // Mark as intentionally unused
		}
	}

	void SetCurrentThreadName(const char* name)
	{
		if (g_Functions.SetCurrentThreadName)
		{
			g_Functions.SetCurrentThreadName(name);
		}
	}

	ScopedEvent::ScopedEvent(const char* id) : m_Id(id)
	{
		Superluminal::BeginEvent(m_Id);
	}

	ScopedEvent::~ScopedEvent()
	{
		Superluminal::EndEvent();
	}

} // namespace Superluminal

#else // SUPERLUMINAL_ENABLED

namespace Superluminal
{
	bool Initialise()
	{
		return false;
	}

	void Shutdown()
	{
		// No-op
	}

	void BeginEvent(const char* /*id*/)
	{
		// No-op
	}

	void BeginEvent(const char* /*id*/, const char* /*data*/)
	{
		// No-op
	}

	void EndEvent()
	{
		// No-op
	}

	void SetCurrentThreadName(const char* /*name*/)
	{
		// No-op
	}

	ScopedEvent::ScopedEvent(const char* /*id*/)
	{
		// No-op
	}

	ScopedEvent::~ScopedEvent()
	{
		// No-op
	}

} // namespace Superluminal

#endif // SUPERLUMINAL_ENABLED
