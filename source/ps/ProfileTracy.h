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

// Standard Subsystem Color Palette Constants (RGB)
#define TRACY_COLOR_SIMULATION  0xFF4500 // OrangeRed
#define TRACY_COLOR_SPATIAL     0xFF8C00 // DarkOrange
#define TRACY_COLOR_PATHFINDING 0x9370DB // MediumPurple
#define TRACY_COLOR_AI          0x4169E1 // RoyalBlue
#define TRACY_COLOR_RENDER      0x228B22 // ForestGreen
#define TRACY_COLOR_GUI         0xFF7F50 // Coral
#define TRACY_COLOR_SCRIPT      0xFFD700 // Gold
#define TRACY_COLOR_TASK        0x4682B4 // SteelBlue
#define TRACY_COLOR_AUDIO       0x2E8B57 // SeaGreen
#define TRACY_COLOR_NET         0x008B8B // DarkCyan

/**
 * Canonical names for Tracy's named memory pools.
 *
 * Tracy identifies a memory pool by the *pointer* it is given, not by the
 * string contents (see the Tracy manual, "Unique pointers"). Passing a bare
 * "Pool name" literal at each call site is therefore unsafe: MSVC only pools
 * identical literals when /GF (Enable String Pooling) is on, and Premake only
 * enables it for Release. In a Debug build the same literal used in two
 * translation units - or in two instantiations of a template such as
 * DynamicArena - yields two distinct pointers, so Tracy splits what should be
 * one pool into several and the frees land in a different pool than the
 * matching allocations.
 *
 * These are `inline constexpr`, which since C++17 gives them external linkage
 * and therefore exactly one address across the whole program regardless of
 * /GF. Always pass one of these instead of a literal.
 */
namespace PS::Tracy::MemoryPool
{
inline constexpr const char* LinearAllocator{"PS::Memory::LinearAllocator"};
inline constexpr const char* DynamicArena{"Allocators::DynamicArena"};
inline constexpr const char* Pool{"Allocators::Pool"};
} // namespace PS::Tracy::MemoryPool

#if defined(TRACY_ENABLE) && TRACY_ENABLE
#include "tracy/Tracy.hpp"
#include "tracy/TracyC.h"

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
#define TRACY_ZONE_TEXT_F(fmt, ...) ZoneTextF(fmt, ##__VA_ARGS__)
#define TRACY_ZONE_NAME(txt, size) ZoneName(txt, size)
#define TRACY_ZONE_NAME_F(fmt, ...) ZoneNameF(fmt, ##__VA_ARGS__)
#define TRACY_ZONE_VALUE(val) ZoneValue(val)
#define TRACY_ZONE_TRANSIENT(varname, name, color) ZoneTransientNC(varname, name, color, true)
#define TRACY_FRAME_MARK() FrameMark
#define TRACY_FRAME_MARK_NAMED(name) do { if (TracyIsStarted) FrameMarkNamed(name); } while (0)
#define TRACY_FRAME_MARK_START(name) do { if (TracyIsStarted) FrameMarkStart(name); } while (0)
#define TRACY_FRAME_MARK_END(name) do { if (TracyIsStarted) FrameMarkEnd(name); } while (0)
#define TRACY_SET_THREAD_NAME(name) do { if (TracyIsStarted) tracy::SetThreadName(name); } while (0)
#define TRACY_PLOT(name, val) do { if (TracyIsStarted) TracyPlot(name, val); } while (0)
#define TRACY_PLOT_CONFIG(name, type, step, fill, color) do { if (TracyIsStarted) TracyPlotConfig(name, type, step, fill, color); } while (0)
#define TRACY_PLOT_TYPE_NUMBER tracy::PlotFormatType::Number
#define TRACY_PLOT_TYPE_MEMORY tracy::PlotFormatType::Memory
#define TRACY_PLOT_TYPE_PERCENTAGE tracy::PlotFormatType::Percentage
#define TRACY_ALLOC(ptr, size) do { if (TracyIsStarted) TracyAlloc(ptr, size); } while (0)
#define TRACY_FREE(ptr) do { if (TracyIsStarted) TracyFree(ptr); } while (0)
#define TRACY_ALLOC_NAMED(ptr, size, name) do { if (TracyIsStarted) TracyAllocN(ptr, size, name); } while (0)
#define TRACY_FREE_NAMED(ptr, name) do { if (TracyIsStarted) TracyFreeN(ptr, name); } while (0)
#define TRACY_MEMORY_DISCARD(name) do { if (TracyIsStarted) TracyMemoryDiscard(name); } while (0)
#define TRACY_ALLOC_S(ptr, size, depth) do { if (TracyIsStarted) TracyAllocS(ptr, size, depth); } while (0)
#define TRACY_FREE_S(ptr, depth) do { if (TracyIsStarted) TracyFreeS(ptr, depth); } while (0)
#define TRACY_ALLOC_NAMED_S(ptr, size, depth, name) do { if (TracyIsStarted) TracyAllocNS(ptr, size, depth, name); } while (0)
#define TRACY_FREE_NAMED_S(ptr, name, depth) do { if (TracyIsStarted) TracyFreeNS(ptr, depth, name); } while (0)
#define TRACY_MEMORY_DISCARD_S(name, depth) do { if (TracyIsStarted) TracyMemoryDiscardS(name, depth); } while (0)
#define TRACY_MESSAGE(msg, len) do { if (TracyIsStarted) TracyMessage(msg, len); } while (0)
#define TRACY_MESSAGE_L(literal) do { if (TracyIsStarted) TracyMessageL(literal); } while (0)
#define TRACY_LOCKABLE(type, name) TracyLockable(type, name)
#define TRACY_LOCKABLE_N(type, name, desc) TracyLockableN(type, name, desc)
#define TRACY_SHARED_LOCKABLE(type, name) TracySharedLockable(type, name)
#define TRACY_SHARED_LOCKABLE_N(type, name, desc) TracySharedLockableN(type, name, desc)
#define TRACY_LOCK_MARK(varname) LockMark(varname)
#define TRACY_LOCKABLE_NAME(varname, txt, size) LockableName(varname, txt, size)
#define TRACY_SECTION_ENTER(name, ...) TracySectionEnter(name, ##__VA_ARGS__)
#define TRACY_SECTION_LEAVE(id) TracySectionLeave(id)
#define TRACY_APP_INFO(txt, size) do { if (TracyIsStarted) TracyAppInfo(txt, size); } while (0)
#define TRACY_SET_PROGRAM_NAME(name) do { if (TracyIsStarted) TracySetProgramName(name); } while (0)

