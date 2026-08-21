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

#ifndef INCLUDED_BENCH_FIXTURES
#define INCLUDED_BENCH_FIXTURES

#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <map>

#include "lib/types.h"
#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "maths/FixedVector3D.h"
#include "simulation2/system/Entity.h"
#include "ps/CStr.h"

namespace BenchmarkFixtures
{

/**
 * Fast, deterministic pseudo-random number generator (xoshiro256**).
 * Guarantees identical sequence across platforms and compiler optimizations.
 */
class DeterministicRng
{
public:
	explicit DeterministicRng(uint64_t seed = 0x853c49e6748fea9bULL)
	{
		SetSeed(seed);
	}

	void SetSeed(uint64_t seed)
	{
		uint64_t z = (seed + 0x9e3779b97f4a7c15ULL);
		for (int i = 0; i < 4; ++i)
		{
			z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
			z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
			m_State[i] = z ^ (z >> 31);
			z += 0x9e3779b97f4a7c15ULL;
		}
	}

	uint64_t NextU64()
	{
		const uint64_t result = RotL(m_State[1] * 5, 7) * 9;
		const uint64_t t = m_State[1] << 17;

		m_State[2] ^= m_State[0];
		m_State[3] ^= m_State[1];
		m_State[1] ^= m_State[2];
		m_State[0] ^= m_State[3];

		m_State[2] ^= t;
		m_State[3] = RotL(m_State[3], 45);

		return result;
	}

	uint32_t NextU32()
	{
		return static_cast<uint32_t>(NextU64());
	}

	uint32_t NextRange(uint32_t minVal, uint32_t maxVal)
	{
		if (minVal >= maxVal)
			return minVal;
		return minVal + (NextU32() % (maxVal - minVal + 1));
	}

	float NextFloat(float minVal = 0.0f, float maxVal = 1.0f)
	{
		float norm = static_cast<float>(NextU32()) / 4294967295.0f;
		return minVal + norm * (maxVal - minVal);
	}

	fixed NextFixed(fixed minVal, fixed maxVal)
	{
		float f = NextFloat(minVal.ToFloat(), maxVal.ToFloat());
		return fixed::FromFloat(f);
	}

private:
	static inline uint64_t RotL(const uint64_t x, int k)
	{
		return (x << k) | (x >> (64 - k));
	}

	uint64_t m_State[4];
};

/**
 * Representation of entity spatial positioning for synthetic range/motion benchmarks.
 */
struct SyntheticEntity
{
	entity_id_t id;
	CFixedVector2D pos;
	CFixedVector2D velocity;
	fixed heading;
	fixed radius;
	u32 player;
	u8 flags;
};

/**
 * Utility to generate synthetic spatial distributions of entities.
 */
class SyntheticGridGenerator
{
public:
	static std::vector<SyntheticEntity> GenerateUniformGrid(size_t count, fixed worldSize)
	{
		std::vector<SyntheticEntity> entities;
		entities.reserve(count);

		size_t side = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
		fixed step = worldSize / fixed::FromInt(static_cast<int>(side + 1));

		DeterministicRng rng(0x12345678ULL);

		for (size_t i = 0; i < count; ++i)
		{
			size_t row = i / side;
			size_t col = i % side;

			fixed x = step.Multiply(fixed::FromInt(static_cast<int>(col + 1))) + rng.NextFixed(fixed::FromFloat(-0.5f), fixed::FromFloat(0.5f));
			fixed z = step.Multiply(fixed::FromInt(static_cast<int>(row + 1))) + rng.NextFixed(fixed::FromFloat(-0.5f), fixed::FromFloat(0.5f));

			SyntheticEntity ent;
			ent.id = static_cast<entity_id_t>(i + 100);
			ent.pos = CFixedVector2D(x, z);
			ent.velocity = CFixedVector2D(rng.NextFixed(fixed::FromFloat(-2.0f), fixed::FromFloat(2.0f)),
			                              rng.NextFixed(fixed::FromFloat(-2.0f), fixed::FromFloat(2.0f)));
			ent.heading = rng.NextFixed(fixed::Zero(), fixed::FromFloat(6.2831853f));
			ent.radius = fixed::FromFloat(1.5f);
			ent.player = static_cast<u32>((i % 4) + 1);
			ent.flags = 0x01; // Normal

			entities.push_back(ent);
		}

		return entities;
	}

	static std::vector<SyntheticEntity> GenerateClusteredSwarm(size_t count, fixed worldSize, size_t clusterCount = 4)
	{
		std::vector<SyntheticEntity> entities;
		entities.reserve(count);

		DeterministicRng rng(0x98765432ULL);

		std::vector<CFixedVector2D> clusterCenters;
		for (size_t c = 0; c < clusterCount; ++c)
		{
			clusterCenters.emplace_back(rng.NextFixed(worldSize.Multiply(fixed::FromFloat(0.2f)), worldSize.Multiply(fixed::FromFloat(0.8f))),
			                           rng.NextFixed(worldSize.Multiply(fixed::FromFloat(0.2f)), worldSize.Multiply(fixed::FromFloat(0.8f))));
		}

		for (size_t i = 0; i < count; ++i)
		{
			const CFixedVector2D& center = clusterCenters[i % clusterCount];
			fixed offsetX = rng.NextFixed(fixed::FromFloat(-15.0f), fixed::FromFloat(15.0f));
			fixed offsetZ = rng.NextFixed(fixed::FromFloat(-15.0f), fixed::FromFloat(15.0f));

			SyntheticEntity ent;
			ent.id = static_cast<entity_id_t>(i + 100);
			ent.pos = center + CFixedVector2D(offsetX, offsetZ);
			ent.velocity = CFixedVector2D(rng.NextFixed(fixed::FromFloat(-1.5f), fixed::FromFloat(1.5f)),
			                              rng.NextFixed(fixed::FromFloat(-1.5f), fixed::FromFloat(1.5f)));
			ent.heading = rng.NextFixed(fixed::Zero(), fixed::FromFloat(6.2831853f));
			ent.radius = fixed::FromFloat(1.2f);
			ent.player = static_cast<u32>((i % clusterCount) + 1);
			ent.flags = 0x01;

			entities.push_back(ent);
		}

		return entities;
	}
};

} // namespace BenchmarkFixtures

#endif // INCLUDED_BENCH_FIXTURES
