#include "particle_system.h"
#include "core/math_utils.h"
#include "universe/universe.h"


namespace Lumix
{


struct ParticleEmitter::ModuleBase
{
	ModuleBase(ParticleEmitter& emitter)
		: m_emitter(emitter)
	{
	}

	virtual ~ModuleBase() {}
	virtual void spawnParticle(int index) {}
	virtual void destoryParticle(int index) {}
	virtual void update(float time_delta) {}

	ParticleEmitter& m_emitter;
};


Interval::Interval()
	: from(0)
	, to(0)
{
}


void Interval::check()
{
	from = Math::maxValue(from, 0.0f);
	to = Math::maxValue(from + 0.001f, to);
}


float Interval::getRandom() const
{
	return from + rand() * ((to - from) / RAND_MAX);
}


ParticleEmitter::ParticleEmitter(Entity entity, Universe& universe, IAllocator& allocator)
	: m_next_spawn_time(0)
	, m_allocator(allocator)
	, m_life(allocator)
	, m_modules(allocator)
	, m_position(allocator)
	, m_velocity(allocator)
	, m_universe(universe)
	, m_entity(entity)
	, m_size(allocator)
{
	m_spawn_period.from = 1;
	m_spawn_period.to = 2;
	m_initial_life.from = 1;
	m_initial_life.to = 2;
}


ParticleEmitter::~ParticleEmitter()
{
	for (auto* module : m_modules)
	{
		LUMIX_DELETE(m_allocator, module);
	}
}


void ParticleEmitter::spawnParticle()
{
	m_position.push(m_universe.getPosition(m_entity));
	m_life.push(m_initial_life.getRandom());
	m_velocity.push(Vec3(0, 0, 0));
	m_size.push(1);
	for (auto* module : m_modules)
	{
		module->spawnParticle(m_life.size() - 1);
	}
}


void ParticleEmitter::destroyParticle(int index)
{
	for (auto* module : m_modules)
	{
		module->destoryParticle(index);
	}
	m_life.eraseFast(index);
	m_position.eraseFast(index);
	m_velocity.eraseFast(index);
	m_size.eraseFast(index);
}


void ParticleEmitter::updateLives(float time_delta)
{
	for (int i = 0, c = m_life.size(); i < c; ++i)
	{
		float life = m_life[i];
		life -= time_delta;
		m_life[i] = life;

		if (life < 0)
		{
			destroyParticle(i);
			--i;
			--c;
		}
	}
}


void ParticleEmitter::updatePositions(float time_delta)
{
	for (int i = 0, c = m_position.size(); i < c; ++i)
	{
		m_position[i] = m_velocity[i] * time_delta;
	}
}


void ParticleEmitter::update(float time_delta)
{
	spawnParticles(time_delta);
	updateLives(time_delta);
	updatePositions(time_delta);
	for (auto* module : m_modules)
	{
		module->update(time_delta);
	}
}


void ParticleEmitter::render()
{
	ASSERT(false);
	TODO("todo");
}


void ParticleEmitter::spawnParticles(float time_delta)
{
	m_next_spawn_time -= time_delta;

	while (m_next_spawn_time < 0)
	{
		m_next_spawn_time += m_spawn_period.getRandom();

		spawnParticle();
	}
}


struct InitialVelocityModule : public ParticleEmitter::ModuleBase
{
	Interval m_x;
	Interval m_y;
	Interval m_z;

	InitialVelocityModule(ParticleEmitter& emitter)
		: ModuleBase(emitter)
	{
		m_z.from = -1;
		m_z.to = 1;
		m_x.from = m_x.to = m_x.to = m_y.from = m_y.to = 0;
	}


	void spawnParticle(int index) override
	{
		Vec3 v;
		v.x = m_x.getRandom();
		v.y = m_y.getRandom();
		v.z = m_z.getRandom();
		m_emitter.m_velocity[index] = v;
	}
};


} // namespace Lumix