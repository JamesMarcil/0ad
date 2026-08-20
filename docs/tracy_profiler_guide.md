# Tracy Profiler Integration & Modernization Guide for 0 A.D.

## Introduction

0 A.D. integrates the **Tracy Profiler** (v0.14.x) — a real-time, nanosecond-resolution, frame-oriented hybrid profiler capable of capturing CPU timelines, multi-threaded execution, GPU workload demarcation, memory pools and allocators, lock contention, and real-time telemetry plots.

This guide explains how to configure, build, run, and instrument 0 A.D. with modern Tracy v0.14.0 capabilities.

---

## 1. Quick Start: Building with Tracy

Tracy integration is opt-in via Premake build flags so that release binaries built without `--with-tracy` incur zero runtime overhead and zero binary bloat.

### Windows (Visual Studio 2022)

1. Open PowerShell or Command Prompt in the repository root.
2. Regenerate project workspaces with Tracy enabled:
   ```cmd
   build\workspaces\update-workspaces.bat --with-tracy
   ```
3. Build the solution (or open `build/workspaces/vs2022/pyrogenesis.sln` in Visual Studio):
   ```cmd
   MSBuild.exe build/workspaces/vs2022/pyrogenesis.sln /p:Configuration=Release /p:Platform=Win32 /m
   ```

### Linux / macOS

1. Regenerate project files:
   ```bash
   ./build/workspaces/update-workspaces.sh --with-tracy
   ```
2. Compile the binaries:
   ```bash
   cd build/workspaces/gcc
   make -j$(nproc)
   ```

---

## 2. Connecting to Tracy GUI