#else // !TRACY_ENABLE

#define TRACY_STARTUP() ((void)0)
#define TRACY_SHUTDOWN() ((void)0)
#define TRACY_ZONE(name) ((void)0)
#define TRACY_ZONE_SCOPED() ((void)0)
#define TRACY_ZONE_COLOR(name, color) ((void)0)
#define TRACY_ZONE_TEXT(str, len) ((void)0)
#define TRACY_ZONE_TEXT_F(fmt, ...) ((void)0)
#define TRACY_ZONE_NAME(txt, size) ((void)0)
#define TRACY_ZONE_NAME_F(fmt, ...) ((void)0)
#define TRACY_ZONE_VALUE(val) ((void)0)
#define TRACY_ZONE_TRANSIENT(varname, name, color) ((void)0)
#define TRACY_FRAME_MARK() ((void)0)
#define TRACY_FRAME_MARK_NAMED(name) ((void)0)
#define TRACY_FRAME_MARK_START(name) ((void)0)
#define TRACY_FRAME_MARK_END(name) ((void)0)
#define TRACY_SET_THREAD_NAME(name) ((void)0)
#define TRACY_PLOT(name, val) ((void)0)
#define TRACY_PLOT_CONFIG(name, type, step, fill, color) ((void)0)
#define TRACY_PLOT_TYPE_NUMBER 0
#define TRACY_PLOT_TYPE_MEMORY 1
#define TRACY_PLOT_TYPE_PERCENTAGE 2
#define TRACY_ALLOC(ptr, size) ((void)0)
#define TRACY_FREE(ptr) ((void)0)
#define TRACY_ALLOC_NAMED(ptr, size, name) ((void)0)
#define TRACY_FREE_NAMED(ptr, name) ((void)0)
#define TRACY_MEMORY_DISCARD(name) ((void)0)
#define TRACY_ALLOC_S(ptr, size, depth) ((void)0)
#define TRACY_FREE_S(ptr, depth) ((void)0)
#define TRACY_ALLOC_NAMED_S(ptr, size, depth, name) ((void)0)
#define TRACY_FREE_NAMED_S(ptr, name, depth) ((void)0)
#define TRACY_MEMORY_DISCARD_S(name, depth) ((void)0)
#define TRACY_MESSAGE(msg, len) ((void)0)
#define TRACY_MESSAGE_L(literal) ((void)0)
#define TRACY_LOCKABLE(type, name) type name
#define TRACY_LOCKABLE_N(type, name, desc) type name
#define TRACY_SHARED_LOCKABLE(type, name) type name
#define TRACY_SHARED_LOCKABLE_N(type, name, desc) type name
#define TRACY_LOCK_MARK(varname) ((void)0)
#define TRACY_LOCKABLE_NAME(varname, txt, size) ((void)0)
#define TRACY_SECTION_ENTER(name, ...) (0)
#define TRACY_SECTION_LEAVE(id) ((void)0)
#define TRACY_APP_INFO(txt, size) ((void)0)
#define TRACY_SET_PROGRAM_NAME(name) ((void)0)

#endif // TRACY_ENABLE

#endif // INCLUDED_PROFILE_TRACY
