#pragma once


#include "lumix.h"
#include "core/array.h"
#include "core/vec3.h"


namespace Lumix
{


class ParticleEmitter;
class IAllocator;
class Universe;


struct Interval
{
	float from;
	float to;


	Interval();
	float getRandom() const;

	void operator=(const Vec2& value)
	{
		from = value.x;
		to = value.y;
	}

	operator Vec2()
	{
		return Vec2(from, to);
	}
};


class ParticleEmitter
{
public:
	struct ModuleBase;

public:
	ParticleEmitter(Entity entity, Universe& universe, IAllocator& allocator);
	~ParticleEmitter();

	void update(float time_delta);
	void render();

public:
	Array<float> m_life;
	Array<float> m_size;
	Array<Vec3> m_position;
	Array<Vec3> m_velocity;

	Interval m_spawn_period;
	Interval m_initial_life;
	Entity m_entity;

private:
	void spawnParticle();
	void destroyParticle(int index);
	void spawnParticles(float time_delta);
	void updateLives(float time_delta);
	void updatePositions(float time_delta);

private:
	IAllocator& m_allocator;
	float m_next_spawn_time;
	Universe& m_universe;
	Array<ModuleBase*> m_modules;
};


} // namespace Lumix