1. Download or build the Tracy Profiler GUI application matching the vendored client version (v0.14.x). Pre-built GUI executables (`Tracy.exe`) can be obtained from the official [Tracy releases](https://github.com/wolfpld/tracy/releases).
2. Launch `Tracy.exe`.
3. Launch 0 A.D. (e.g. `binaries/system/pyrogenesis.exe` or `test.exe`).
4. In the Tracy GUI, click **Connect** (or enter `127.0.0.1` / your target IP address if profiling remotely).
5. Live performance data, thread timelines, frame marks, and telemetry plots will stream immediately to the GUI.

---

## 3. Standard Subsystem Color Palette

To make performance traces instantly readable and visually differentiated across engine subsystems, 0 A.D. defines standard hex color constants in `source/ps/ProfileTracy.h`:

| Constant | Subsystem | Hex RGB / Visual Color |
|---|---|---|
| `TRACY_COLOR_SIMULATION` | Turn lifecycle, component updates, unit motion | `0xFF4500` (OrangeRed) |
| `TRACY_COLOR_SPATIAL` | Range manager, spatial queries, collision, visibility | `0xFF8C00` (DarkOrange) |
| `TRACY_COLOR_PATHFINDING` | JPS, Hierarchical, Vertex short-path | `0x9370DB` (MediumPurple) |
| `TRACY_COLOR_AI` | AI manager, player scripts, bot planning | `0x4169E1` (RoyalBlue) |
| `TRACY_COLOR_RENDER` | Scene passes, terrain, models, shaders | `0x228B22` (ForestGreen) |
| `TRACY_COLOR_GUI` | GUI manager, widgets, Canvas2D, text layout | `0xFF7F50` (Coral) |
| `TRACY_COLOR_SCRIPT` | SpiderMonkey execution, GC slices, JS APIs | `0xFFD700` (Gold) |
| `TRACY_COLOR_TASK` | TaskManager worker threads, threadpool tasks | `0x4682B4` (SteelBlue) |
| `TRACY_COLOR_AUDIO` | OpenAL sound manager, streaming | `0x2E8B57` (SeaGreen) |
| `TRACY_COLOR_NET` | Network client, server, packets, serialization | `0x008B8B` (DarkCyan) |

---

## 4. Macro Reference & Usage Guide

All Tracy instrumentation in 0 A.D. is abstracted through `source/ps/ProfileTracy.h`. When compiled without `--with-tracy`, all macros cleanly expand to zero-cost no-ops.

### 4.1 Scoped Zones & Subsystem Colors

```cpp
#include "ps/ProfileTracy.h"

void CSimulation2Impl::Update(...)
{
    TRACY_ZONE_COLOR("Sim - Update", TRACY_COLOR_SIMULATION);
    // ... simulation work ...
}
```

### 4.2 Dynamic Zone Text, Names & Numeric Values

```cpp
void LongPathfinder::ComputeJPSPath(..., entity_pos_t x0, entity_pos_t z0, pass_class_t passClass, WaypointPath& path)
{
    TRACY_ZONE_COLOR("ComputePathJPS", TRACY_COLOR_PATHFINDING);
    TRACY_ZONE_TEXT_F("From (%.1f,%.1f) | PassClass: %u", x0.ToDouble(), z0.ToDouble(), (unsigned)passClass);

    // ... path search ...

    TRACY_ZONE_VALUE(path.m_Waypoints.size());
}
```

### 4.3 Frame Marks & Turn Intervals

Tracy v0.14 supports continuous frame marks as well as discrete duration intervals:

- `TRACY_FRAME_MARK()`: Primary graphical render frame boundary.
- `TRACY_FRAME_MARK_START("SimulationTurn")` & `TRACY_FRAME_MARK_END("SimulationTurn")`: Marks the precise start and end duration of a discrete simulation turn.
- `TRACY_FRAME_MARK_NAMED(name)`: Single instantaneous event marker.

### 4.4 Telemetry Plots & Formatting

Plots can be configured at startup with custom units, step lines, and fill colors:

```cpp
// Configuration (e.g. in CRenderer constructor)
TRACY_PLOT_CONFIG("Draw Calls", TRACY_PLOT_TYPE_NUMBER, true, true, TRACY_COLOR_RENDER);
TRACY_PLOT_CONFIG("JS Heap Size", TRACY_PLOT_TYPE_MEMORY, false, false, TRACY_COLOR_SCRIPT);

// Runtime Emission
TRACY_PLOT("Draw Calls", (int64_t)stats.m_DrawCalls);
TRACY_PLOT("JS Heap Size", (int64_t)gcBytes);
```

Supported plot types:
- `TRACY_PLOT_TYPE_NUMBER`: Standard numerical metric.
- `TRACY_PLOT_TYPE_MEMORY`: Formatted automatically with Byte/KB/MB/GB units.
- `TRACY_PLOT_TYPE_PERCENTAGE`: Formatted as 0–100%.

### 4.5 Lock Contention Profiling

To see how long threads spend waiting for a mutex, who held it while they waited, and how long each hold lasted, declare the mutex through `TRACY_LOCKABLE_N` instead of declaring a bare `std::mutex`:

```cpp
#include "ps/ProfileTracy.h"

// In the class declaration - the description is what the Locks window labels it with:
TRACY_LOCKABLE_N(std::mutex, m_WorkerMutex, "NetServer WorkerMutex");
mutable TRACY_LOCKABLE_N(std::recursive_mutex, m_Mutex, "ConfigDB Mutex");
```

At every lock site, let the guard deduce its type. `TRACY_LOCKABLE_N` declares a `tracy::Lockable<std::mutex>`, not a `std::mutex`, so a guard that spells the type out will no longer compile:

```cpp
std::lock_guard lock(m_WorkerMutex);          // correct: deduces tracy::Lockable<std::mutex>
std::lock_guard<std::mutex> lock(m_Mutex);    // does not compile once the mutex is instrumented
```

Available lock macros:
- `TRACY_LOCKABLE(type, name)`: Declares an instrumented mutex, labelled in the GUI with the variable name.
- `TRACY_LOCKABLE_N(type, name, desc)`: As above, with an explicit human-readable label. Preferred - a bare `m_Mutex` is not identifiable in the Locks window.
- `TRACY_SHARED_LOCKABLE(type, name)` / `TRACY_SHARED_LOCKABLE_N(type, name, desc)`: For reader-writer mutexes (`std::shared_mutex`), tracking `lock_shared` / `unlock_shared` separately from exclusive acquisition.
- `TRACY_LOCK_MARK(varname)`: Records the acquisition site when one lock is taken from several different branches.
- `TRACY_LOCKABLE_NAME(varname, txt, size)`: Assigns a runtime-computed name to a single lock instance (e.g. per worker index).

#### Instrumented locks

| Lock | Label in the Locks window | Contention it exposes |
|---|---|---|
| `TaskManager::m_GlobalMutex` (`source/ps/TaskManager.cpp`) | `TaskManager NormalQueue` | Task dispatch and work-stealing across every worker thread. |
| `TaskManager::m_GlobalLowPriorityMutex` | `TaskManager LowPriorityQueue` | Low-priority queue dispatch. |
| `CCmpRangeManager` query mutex (`source/simulation2/components/CCmpRangeManager.cpp`) | `RangeManager QueryMutex` | Parallel spatial range queries within a simulation turn. |
| `LongPathfinder` JPS cache mutex (`source/simulation2/helpers/LongPathfinder.cpp`) | `Pathfinder JPCMutex` | Threads racing to build the same jump-point cache. |
| `CNetServerWorker::m_WorkerMutex` (`source/network/NetServer.h`) | `NetServer WorkerMutex` | Network worker vs. main thread over the message queue. |
| `NetStats` mutex (`source/network/NetStats.h`) | `NetStats Mutex` | Telemetry collection vs. the profiler display. |
| `CSoundManager` worker / dead-items / distress mutexes (`source/soundmanager/`) | `SoundManager WorkerMutex`, `SoundManager DeadItemsMutex`, `SoundManager DistressMutex` | Audio worker thread vs. gameplay code queueing sounds. |
| `CSoundBase::m_ItemMutex` (`source/soundmanager/items/CSoundBase.h`) | `SoundItem Mutex` | Per-source buffer upload and playback state. |
| `CConsole::m_Mutex` (`source/ps/CConsole.h`) | `Console Mutex` | Console message queue writes from non-main threads. |
| `CConfigDB::m_Mutex` (`source/ps/ConfigDB.h`) | `ConfigDB Mutex` | Config reads/writes and change hooks. A `std::recursive_mutex`, whose nested acquisitions Tracy tracks correctly. |
| `DAP::Interface::m_WaitingLock`, socket handler `m_ConnectionLock` (`source/dapinterface/`) | `DAP WaitingLock`, `DAP ConnectionLock` | Debug-adapter socket thread vs. the main loop. |
| `RL::Interface::m_Lock` (`source/rlinterface/RLInterface.h`) | `RLInterface StateMutex` | RL HTTP thread vs. the simulation. |

#### Locks that deliberately stay uninstrumented

Two constraints decide whether a mutex *can* be instrumented at all. Check both before converting one; each has already cost debugging time once.

**1. It must not be constructed before `TRACY_STARTUP()`.** This build configures Tracy with `TRACY_MANUAL_LIFETIME`, so the profiler singleton does not exist until `EarlyInit()` calls `TRACY_STARTUP()`. Constructing a `tracy::Lockable` registers the lock with that singleton, so a mutex at namespace scope - constructed during static initialisation, before `main()` - trips `assert(s_profilerData)` in `TracyProfiler.cpp` and aborts on startup.

Safe: members of objects created at runtime (`g_NetServer`, `g_SoundManager`, `g_Console`, `g_ConfigDB`, the DAP and RL interfaces), and **function-local** statics, which initialise lazily on first call - see `JPCMutex` in `LongPathfinder::ComputeJPSPath`.

Not safe, and therefore left as plain `std::mutex`:
- `CLogger::m_Mutex` - `CLogger.cpp` constructs a namespace-scope `nullLogger` (the default `g_Logger`) deliberately, so that logging works from the program's very first static initialiser.
- `vfs_mutex` (`source/lib/file/vfs/vfs.cpp`) - namespace-scope static.
- `g_DebugMutex` in `LongPathfinder.cpp` and `VertexPathfinder.cpp` - namespace-scope statics.

**2. It must not be waited on by a `std::condition_variable`.** `std::condition_variable::wait` accepts only `std::unique_lock<std::mutex>`, and `tracy::Lockable<std::mutex>` is a different type. Instrumenting such a mutex means moving its handshake to `std::condition_variable_any` - a change to the subsystem's threading, not to its profiling - so `RL::Interface::m_MsgLock` and `DAP::Interface::m_MsgLock` stay plain.

### 4.6 Memory Pools & Named Allocators

#### Always name a pool with a `PS::Tracy::MemoryPool` constant

Tracy identifies a memory pool by the *pointer* it is handed, not by the string's contents. A bare `"Renderer Geometry"` literal at each call site is therefore unsafe: MSVC merges identical literals only under `/GF`, which Premake enables for Release alone. In a Debug build the same literal in two translation units - or in two instantiations of a template such as `DynamicArena` - has two different addresses, so Tracy splits one pool into several and frees land in a different pool than their allocations.

`source/ps/ProfileTracy.h` declares every pool name as an `inline constexpr const char*`, which has external linkage and therefore exactly one address program-wide. Pass one of those, never a literal:

```cpp
TRACY_ALLOC_NAMED(ptr, size, PS::Tracy::MemoryPool::Pool);   // correct
TRACY_FREE_NAMED(ptr, "Allocators::Pool");                   // wrong: splits the pool in Debug
```

Available memory macros:
- `TRACY_ALLOC(ptr, size)` / `TRACY_FREE(ptr)`: Untagged heap allocation tracking.
- `TRACY_ALLOC_NAMED(ptr, size, name)` / `TRACY_FREE_NAMED(ptr, name)`: Allocation tracking within a named pool.
- `TRACY_MEMORY_DISCARD(name)`: Declares every outstanding allocation in the pool released at once, with no individual frees - the O(1) match for a bump or arena allocator reset.
- `TRACY_ALLOC_S` / `TRACY_FREE_S` / `TRACY_ALLOC_NAMED_S` / `TRACY_FREE_NAMED_S` / `TRACY_MEMORY_DISCARD_S`: Callstack-capturing variants taking an explicit capture depth. Note that this build also defines `TRACY_NO_CALLSTACK`, so no callstack is actually collected.

#### Instrumented pools

| Pool | Owner | What it shows |
|---|---|---|
| `PS::Memory::LinearAllocator` | `source/ps/memory/LinearAllocator.h` | Per-frame renderer scratch allocations. `Release()` reports a discard, so the Memory window shows a clean per-frame sawtooth. |
| `Allocators::DynamicArena` | `source/lib/allocators/DynamicArena.h` | Block growth of the arenas used by spatial queries and AI. |
| `Allocators::Pool` | `source/lib/allocators/pool.cpp` | Fixed-size element churn; `pool_free_all` reports a discard. |
| `VFS FileBuffer` | `source/lib/file/vfs/vfs.cpp` | Bytes currently held live by loaded file buffers. |
| `Renderer Geometry` | `source/graphics/ModelDef.cpp` | CPU-side mesh vertex and face arrays, per loaded model definition. |
| `Renderer Textures` | `source/lib/tex/`, `source/graphics/TextureConverter.cpp` | Decoded texture pixel data - mipmap chains, format transforms, S3TC decompression - plus the RGBA staging buffer used when converting 8bpp input. |
| `Audio Buffers` | `source/soundmanager/data/OggData.cpp` | Decoded PCM handed to OpenAL, per streaming buffer. |

#### Recipe: a custom allocator

Report each block where it is carved out, the matching free where it is returned, and a discard wherever the allocator resets wholesale:

```cpp
void* LinearAllocator::allocate(size_t n, size_t alignment)
{
    void* ptr = /* bump the offset */;
    TRACY_ALLOC_NAMED(ptr, n, PS::Tracy::MemoryPool::LinearAllocator);
    return ptr;
}

void LinearAllocator::Release()
{
    TRACY_MEMORY_DISCARD(PS::Tracy::MemoryPool::LinearAllocator);  // instead of N frees
    // ... reset the offset ...
}
```

#### Recipe: a buffer whose owner outlives the call that allocated it

An allocation reported without a matching free is worse than no instrumentation: once the address is recycled, the next allocation of it looks like a double-allocation to the Tracy server. So for anything handed out as a `std::shared_ptr`, report the free from a custom deleter rather than guessing where the last reference dies. `VFS::LoadFile` and `tex_AllocateAligned` (`source/lib/tex/tex_internal.h`) both do this:

```cpp
void* mem = rtl_AllocateAligned(size, alignment);
if (!mem)
    WARN_RETURN(ERR::NO_MEM);
TRACY_ALLOC_NAMED(mem, size, PS::Tracy::MemoryPool::RendererTextures);
p.reset(static_cast<T*>(mem), [](T* t)
{
    TRACY_FREE_NAMED(t, PS::Tracy::MemoryPool::RendererTextures);
    rtl_FreeAligned(t);
});
```

Both wrap `rtl_AllocateAligned` directly rather than calling the shared `AllocateAligned` helper, because that helper also serves buffers which must not be counted as texture or file data.

#### Recipe: a resource identified by a handle rather than a pointer

Tracy keys a pool entry on an address. For something named by an integer handle - an OpenAL buffer, say - do not cast the handle to a pointer: the API may hand the same name out again after deletion, colliding with an entry Tracy still holds live. Key on a stable address the handle lives at instead, and keep enough state to know whether that key is currently live, because Tracy has no realloc and a refill must free the previous size first. `COggData` (`source/soundmanager/data/OggData.cpp`) keys on the address of each `m_Buffer` slot and tracks live bytes per slot, releasing every one of them before the object dies.

#### Memory telemetry plots

Pools show live bytes; plots show a value over time. The two are complementary, not redundant:
- `JS Heap Size`, `JS GC Chunk Bytes` (`source/scriptinterface/Context.cpp`): SpiderMonkey's `JSGC_BYTES` and `JSGC_CHUNK_BYTES`, emitted from the GC slice callback. Correlate the drops with the GC zones to tell real growth from collection lag.
- `VFS Loaded Bytes` (`source/lib/file/vfs/vfs.cpp`): cumulative bytes ever loaded, as against the `VFS FileBuffer` pool's currently-live view.

### 4.7 Application Metadata & Program Name

```cpp
TRACY_SET_PROGRAM_NAME("0 A.D. (Pyrogenesis)");
TRACY_APP_INFO(PS_VERSION, strlen(PS_VERSION));
```

### 4.8 SpiderMonkey JavaScript Profiling

JavaScript profiling calls (`ProfileStart`, `ProfileStop`, `ProfileAttribute`) from game simulation scripts and GUI scripts are bridged to Tracy dynamic transient zones via thread-local stack tracking. This allows fine-grained JS functions and component logic to appear directly nested in the Tracy timeline with script names and attributes.

---

## 5. Navigating Memory & Lock Contention in Tracy GUI

### 5.1 Analyzing Memory Allocations
1. In the Tracy GUI top navigation bar, click **Memory**.
2. The memory breakdown window displays:
   - **Allocations Overview**: Total active memory, peak high-water mark, and allocation count.
   - **Named Allocator Filter**: Use the dropdown/search to filter by pool - `"PS::Memory::LinearAllocator"`, `"Allocators::DynamicArena"`, `"Allocators::Pool"`, `"VFS FileBuffer"`, `"Renderer Geometry"`, `"Renderer Textures"` or `"Audio Buffers"`.
   - **Bottom Timeline**: Highlights memory consumption spikes over time correlated with frame execution and simulation turns.

### 5.2 Analyzing Lock Contention & Mutex Wait Times
1. In the Tracy GUI top navigation bar, click **Locks**.
2. The lock contention viewer lists all instrumented mutexes:
   - **Wait Time vs Hold Time**: Identifies locks where threads spend significant time blocked waiting for acquisition.
   - **Contention Events**: Visualizes exact moments where multiple worker threads competed for the same lock (e.g. `TaskManager NormalQueue` during turn dispatch).
   - **Thread Blocking Graph**: Shows which thread is holding the lock while other threads are blocked.

### 5.3 Analyzing SpiderMonkey GC Telemetry
1. Under the **Plots** section on the main timeline:
   - Expand `"JS Heap Size"` and `"JS GC Chunk Bytes"`.
   - Correlate garbage collection drops with GC slice zones (`"JS GC Callback"`) to detect memory leaks and JS allocation hotspots during gameplay.

---

## 6. Configuration Defines

The following preprocessor defines are managed automatically by Premake when `--with-tracy` is supplied:

| Define | Purpose |
|---|---|
| `TRACY_ENABLE=1` | Activates Tracy client instrumentation. |
| `TRACY_ON_DEMAND=1` | Keeps client dormant until Tracy GUI connects (no memory buffer bloat while disconnected). |
| `TRACY_DELAYED_INIT=1` & `TRACY_MANUAL_LIFETIME=1` | Ties profiler lifecycle cleanly to `CProfiler2::Initialise()` / `CProfiler2::Shutdown()`. |
| `TRACY_NO_SYSTEM_TRACING=1` | Disables Windows ETW kernel logger requirements so standard non-admin users/test harnesses run without permission prompts or stalls. |
| `TRACY_NO_CALLSTACK=1` | Prevents blocking kernel device driver symbol enumeration on Windows during startup. |
