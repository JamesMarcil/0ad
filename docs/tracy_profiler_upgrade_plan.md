# Plan: Tracy Profiler Upgrade to Latest Version (v0.14.0)

## Executive Summary

The purpose of this initiative is to upgrade the vendored **Tracy Profiler** client library in 0 A.D. (Pyrogenesis engine) from version **v0.11.1** to the latest stable release (**v0.14.0**).

Tracy profiler protocol versions are strictly matched between client and server (GUI). Upgrading to v0.14.0 brings significant performance improvements, enhanced callstack sampling reconstruction, multi-process trace merging (`tracy-merge`), high-level execution section markers (`TracySectionEnter`/`TracySectionLeave`), UDP discovery daemon support, and modernized client headers.

This plan defines an atomic, incremental, and verifiable roadmap for upgrading the Tracy client library while ensuring zero regressions across all build configurations (MSVC 2022 Win32/x64, GCC/Clang) and test suites.

---

## Key Changes & Enhancements in Tracy v0.14.0

1. **Section Demarcation**: High-level game phase tracking (`TracySectionEnter` / `TracySectionLeave`) suitable for simulation phases, loading stages, and game match states.
2. **Heuristic Callstack Reconstruction**: Zone stack reconstruction without requiring synchronous OS symbol queries.
3. **Multi-Process & Capture Tools**: Compatibility with `tracy-capture-daemon` and `tracy-merge`.
4. **Updated Client Protocol & Networking**: Optimized protocol buffers and reduced network streaming overhead.
5. **Timeline & Memory Visualization**: Color-coded timeline markers (kernel, library, user) and improved memory tracking views.

---

## Upgrade Roadmap & Phased Execution

```mermaid
graph TD
    subgraph Phase1 ["Phase 1: Source Preparation & Vendoring"]
        P1A["Fetch Tracy v0.14.0 Release Source"] --> P1B["Replace source/third_party/tracy/"]
        P1B --> P1C["Update README.md & License Documentation"]
    end

    subgraph Phase2 ["Phase 2: Build System & Premake Alignment"]
        P2A["Verify Premake5 include paths & source lists"] --> P2B["Verify Windows/POSIX library dependencies"]
        P2B --> P2C["Update Configuration Defines for v0.14.0"]
    end

    subgraph Phase3 ["Phase 3: Client Abstraction & API Compatibility"]
        P3A["Review ProfileTracy.h against v0.14.0 Macro APIs"] --> P3B["Update Delayed Init & Manual Lifetime bindings"]
        P3B --> P3C["Integrate New Section Tracking (Optional)"]
    end

    subgraph Phase4 ["Phase 4: Multi-Configuration Compilation & Testing"]
        P4A["Build Win32 Debug (pyrogenesis_dbg, test_dbg)"] --> P4B["Run cxxtest Suite (test_dbg.exe)"]
        P4B --> P4C["Build Win32 Release (pyrogenesis, test)"]
        P4C --> P4D["Run cxxtest Suite (test.exe)"]
    end

    subgraph Phase5 ["Phase 5: Live Profiler Validation & Documentation"]
        P5A["Connect Tracy GUI v0.14.0 to Running Client"] --> P5B["Verify Timeline, Zones, Plots, and Thread Names"]
        P5B --> P5C["Update docs/tracy_profiler_guide.md"]
        P5C --> P5D["Final Commit & Review Checklist"]
    end

    Phase1 --> Phase2 --> Phase3 --> Phase4 --> Phase5
```

---

## Detailed Step-by-Step Breakdown

### Phase 1: Source Preparation & Client Vendoring

**Goal**: Cleanly vendor Tracy v0.14.0 client headers and implementation files into `source/third_party/tracy/`.

- [ ] **Step 1.1: Download and Extract Tracy v0.14.0 Source**
  - Download official release tag `v0.14.0` from `https://github.com/wolfpld/tracy`.
  - Extract client sources:
    - `public/tracy/` $\rightarrow$ `source/third_party/tracy/include/tracy/`
    - `public/client/` $\rightarrow$ `source/third_party/tracy/include/client/`
    - `public/common/` $\rightarrow$ `source/third_party/tracy/include/common/`
    - `public/libbacktrace/` $\rightarrow$ `source/third_party/tracy/include/libbacktrace/`
    - `public/TracyClient.cpp` $\rightarrow$ `source/third_party/tracy/src/TracyClient.cpp`
- [ ] **Step 1.2: Update Vendoring Documentation**
  - Update `source/third_party/tracy/README.md` to reference `v0.14.0` and upstream release date.
  - Verify license file `source/third_party/tracy/LICENSE` matches 3-Clause BSD.

---

### Phase 2: Build System & Premake Configuration Alignment

