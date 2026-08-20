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

#ifndef INCLUDED_PROFILE_TRACY
#define INCLUDED_PROFILE_TRACY

#if defined(TRACY_ENABLE) && TRACY_ENABLE
#include "tracy/Tracy.hpp"

#if defined(TRACY_MANUAL_LIFETIME)
#define TRACY_STARTUP() tracy::StartupProfiler()
#define TRACY_SHUTDOWN() tracy::ShutdownProfiler()
#else
#define TRACY_STARTUP()
#define TRACY_SHUTDOWN()
#endif

#define TRACY_ZONE(name) ZoneScopedN(name)
#define TRACY_ZONE_SCOPED() ZoneScoped
#define TRACY_ZONE_COLOR(name, color) ZoneScopedNC(name, color)
#define TRACY_ZONE_TEXT(str, len) ZoneText(str, len)
#define TRACY_ZONE_VALUE(val) ZoneValue(val)
#define TRACY_FRAME_MARK() FrameMark
#define TRACY_FRAME_MARK_NAMED(name) FrameMarkNamed(name)
#define TRACY_SET_THREAD_NAME(name) tracy::SetThreadName(name)
#define TRACY_PLOT(name, val) TracyPlot(name, val)
#define TRACY_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define TRACY_FREE(ptr) TracyFree(ptr)
#define TRACY_MESSAGE(msg, len) TracyMessage(msg, len)
#define TRACY_MESSAGE_L(literal) TracyMessageL(literal)
#define TRACY_LOCKABLE(type, name) TracyLockable(type, name)

#else // !TRACY_ENABLE

#define TRACY_STARTUP()
#define TRACY_SHUTDOWN()
#define TRACY_ZONE(name)
#define TRACY_ZONE_SCOPED()
#define TRACY_ZONE_COLOR(name, color)
#define TRACY_ZONE_TEXT(str, len)
#define TRACY_ZONE_VALUE(val)
#define TRACY_FRAME_MARK()
#define TRACY_FRAME_MARK_NAMED(name)
#define TRACY_SET_THREAD_NAME(name)
#define TRACY_PLOT(name, val)
#define TRACY_ALLOC(ptr, size)
#define TRACY_FREE(ptr)
#define TRACY_MESSAGE(msg, len)
#define TRACY_MESSAGE_L(literal)
#define TRACY_LOCKABLE(type, name) type name

#endif // TRACY_ENABLE

#endif // INCLUDED_PROFILE_TRACY
