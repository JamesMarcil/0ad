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

#include "precompiled.h"

#include "ParticleManager.h"

#include "ps/Filesystem.h"
#include "ps/Profile.h"
#include "renderer/backend/IDevice.h"
#include "renderer/Scene.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

class CFrustum;

static Status ReloadChangedFileCB(void* param, const VfsPath& path)
{
	return static_cast<CParticleManager*>(param)->ReloadChangedFile(path);
}

CParticleManager::CParticleManager(Renderer::Backend::IDevice& device)
	: m_UseInstancing{device.GetCapabilities().instancing}
{
	RegisterFileReloadFunc(ReloadChangedFileCB, this);
}

CParticleManager::~CParticleManager()
{
	UnregisterFileReloadFunc(ReloadChangedFileCB, this);
}

CParticleEmitterType* CParticleManager::LoadEmitterType(const VfsPath& path)
{
	if (auto it{m_EmitterTypes.find(path)}; it != m_EmitterTypes.end())
		return it->second.get();

	std::unique_ptr<CParticleEmitterType> emitterType{
		std::make_unique<CParticleEmitterType>(*this)};
	if (!emitterType->Load(path))
		return nullptr;

	return m_EmitterTypes.emplace(path, std::move(emitterType)).first->second.get();
}

void CParticleManager::AddUnattachedEmitter(std::unique_ptr<CParticleEmitter> emitter)
{
	emitter->m_Active = false;
	m_UnattachedEmitters.emplace_back(std::move(emitter));
}

void CParticleManager::ClearUnattachedEmitters()
{
	m_UnattachedEmitters.clear();
}

void CParticleManager::Interpolate(const float simFrameLength)
{
	m_CurrentTime += simFrameLength;
}

struct ParticleAlive
{
	bool operator()(const SParticle& particle) const
	{
		return particle.age < particle.maxAge;
	}
};

struct EmitterHasNoParticles
{
	bool operator()(const std::unique_ptr<CParticleEmitter>& emitter) const
	{
		return std::none_of(emitter->m_Particles.begin(), emitter->m_Particles.end(), ParticleAlive{});
	}
};

void CParticleManager::RenderSubmit(SceneCollector& collector, const CFrustum&)
{
	PROFILE("submit unattached particles");

	// Delete any unattached emitters that have no particles left
	std::erase_if(m_UnattachedEmitters, EmitterHasNoParticles());

	// TODO: should do some frustum culling

	for (std::unique_ptr<CParticleEmitter>& emitter : m_UnattachedEmitters)
		collector.Submit(emitter.get());
}

Status CParticleManager::ReloadChangedFile(const VfsPath& path)
{
	if (auto it = m_EmitterTypes.find(path); it != m_EmitterTypes.end())
	{
		// We can't erase an emitter type because emitters might hold
		// a reference to it.
		it->second->Load(path);
	}
	return INFO::OK;
}