**Goal**: Ensure Premake scripts correctly discover all v0.14.0 source files, headers, and platform dependencies.

- [ ] **Step 2.1: Audit Premake Configuration**
  - Inspect `build/premake/premake5.lua` and `build/premake/extern_libs5.lua`.
  - Confirm static library project `tracy` compiles `source/third_party/tracy/src/TracyClient.cpp`.
  - Verify linked system libraries (`ws2_32`, `dbghelp`, `advapi32`, `user32` on Windows; `pthread`, `dl` on Linux).
- [ ] **Step 2.2: Review Preprocessor Defines for v0.14.0**
  - Ensure compatibility of defines:
    - `TRACY_ENABLE=1`
    - `TRACY_ON_DEMAND=1`
    - `TRACY_DELAYED_INIT=1`
    - `TRACY_MANUAL_LIFETIME=1`
    - `TRACY_NO_SYSTEM_TRACING=1`
    - `TRACY_NO_CALLSTACK=1`
  - Check for any new or renamed configuration macros in v0.14.0.
- [ ] **Step 2.3: Regenerate Workspace Files**
  - Run `build/workspaces/update-workspaces.bat --with-tracy`.

---

### Phase 3: Abstraction Layer & API Compatibility

**Goal**: Verify and adapt `source/ps/ProfileTracy.h` and engine hooks to any API changes in v0.14.0.

- [ ] **Step 3.1: Audit Macro Abstractions in `source/ps/ProfileTracy.h`**
  - Verify macro definitions:
    - `ZoneScoped`, `ZoneScopedN`, `ZoneScopedNC`
    - `FrameMark`, `FrameMarkNamed`
    - `tracy::SetThreadName`
    - `TracyPlot`, `TracyPlotConfig`
    - `TracyAlloc`, `TracyFree`
    - `TracyLockable`, `LockableBase`
- [ ] **Step 3.2: Verify Lifecycle Hooks**
  - Confirm `tracy::StartupProfiler()` and `tracy::ShutdownProfiler()` function signatures and behavior in `source/ps/Profiler2.cpp`.
  - Ensure idempotency and safe delayed initialization.
- [ ] **Step 3.3: (Optional) Integrate New Section Tracking**
  - Expose `TRACY_SECTION_ENTER(name)` / `TRACY_SECTION_LEAVE(name)` in `ProfileTracy.h` for high-level match phases and loading sequences.

---

### Phase 4: Compilation & Unit Test Verification

**Goal**: Verify zero compiler warnings, zero errors, and 100% passing test suite across configurations.

- [ ] **Step 4.1: Build Win32 Debug**
  - Build `build/workspaces/vs2022/pyrogenesis.sln` in `Debug` `Win32`.
  - Verify `tracy_dbg.lib`, `pyrogenesis_dbg.exe`, and `test_dbg.exe` build cleanly.
- [ ] **Step 4.2: Execute Debug Test Suite**
  - Run `binaries/system/test_dbg.exe`.
  - Confirm all 477+ tests pass (`OK!`) with zero exit delays or deadlocks.
- [ ] **Step 4.3: Build Win32 Release**
  - Build `build/workspaces/vs2022/pyrogenesis.sln` in `Release` `Win32`.
  - Verify `tracy.lib`, `pyrogenesis.exe`, and `test.exe` build cleanly.
- [ ] **Step 4.4: Execute Release Test Suite**
  - Run `binaries/system/test.exe`.
  - Confirm all 477+ tests pass (`OK!`).

---

### Phase 5: Live GUI Verification & Documentation

**Goal**: Validate end-to-end communication with Tracy GUI v0.14.0 and update developer guides.

- [ ] **Step 5.1: Live Profiling Verification**
  - Launch `Tracy.exe` (v0.14.0).
  - Launch `pyrogenesis.exe` or `test.exe`.
  - Connect GUI and verify:
    - Main thread, TaskManager workers, Sound, and Net threads appear with correct names.
    - Continuous frame rate timeline and `SimulationTurn` / `RenderFrame` marks.
    - Real-time metric plots (`Draw Calls`, `Terrain Tris`, `Particles`, etc.) populate graphs.
    - Hierarchical flamegraphs for simulation and rendering hot loops display accurately.
- [ ] **Step 5.2: Update Documentation**
  - Update `docs/tracy_profiler_guide.md` with v0.14.0 GUI download instructions and feature notes.
  - Update `docs/README.md` if necessary.

---

## Review & Approval Protocol

In accordance with project guidelines:
1. This plan **must be formally reviewed and approved** before enacting code changes.
2. Once approved, the plan file `docs/tracy_profiler_upgrade_plan.md` will be committed to the branch.
3. Code changes will then be implemented following the atomic phases described above.
4. Each phase will be compiled, tested against the test suite, and committed incrementally.
