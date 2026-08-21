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

#ifndef INCLUDED_ENTTCONFIG
#define INCLUDED_ENTTCONFIG

/**
 * Master preprocessor switch for EnTT ECS modernization.
 * When enabled (1), enables modern EnTT-backed implementations for simulation subsystems.
 * Can be controlled globally via Premake: premake5 --with-entt-ecs
 */
#ifndef CONFIG_ENABLE_ENTT_ECS
#define CONFIG_ENABLE_ENTT_ECS 0
#endif

/**
 * Subsystem-level granular feature flags.
 * Each flag defaults to the value of CONFIG_ENABLE_ENTT_ECS unless explicitly overridden
 * in compiler definitions or build configuration.
 */

// 1. Core Entity Registry & Component Lifecycle Storage
#ifndef CONFIG_ENTT_ENTITY_REGISTRY
#define CONFIG_ENTT_ENTITY_REGISTRY CONFIG_ENABLE_ENTT_ECS
#endif

// 2. High-Performance Message Dispatch & Signal Routing
#ifndef CONFIG_ENTT_MESSAGE_DISPATCH
#define CONFIG_ENTT_MESSAGE_DISPATCH CONFIG_ENABLE_ENTT_ECS
#endif

// 3. Spatial Partitioning & Range Manager Contiguous View Storage
#ifndef CONFIG_ENTT_SPATIAL_STORAGE
#define CONFIG_ENTT_SPATIAL_STORAGE CONFIG_ENABLE_ENTT_ECS
#endif

// 4. Unit Motion & Kinematic Batch Integration
#ifndef CONFIG_ENTT_UNIT_MOTION
#define CONFIG_ENTT_UNIT_MOTION CONFIG_ENABLE_ENTT_ECS
#endif

// 5. Unit Renderer Interpolation & Submission Batching
#ifndef CONFIG_ENTT_RENDER_SUBMIT
#define CONFIG_ENTT_RENDER_SUBMIT CONFIG_ENABLE_ENTT_ECS
#endif

#if CONFIG_ENABLE_ENTT_ECS
#include <entt/entt.hpp>
static_assert(ENTT_VERSION_MAJOR >= 4, "EnTT v4.0.0 or higher is required for EnTT modernization features");
#endif

#endif // INCLUDED_ENTTCONFIG
