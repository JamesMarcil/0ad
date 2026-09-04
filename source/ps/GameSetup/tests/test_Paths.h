/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "lib/self_test.h"

#include "lib/os_path.h"
#include "ps/CStr.h"
#include "ps/GameSetup/CmdLineArgs.h"
#include "ps/GameSetup/Paths.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

class TestPaths : public CxxTest::TestSuite
{
public:
	void test_logdir_not_specified()
	{
		// When -logDir is not specified, logs path should be OS-specific default
		constexpr std::array<const char*, 2> argv = { "program", "-test=value" };
		CmdLineArgs args(argv);
		Paths paths(args);

		// The logs path should exist and not be empty (OS-specific default)
		TS_ASSERT(!paths.Logs().empty());
	}

	void test_logdir_empty_value()
	{
		// When -logDir= (empty value) is specified, logs path should be OS-specific default
		constexpr std::array<const char*, 2> argv = { "program", "-logDir=" };
		CmdLineArgs args(argv);
		Paths paths(args);

		// The logs path should exist and not be empty (OS-specific default)
		TS_ASSERT(!paths.Logs().empty());
	}

	void test_logdir_absolute_path()
	{
		// When -logDir=/some/path is specified, logs path should be overridden
#if OS_WIN
		constexpr std::array<const char*, 2> argv = { "program", "-logDir=C:\\temp\\logs" };
		const OsPath expectedLogs(L"C:\\temp\\logs\\");
#else
		constexpr std::array<const char*, 2> argv = { "program", "-logDir=/tmp/logs" };
		const OsPath expectedLogs(L"/tmp/logs/");
#endif
		CmdLineArgs args(argv);
		Paths paths(args);

		// The logs path should be the overridden value (with trailing separator)
		TS_ASSERT_EQUALS(paths.Logs(), expectedLogs);
	}

	void test_logdir_relative_path()
	{
		// When -logDir=./relative/path is specified, logs path should be overridden
		constexpr std::array<const char*, 2> argv = { "program", "-logDir=./custom_logs" };
		CmdLineArgs args(argv);
		Paths paths(args);

		// The logs path should be the overridden relative value
		// Note: the exact path depends on how OsPath constructs the path
		TS_ASSERT(!paths.Logs().empty());
		TS_ASSERT(paths.Logs().IsDirectory()); // Should end with separator
	}

	void test_logdir_precedence()
	{
		// -logDir should take precedence over -writableRoot
#if OS_WIN
		constexpr std::array<const char*, 3> argv = {
			"program",
			"-writableRoot",
			"-logDir=C:\\custom\\logs"
		};
		const OsPath expectedLogs(L"C:\\custom\\logs\\");
#else
		constexpr std::array<const char*, 3> argv = {
			"program",
			"-writableRoot",
			"-logDir=/custom/logs"
		};
		const OsPath expectedLogs(L"/custom/logs/");
#endif
		CmdLineArgs args(argv);
		Paths paths(args);

		// The logs path should be the overridden value, not from writableRoot
		TS_ASSERT_EQUALS(paths.Logs(), expectedLogs);
	}

	void test_logdir_with_forward_slashes()
	{
		// OsPath should handle forward slashes on all platforms
		constexpr std::array<const char*, 2> argv = { "program", "-logDir=/path/to/logs" };
		CmdLineArgs args(argv);
		Paths paths(args);

		TS_ASSERT(!paths.Logs().empty());
		TS_ASSERT(paths.Logs().IsDirectory()); // Should be directory path (ends with /)
	}

	void test_logdir_is_directory_path()
	{
		// The logs path should always end with a separator (be a directory path)
		constexpr std::array<const char*, 2> argv = { "program", "-logDir=/my/logs" };
		CmdLineArgs args(argv);
		Paths paths(args);

		// IsDirectory() returns true if path ends with separator
		TS_ASSERT(paths.Logs().IsDirectory());
	}
};
