/* Copyright (C) 2025 Wildfire Games.
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
 * private texture loader helper functions
 */

#ifndef INCLUDED_TEX_INTERNAL
#define INCLUDED_TEX_INTERNAL

#include "lib/alignment.h"
#include "lib/allocators/dynarray.h"
#include "lib/allocators/shared_ptr.h"
#include "lib/debug.h"
#include "lib/status.h"
#include "lib/sysdep/rtl.h"
#include "ps/ProfileTracy.h"

#include <cstddef>
#include <memory>

/**
 * allocate an aligned buffer of decoded texture pixel data and report it to
 * Tracy's "Renderer Textures" memory pool.
 *
 * as AllocateAligned, except that the returned shared_ptr's deleter reports the
 * matching free. these buffers end up in Tex::m_Data, a shared_ptr whose
 * lifetime belongs to whoever still holds a reference - a decoded image can be
 * released long after, and far from, the codec that produced it - so the free
 * cannot be reported from any one call site.
 *
 * AllocateAligned itself is deliberately left untouched: it is shared with
 * unrelated buffers (VFS writes, archive I/O) that must not be counted as
 * texture data.
 *
 * @param p receives the buffer
 * @param size bytes to allocate
 * @param alignment as AllocateAligned
 * @return Status
 **/
template<class T>
static inline Status tex_AllocateAligned(std::shared_ptr<T>& p, size_t size,
	size_t alignment = cacheLineSize)
{
#if defined(TRACY_ENABLE) && TRACY_ENABLE
	void* mem = rtl_AllocateAligned(size, alignment);
	if(!mem)
		WARN_RETURN(ERR::NO_MEM);
	TRACY_ALLOC_NAMED(mem, size, PS::Tracy::MemoryPool::RendererTextures);
	p.reset(static_cast<T*>(mem), [](T* t)
	{
		TRACY_FREE_NAMED(t, PS::Tracy::MemoryPool::RendererTextures);
		rtl_FreeAligned(t);
	});
	return INFO::OK;
#else
	return AllocateAligned(p, size, alignment);
#endif
}

/**
 * check if the given texture format is acceptable: 8bpp grey,
 * 24bpp color or 32bpp color+alpha (BGR / upside down are permitted).
 * basically, this is the "plain" format understood by all codecs and
 * tex_codec_plain_transform.
 * @param bpp bits per pixel
 * @param flags TexFlags
 * @return Status
 **/
extern Status tex_validate_plain_format(size_t bpp, size_t flags);


/**
 * indicate if the two vertical orientations match.
 *
 * used by tex_codec.
 *
 * @param src_flags TexFlags, used to extract the orientation.
 * we ask for this instead of src_orientation so callers don't have to
 * mask off TEX_ORIENTATION.
 * @param dst_orientation orientation to compare against.
 * can be one of TEX_BOTTOM_UP, TEX_TOP_DOWN, or 0 for the
 * "global orientation".
 * @return bool
 **/
extern bool tex_orientations_match(size_t src_flags, size_t dst_orientation);

#endif	// #ifndef INCLUDED_TEX_INTERNAL
