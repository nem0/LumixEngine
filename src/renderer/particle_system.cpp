#include "particle_system.h"
#include "core/array.h"
#include "core/vec3.h"
#include "universe/universe.h"
#include <bgfx/bgfx.h>


namespace Lumix
{


namespace Particles
{


struct Interval
{
	float from;
	float to;


	Interval()
		: from(0)
		, to(0)
	{
	}


	float getRandom() const
	{
		return from + rand() * ((to - from) / RAND_MAX);
	}
};


struct Emitter;


struct ModuleBase
{
	ModuleBase(Emitter& emitter)
		: m_emitter(emitter)
	{
	}

	virtual ~ModuleBase() {}
	virtual void spawnParticle(int index) {}
	virtual void destoryParticle(int index) {}
	virtual void update(float time_delta) {}

	Emitter& m_emitter;
};


struct Emitter
{
	IAllocator& m_allocator;
	Entity m_entity;
	float m_next_spawn_time;
	Interval m_spawn_period;
	Interval m_initial_life;
	Universe& m_universe;

	Array<float> m_life;
	Array<float> m_size;
	Array<Vec3> m_position;
	Array<Vec3> m_velocity;

	Array<ModuleBase*> m_modules;

	Emitter(Entity entity, Universe& universe, IAllocator& allocator)
		: m_next_spawn_time(0)
		, m_allocator(allocator)
		, m_life(allocator)
		, m_modules(allocator)
		, m_position(allocator)
		, m_velocity(allocator)
		, m_universe(universe)
		, m_entity(entity)
	{
		m_spawn_period.from = 1;
		m_spawn_period.to = 2;
		m_initial_life.from = 1;
		m_initial_life.to = 2;
	}


	~Emitter()
	{
		for(auto* module : m_modules)
		{
			LUMIX_DELETE(m_allocator, module);
		}
	}


	void spawnParticle()
	{
		m_position.push(m_universe.getPosition(m_entity));
		m_life.push(m_initial_life.getRandom());
		m_velocity.push(Vec3(0, 0, 0));
		m_size.push(1);
		for(auto* module : m_modules)
		{
			module->spawnParticle(m_life.size() - 1);
		}
	}


	void destroyParticle(int index)
	{
		for(auto* module : m_modules)
		{
			module->destoryParticle(index);
		}
		m_life.eraseFast(index);
		m_position.eraseFast(index);
		m_velocity.eraseFast(index);
		m_size.eraseFast(index);
	}


	void updateLives(float time_delta)
	{
		for(int i = 0, c = m_life.size(); i < c; ++i)
		{
			float life = m_life[i];
			life -= time_delta;
			m_life[i] = life;

			if(life < 0)
			{
				destroyParticle(i);
				--i;
				--c;
			}
		}
	}


	void updatePositions(float time_delta)
	{
		for(int i = 0, c = m_position.size(); i < c; ++i)
		{
			m_position[i] = m_velocity[i] * time_delta;
		}
	}


	void update(float time_delta)
	{
		spawnParticles(time_delta);
		updateLives(time_delta);
		updatePositions(time_delta);
		for(auto* module : m_modules)
		{
			module->update(time_delta);
		}
	}


	void render()
	{
		ASSERT(false);
		TODO("todo");
	}


	void spawnParticles(float time_delta)
	{
		m_next_spawn_time -= time_delta;

		while(m_next_spawn_time < 0)
		{
			m_next_spawn_time += m_spawn_period.getRandom();

			spawnParticle();
		}
	}
};


struct System
{
	System(IAllocator& allocator)
		: m_allocator(allocator)
		, m_emitters(allocator)
	{
	}


	IAllocator& m_allocator;
	Lumix::Array<Emitter*> m_emitters;
};


static System* g_system = nullptr;


void init(IAllocator& allocator)
{
	ASSERT(!g_system);

	g_system = LUMIX_NEW(allocator, System)(allocator);
}


void shutdown()
{
	ASSERT(g_system);

	LUMIX_DELETE(g_system->m_allocator, g_system);
	g_system = nullptr;
}


EmitterHandle createEmitter(Entity entity, Universe& universe)
{
	EmitterHandle handle = -1;
	for(int i = 0; i < g_system->m_emitters.size(); ++i)
	{
		if(!g_system->m_emitters[i])
		{
			handle = i;
			break;
		}
	}

	if(handle < 0)
	{
		handle = g_system->m_emitters.size();
		g_system->m_emitters.push(nullptr);
	}

	g_system->m_emitters[handle] = LUMIX_NEW(g_system->m_allocator, Emitter)(entity, universe, g_system->m_allocator);

	return handle;
}


void destroyEmitter(EmitterHandle emitter)
{
	LUMIX_DELETE(g_system->m_allocator, g_system->m_emitters[emitter]);
	g_system->m_emitters[emitter] = nullptr;
}


void render(EmitterHandle emitter_handle)
{
	Emitter& emitter = *g_system->m_emitters[emitter_handle];
	emitter.render();
}


void update(EmitterHandle emitter_handle, float time_delta)
{
	Emitter& emitter = *g_system->m_emitters[emitter_handle];
	emitter.update(time_delta);
}


struct InitialVelocityModule : public ModuleBase
{
	Interval m_x;
	Interval m_y;
	Interval m_z;

	InitialVelocityModule(Emitter& emitter)
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


} // namespace Particles


} // namespace Lumix
