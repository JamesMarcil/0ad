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

#include "lib/sysdep/filesystem.h"

#include "lib/debug.h"
#include "lib/sysdep/os/win/wutil.h"	// StatusFromWin
#include "lib/sysdep/os/win/wposix/waio.h"	// waio_reopen
#include "lib/sysdep/os/win/wposix/crt_posix.h"			// _close, _lseeki64 etc.

//-----------------------------------------------------------------------------
// fcntl.h
//-----------------------------------------------------------------------------

static int ShareMode(int oflag)
{
	// Derive share mode from open flags to allow concurrent access where safe.
	// For read-only access, allow other processes to read; for write modes,
	// deny read access to other processes (prevent data corruption/consistency issues).
	// (c.f. waio.cpp ShareMode() which uses Windows API FILE_SHARE_* equivalents)
	if((oflag & (O_WRONLY|O_RDWR)) == 0)  // O_RDONLY
		return _SH_DENYWR;  // allow others to read, deny write
	else  // O_WRONLY or O_RDWR
		return _SH_DENYRD;  // keep current behavior - deny read to others
}

int wopen(const OsPath& pathname, int oflag)
{
	ENSURE(!(oflag & O_CREAT));	// must specify mode_arg if O_CREAT
	return wopen(OsString(pathname), oflag, _S_IREAD|_S_IWRITE);
}


int wopen(const OsPath& pathname, int oflag, mode_t mode)
{
	if(oflag & O_DIRECT)
	{
		Status ret = waio_open(pathname, oflag);
		if(ret < 0)
		{
			errno = ErrnoFromStatus(ret);
			return -1;
		}
		return (int)ret;	// file descriptor
	}
	else
	{
		WinScopedPreserveLastError s;	// _wsopen_s's CreateFileW
		int fd;
		oflag |= _O_BINARY;
		if(oflag & O_WRONLY)
			oflag |= O_CREAT|O_TRUNC;
		// NB: _wsopen_s ignores mode unless oflag & O_CREAT
		errno_t ret = _wsopen_s(&fd, OsString(pathname).c_str(), oflag, ShareMode(oflag), mode);
		if(ret != 0)
		{
			errno = ret;
			return -1;	// NOWARN
		}
		return fd;
	}
}


int wclose(int fd)
{
	ENSURE(fd >= 3);	// not invalid nor stdin/out/err

	if(waio_close(fd) != 0)
		return _close(fd);
	return 0;
}
