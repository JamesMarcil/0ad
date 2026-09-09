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

/**
 * @file
 * Superluminal instrumentation API integration.
 *
 * Provides a self-contained interface to the Superluminal CPU profiler,
 * with graceful fallback to no-ops when Superluminal is not available
 * or when CONFIG2_SUPERLUMINAL is not defined.
 *
 * This header does NOT include windows.h or Superluminal's loader header
 * to avoid polluting the preprocessor namespace (e.g., min/max macros).
 * The actual DLL loading logic is confined to Superluminal.cpp.
 */

#ifndef INCLUDED_SUPERLUMINAL
#define INCLUDED_SUPERLUMINAL

// Define SUPERLUMINAL_ENABLED based on platform and build config
#if defined(_WIN32) && defined(CONFIG2_SUPERLUMINAL)
	#define SUPERLUMINAL_ENABLED 1
#else
	#define SUPERLUMINAL_ENABLED 0
#endif

namespace Superluminal
{
	/**
	 * Initialise the Superluminal profiler integration.
	 * Attempts to load the PerformanceAPI DLL in order of preference:
	 * 1. Path specified in PYROGENESIS_SUPERLUMINAL_DLL environment variable
	 * 2. PerformanceAPI.dll in the same directory as the running executable
	 * 3. PerformanceAPI.dll in %ProgramFiles%\Superluminal\Performance\API\dll\<arch>\
	 *
	 * @return true if the DLL was successfully loaded and initialised, false otherwise.
	 *         Returning false is the expected case when Superluminal is not installed.
	 */
	bool Initialise();

	/**
	 * Shut down the Superluminal profiler integration.
	 * Intentionally does NOT free the module (to avoid shutdown race conditions).
	 * After this call, all profiler functions become no-ops.
	 */
	void Shutdown();

	/**
	 * Record the beginning of a named profiling region.
	 * @param id A compile-time constant string identifying the region.
	 *           Must remain valid for the lifetime of the program.
	 */
	void BeginEvent(const char* id);

	/**
	 * Record the beginning of a named profiling region with optional runtime data.
	 * @param id A compile-time constant string identifying the region.
	 *           Must remain valid for the lifetime of the program.
	 * @param data Optional runtime string providing context for this invocation.
	 *             Can be nullptr. Need not remain valid after the call returns.
	 */
	void BeginEvent(const char* id, const char* data);

	/**
	 * Record the end of the most recent profiling region (LIFO).
	 */
	void EndEvent();

	/**
	 * Set the name of the current thread for display in the profiler.
	 * @param name Thread name as UTF8 encoded string.
	 */
	void SetCurrentThreadName(const char* name);

	/**
	 * RAII helper to automatically record region entry/exit.
	 * Calls BeginEvent in the constructor, EndEvent in the destructor.
	 * Non-copyable.
	 */
	class ScopedEvent
	{
	public:
		/**
		 * Construct and begin a profiling region.
		 * @param id Region identifier (must remain valid forever).
		 */
		explicit ScopedEvent(const char* id);

		/**
		 * Destruct and end the profiling region.
		 */
		~ScopedEvent();

		// Non-copyable
		ScopedEvent(const ScopedEvent&) = delete;
		ScopedEvent& operator=(const ScopedEvent&) = delete;

	private:
		const char* m_Id;
	};

} // namespace Superluminal

#endif // INCLUDED_SUPERLUMINAL
