#pragma once


#include "lumix.h"
#include "core/array.h"
#include "core/vec3.h"


namespace Lumix
{


class ParticleEmitter;
class IAllocator;
class Material;
class Universe;


struct Interval
{
	float from;
	float to;


	Interval();
	float getRandom() const;


	void check();

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


class LUMIX_RENDERER_API ParticleEmitter
{
public:
	struct LUMIX_RENDERER_API ModuleBase
	{
		ModuleBase(ParticleEmitter& emitter);

		virtual ~ModuleBase() {}
		virtual void spawnParticle(int index) {}
		virtual void destoryParticle(int index) {}
		virtual void update(float time_delta) {}
		virtual uint32_t getType() const = 0;

		ParticleEmitter& m_emitter;
	};


	struct LUMIX_RENDERER_API LinearMovementModule : public ModuleBase
	{
		LinearMovementModule(ParticleEmitter& emitter);
		void spawnParticle(int index) override;
		uint32_t getType() const override { return s_type; }

		static const uint32_t s_type;
		Interval m_x;
		Interval m_y;
		Interval m_z;
	};


public:
	ParticleEmitter(Entity entity, Universe& universe, IAllocator& allocator);
	~ParticleEmitter();

	void update(float time_delta);
	Material* getMaterial() const { return m_material; }
	void setMaterial(Material* material);
	IAllocator& getAllocator() { return m_allocator; }
	void addModule(ModuleBase* module);

public:
	Array<float> m_life;
	Array<float> m_size;
	Array<Vec3> m_position;
	Array<Vec3> m_velocity;

	Interval m_spawn_period;
	Interval m_initial_life;
	Interval m_initial_size;
	Array<ModuleBase*> m_modules;
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
	Material* m_material;
};


} // namespace Lumix