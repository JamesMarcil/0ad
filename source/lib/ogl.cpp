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

#include "ogl.h"

#include "lib/config2.h"
#include "lib/debug.h"
#include "ps/CLogger.h"

#include <cstdarg>
#include <cstring>


static int GLVersion;

bool ogl_HaveVersion(int major, int minor)
{
	return GLAD_MAKE_VERSION(major, minor) <= GLVersion;
}

const char* ogl_GetErrorName(GLenum err)
{
#define E(e) case e: return #e;
	switch (err)
	{
	E(GL_INVALID_ENUM)
	E(GL_INVALID_VALUE)
	E(GL_INVALID_OPERATION)
#if !CONFIG2_GLES
	E(GL_STACK_OVERFLOW)
	E(GL_STACK_UNDERFLOW)
#endif
	E(GL_OUT_OF_MEMORY)
	E(GL_INVALID_FRAMEBUFFER_OPERATION)
	default: return "Unknown GL error";
	}
#undef E
}

static void dump_gl_error(GLenum err)
{
	debug_printf("OGL| %s (%04x)\n", ogl_GetErrorName(err), err);
}

void ogl_WarnIfErrorLoc(const char *file, int line)
{
	// glGetError may return multiple errors, so we poll it in a loop.
	// the debug_printf should only happen once (if this is set), though.
	bool error_enountered = false;
	GLenum first_error = 0;

	for(;;)
	{
		GLenum err = glGetError();
		if(err == GL_NO_ERROR)
			break;

		if(!error_enountered)
			first_error = err;

		error_enountered = true;
		dump_gl_error(err);
	}

	if(error_enountered)
		debug_printf("%s:%d: OpenGL error(s) occurred: %s (%04x)\n", file, line, ogl_GetErrorName(first_error), (unsigned int)first_error);
}

//----------------------------------------------------------------------------
// feature and limit detect
//----------------------------------------------------------------------------

bool ogl_Init(void* (load)(const char*))
{
	GLADloadfunc loadFunc = reinterpret_cast<GLADloadfunc>(load);
	if (!loadFunc)
		return false;

#define LOAD_ERROR(ERROR_STRING) \
	if (g_Logger) \
		LOGERROR(ERROR_STRING); \
	else \
		debug_printf(ERROR_STRING); \

#if !CONFIG2_GLES
	GLVersion = gladLoadGL(loadFunc);
	if (!GLVersion)
	{
		LOAD_ERROR("Failed to load OpenGL functions.");
		return false;
	}
#else
	GLVersion = gladLoadGLES2(loadFunc);
	if (!GLVersion)
	{
		LOAD_ERROR("Failed to load GLES2 functions.");
		return false;
	}
#endif
#undef LOAD_ERROR

	return true;
}
