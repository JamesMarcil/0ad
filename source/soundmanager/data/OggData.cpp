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

#include "precompiled.h"

#include "OggData.h"

#if CONFIG2_AUDIO

#include "lib/status.h"
#include "lib/types.h"
#include "ps/CLogger.h"
#include "ps/Filesystem.h"
#include "ps/ProfileTracy.h"
#include "soundmanager/SoundManager.h"
#include "soundmanager/data/ogg.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

/*
* Each buffer holds ~0.56 seconds of audio.
* Bytes per second = sampleRate × channels × bytesPerSample.
* For 44100 Hz, 2 channels, 2 bytes per sample, this is 176400 bytes per second.
* Nice multiple of 4096 (system page size)
*/
constexpr int OGG_DEFAULT_BUFFER_SIZE = 98304;

COggData::COggData()
	: m_Format(0), m_Frequency(0), m_OneShot(false), m_BuffersCount(0)
{
}

COggData::~COggData()
{
	AL_CHECK;
	if (m_BuffersCount > 0)
		alDeleteBuffers(m_BuffersCount, &m_Buffer.at(0));

	AL_CHECK;
	m_BuffersCount = 0;
#if defined(TRACY_ENABLE) && TRACY_ENABLE
	// Every remaining entry, not just [0, m_BuffersCount): a buffer that was
	// filled but left outside that range (FetchDataIntoBuffer skips chunks that
	// decode to nothing, so the written indices need not be contiguous) is
	// leaked by the alDeleteBuffers above, and must still be released here so
	// that no pool entry outlives the m_Buffer slot it is keyed on.
	TracyUntrackBuffers(0, OGG_DEFAULT_BUFFER_COUNT);
#endif
}

void COggData::SetFormatAndFreq(ALenum form, ALsizei freq)
{
	m_Format = form;
	m_Frequency = freq;
}

bool COggData::IsStereo()
{
	return m_Format == AL_FORMAT_STEREO16;
}

bool COggData::InitOggFile(const VfsPath& itemPath)
{
	if (OpenOggNonstream(g_VFS, itemPath, m_Stream) != INFO::OK)
		return false;

	m_FileFinished = false;

	SetFormatAndFreq(m_Stream->Format(), m_Stream->SamplingRate());
	SetFileName(itemPath);

	AL_CHECK;
	alGenBuffers(m_Buffer.size(), m_Buffer.data());

	ALenum err{alGetError()};
	if (err != AL_NO_ERROR)
	{
		LOGERROR("Failed to create initial buffer. OpenAL error: %s\n", alGetString(err));
		return false;
	}

	m_BuffersCount = FetchDataIntoBuffer(m_Buffer.size(), m_Buffer.data());
	if (m_FileFinished)
	{
		m_OneShot = true;
		if (m_BuffersCount < OGG_DEFAULT_BUFFER_COUNT)
		{
			alDeleteBuffers(OGG_DEFAULT_BUFFER_COUNT - m_BuffersCount, &m_Buffer.at(m_BuffersCount));
#if defined(TRACY_ENABLE) && TRACY_ENABLE
			TracyUntrackBuffers(m_BuffersCount, OGG_DEFAULT_BUFFER_COUNT);
#endif
		}
	}
	AL_CHECK;

	return true;
}

ALsizei COggData::GetBufferCount()
{
	return m_BuffersCount;
}

bool COggData::IsFileFinished()
{
	return m_FileFinished;
}

void COggData::ResetFile()
{
	m_Stream->ResetFile();
	m_FileFinished = false;
}

bool COggData::IsOneShot()
{
	return m_OneShot;
}

int COggData::FetchDataIntoBuffer(int count, ALuint* buffers)
{
	std::vector<u8> PCMOut(OGG_DEFAULT_BUFFER_SIZE);
	int buffersWritten{0};

	for (int i{0}; i < count && !m_FileFinished; ++i)
	{
		std::fill(PCMOut.begin(), PCMOut.end(), 0);
		const size_t totalRet{m_Stream->GetNextChunk(std::span<u8>(PCMOut))};
		m_FileFinished = m_Stream->AtFileEOF();
		if (totalRet == 0)
			continue;

		++buffersWritten;
		alBufferData(buffers[i], m_Format, PCMOut.data(), static_cast<ALsizei>(totalRet), m_Frequency);
#if defined(TRACY_ENABLE) && TRACY_ENABLE
		TracyTrackBuffer(buffers[i], static_cast<ALsizei>(totalRet));
#endif
	}
	return buffersWritten;
}

#if defined(TRACY_ENABLE) && TRACY_ENABLE
void COggData::TracyTrackBuffer(const ALuint name, const ALsizei bytes)
{
	for (size_t i{0}; i < m_Buffer.size(); ++i)
	{
		if (m_Buffer[i] != name)
			continue;
		// The pool is keyed on the address of the m_Buffer slot rather than on
		// the ALuint it holds: an OpenAL buffer name is a handle, not a pointer,
		// and OpenAL is free to hand the same name out again once
		// alDeleteBuffers releases it, which would collide with a name Tracy
		// still holds live for another COggData.
		if (m_TracyBufferBytes[i] != 0)
			TRACY_FREE_NAMED(&m_Buffer[i], PS::Tracy::MemoryPool::AudioBuffers);
		TRACY_ALLOC_NAMED(&m_Buffer[i], static_cast<size_t>(bytes), PS::Tracy::MemoryPool::AudioBuffers);
		m_TracyBufferBytes[i] = bytes;
		return;
	}
	// Not one of our buffers; leaving it untracked is preferable to reporting
	// an allocation we would have no way of ever matching with a free.
}

void COggData::TracyUntrackBuffers(const int first, const int last)
{
	for (int i{first}; i < last; ++i)
	{
		const size_t index{static_cast<size_t>(i)};
		if (m_TracyBufferBytes.at(index) == 0)
			continue;
		TRACY_FREE_NAMED(&m_Buffer.at(index), PS::Tracy::MemoryPool::AudioBuffers);
		m_TracyBufferBytes.at(index) = 0;
	}
}
#endif

ALuint COggData::GetBuffer()
{
	return m_Buffer.at(0);
}

ALuint* COggData::GetBufferPtr()
{
	return m_Buffer.data();
}

#endif // CONFIG2_AUDIO
