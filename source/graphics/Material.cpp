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

#include "Material.h"

#include "ps/CStrInternStatic.h"

class CVector4D;

CMaterial::CMaterial() :
	m_AlphaBlending(false)
{
}

CStrIntern CMaterial::GetShaderEffect(const Pass pass) const
{
	ENSURE(pass < Pass::COUNT);
	return m_ShaderEffects[static_cast<uint8_t>(pass)];
}

void CMaterial::SetShaderEffect(const Pass pass, CStrIntern effect)
{
	ENSURE(pass < Pass::COUNT);
	m_ShaderEffects[static_cast<uint8_t>(pass)] = effect;
}

void CMaterial::AddShaderDefine(CStrIntern key, CStrIntern value)
{
	m_ShaderDefines.Add(key, value);
}

void CMaterial::AddStaticUniform(const char* key, const CVector4D& value)
{
	m_StaticUniforms.Add(key, value);
}

void CMaterial::AddSampler(const TextureSampler& texture)
{
	m_Samplers.push_back(texture);
	if (texture.Name == str_baseTex)
		m_DiffuseTexture = texture.Sampler;
}

void CMaterial::AddRenderQuery(const char* key)
{
	m_RenderQueries.Add(key);
}

void CMaterial::AddRequiredSampler(RequiredSampler sampler)
{
	m_RequiredSamplers.emplace_back(std::move(sampler));
}
