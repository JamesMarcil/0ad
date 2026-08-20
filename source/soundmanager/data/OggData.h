/* Copyright (C) 2025 Wildfire Games.
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

#ifndef INCLUDED_OGGDATA_H
#define INCLUDED_OGGDATA_H

#include "lib/config2.h"

#if CONFIG2_AUDIO

#include "ogg.h"
#include "SoundData.h"

#include "lib/file/vfs/vfs_path.h"

#include <AL/al.h>
#include <array>

/*
* 50 buffers of 98304 bytes each gives us 4.9 seconds of audio, which is a good amount to have buffered at once.
*/
constexpr int OGG_DEFAULT_BUFFER_COUNT = 50;

class COggData final : public CSoundData
{
public:
	COggData();
	~COggData();

	bool InitOggFile(const VfsPath& itemPath);
	bool IsFileFinished();
	bool IsOneShot() override;
	bool IsStereo() override;

	int FetchDataIntoBuffer(int count, ALuint* buffers);
	void ResetFile();

private:
	ALuint m_Format;
	ALsizei m_Frequency;
protected:
	OggStreamPtr m_Stream;
	bool m_FileFinished;
	bool m_OneShot;
	std::array<ALuint, OGG_DEFAULT_BUFFER_COUNT> m_Buffer{};
	int m_BuffersCount;

// TRACY_ENABLE is defined project-wide by premake (--with-tracy), so this
// member is either present in every translation unit or in none of them.
#if defined(TRACY_ENABLE) && TRACY_ENABLE
	/**
	 * Decoded PCM bytes currently reported to Tracy's "Audio Buffers" pool for
	 * the AL buffer named by the m_Buffer entry at the same index, or 0 if that
	 * entry has no live allocation there. Needed because a streaming item
	 * refills a buffer many times over its life and Tracy has no realloc: each
	 * refill has to free the previous size before reporting the new one, and the
	 * first fill must not report a free of something never allocated.
	 */
	std::array<ALsizei, OGG_DEFAULT_BUFFER_COUNT> m_TracyBufferBytes{};

	/**
	 * Reports @p bytes of decoded PCM for AL buffer @p name - which must be one
	 * of m_Buffer's entries - replacing whatever that buffer held before.
	 */
	void TracyTrackBuffer(ALuint name, ALsizei bytes);

	/**
	 * Reports the frees matching every live allocation in m_Buffer's index range
	 * [@p first, @p last). Called alongside each alDeleteBuffers over the same
	 * range, and again for everything left over in the destructor: the pool is
	 * keyed on the addresses of m_Buffer's slots, so no entry may outlive the
	 * COggData that owns it, or a later instance reusing that heap address would
	 * report an allocation Tracy still considers live.
	 */
	void TracyUntrackBuffers(int first, int last);
#endif

	void SetFormatAndFreq(ALenum form, ALsizei freq);
	int GetBufferCount() override;
	unsigned int GetBuffer() override;
	unsigned int* GetBufferPtr() override;
};

#endif // CONFIG2_AUDIO
#endif // INCLUDED_OGGDATA_H
