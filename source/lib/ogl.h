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
 * OpenGL helper functions.
 */

#ifndef INCLUDED_OGL
#define INCLUDED_OGL

#include "lib/code_annotation.h"
#include "lib/config2.h" // CONFIG2_GLES


#if CONFIG2_GLES
# include "external_libraries/opengles2_wrapper.h"
#else
# include <glad/gl.h>
#endif

//-----------------------------------------------------------------------------
// errors

/**
 * raise a warning (break into the debugger) if an OpenGL error is pending.
 * resets the OpenGL error state afterwards.
 *
 * when an error is reported, insert calls to this in a binary-search scheme
 * to quickly narrow down the actual error location.
 *
 * reports a bogus invalid_operation error if called before OpenGL is
 * initialized, so don't!
 *
 * disabled in release mode for efficiency and to avoid annoying errors.
 **/
extern void ogl_WarnIfErrorLoc(const char *file, int line);
#ifdef NDEBUG
# define ogl_WarnIfError()
#else
# define ogl_WarnIfError() ogl_WarnIfErrorLoc(__FILE__, __LINE__)
#endif

/**
* get a name of the error.
*
* useful for debug.
*
* @return read-only C string of unspecified length containing
* the error's name.
**/
extern const char* ogl_GetErrorName(GLenum err);

#endif	// INCLUDED_OGL
