# Superluminal Performance API

This directory contains headers for the Superluminal Performance API, version 3.0.

## Source

Vendored from: C:\Program Files\Superluminal\Performance\API\include\Superluminal\

## Implementation Notes

- This is a header-only integration using dynamic DLL loading via the PerformanceAPI_loader interface
- No static library linking required
- PerformanceAPI.dll is resolved at runtime, not shipped with the application
- Only PerformanceAPI_capi.h and PerformanceAPI_loader.h are vendored; PerformanceAPI.h (C++ wrapper) is not used
- The API compiles to no-ops on non-Windows platforms or when CONFIG2_SUPERLUMINAL is not defined
