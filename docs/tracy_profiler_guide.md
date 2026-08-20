# Tracy Profiler Integration Guide for 0 A.D.

## Introduction

0 A.D. integrates the **Tracy Profiler** (v0.14.x) — a real-time, nanosecond-resolution, frame-oriented hybrid profiler capable of capturing CPU timelines, multi-thread execution, GPU workload demarcation, memory allocations, lock contention, and real-time numerical telemetry.

This guide explains how to configure, build, run, and instrument 0 A.D. with Tracy support.

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

## 3. Macro Reference & Usage

All Tracy instrumentation in 0 A.D. is abstracted through `source/ps/ProfileTracy.h`. When compiled without `--with-tracy`, all macros cleanly expand to no-ops.

### Scope Profiling (Zones)

```cpp
#include "ps/ProfileTracy.h"

void ProcessComplexEntity(CEntity* entity)
{
    TRACY_ZONE("ProcessComplexEntity");
    // ... function work ...
}

void ProcessStages()
{
    {
        TRACY_ZONE("Stage 1 - Setup");
        // ... stage 1 work ...
    }
    {
        TRACY_ZONE("Stage 2 - Computation");
        // ... stage 2 work ...
    }
}
```

Existing engine macros (`PROFILE`, `PROFILE2`, `PROFILE3`) are bridged and forward to Tracy automatically.

### Frame Demarcation

- `TRACY_FRAME_MARK()`: Marks the end of a primary visual engine frame.
- `TRACY_FRAME_MARK_NAMED("SimulationTurn")`: Marks secondary frames/phases (e.g., discrete simulation turns, renderer submissions).

### Thread Registration

- `TRACY_SET_THREAD_NAME("Worker Thread Name")`: Assigns human-readable names to threads in the Tracy GUI timeline. Handled automatically for all engine threads registered via `CProfiler2::RegisterCurrentThread(name)`.

### Real-Time Metric Telemetry (Plots)

```cpp
TRACY_PLOT("Draw Calls", (int64_t)stats.m_DrawCalls);
TRACY_PLOT("Entity Count", (int64_t)entityCount);
TRACY_PLOT("Memory Usage (KB)", (double)usedMemoryKB);
```

### Memory Allocation Tracking

```cpp
TRACY_ALLOC(pointer, sizeInBytes);
TRACY_FREE(pointer);
```

---

## 4. Configuration Defines

The following preprocessor defines are managed automatically by Premake when `--with-tracy` is supplied:

| Define | Purpose |
|---|---|
| `TRACY_ENABLE=1` | Activates Tracy client instrumentation. |
| `TRACY_ON_DEMAND=1` | Keeps client dormant until Tracy GUI connects (no memory buffer bloat while disconnected). |
| `TRACY_DELAYED_INIT=1` & `TRACY_MANUAL_LIFETIME=1` | Ties profiler lifecycle cleanly to `CProfiler2::Initialise()` / `CProfiler2::Shutdown()`. |
| `TRACY_NO_SYSTEM_TRACING=1` | Disables Windows ETW kernel logger requirements so standard non-admin users/test harnesses run without permission prompts or stalls. |
| `TRACY_NO_CALLSTACK=1` | Prevents blocking kernel device driver symbol enumeration on Windows during startup. |
