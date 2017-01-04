#pragma once


#include "engine/lumix.h"
#include "engine/array.h"
#include "engine/vec.h"


namespace Lumix
{


class ParticleEmitter;
class IAllocator;
class InputBlob;
class Material;
class OutputBlob;
class RenderScene;
class ResourceManager;
class Universe;
class WorldEditor;


struct IntInterval
{
	int from;
	int to;

	IntInterval();
	int getRandom() const;
};


struct Interval
{
	float from;
	float to;


	Interval();
	float getRandom() const;

	void check();
	void checkZero();

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
		explicit ModuleBase(ParticleEmitter& emitter);

		virtual ~ModuleBase() {}
		virtual void spawnParticle(int /*index*/) {}
		virtual void destoryParticle(int /*index*/) {}
		virtual void update(float /*time_delta*/) {}
		virtual void serialize(OutputBlob& blob) = 0;
		virtual void deserialize(InputBlob& blob) = 0;
		virtual ComponentType getType() const = 0;
		virtual void drawGizmo(WorldEditor& editor, RenderScene& scene) {}

		ParticleEmitter& m_emitter;
	};


	struct LUMIX_RENDERER_API SubimageModule LUMIX_FINAL : public ModuleBase
	{
		explicit SubimageModule(ParticleEmitter& emitter);
		void serialize(OutputBlob& blob) override;
		void deserialize(InputBlob& blob) override;
		ComponentType getType() const override { return s_type; }
		static const ComponentType s_type;
		
		int rows;
		int cols;
	};


	struct LUMIX_RENDERER_API SpawnShapeModule LUMIX_FINAL : public ModuleBase
	{
		explicit SpawnShapeModule(ParticleEmitter& emitter);
		void spawnParticle(int index) override;
		void serialize(OutputBlob& blob) override;
		void deserialize(InputBlob& blob) override;
		ComponentType getType() const override { return s_type; }

		static const ComponentType s_type;
		enum Shape : u8
		{
			SPHERE
		};
		float m_radius;
		Shape m_shape;
	};


	struct LUMIX_RENDERER_API LinearMovementModule LUMIX_FINAL : public ModuleBase
	{
		explicit LinearMovementModule(ParticleEmitter& emitter);
		void spawnParticle(int index) override;
		void serialize(OutputBlob& blob) override;
		void deserialize(InputBlob& blob) override;
		ComponentType getType() const override { return s_type; }

		static const ComponentType s_type;
		Interval m_x;
		Interval m_y;
		Interval m_z;
	};


	struct LUMIX_RENDERER_API PlaneModule LUMIX_FINAL : public ModuleBase
	{
		explicit PlaneModule(ParticleEmitter& emitter);
		void serialize(OutputBlob& blob) override;
		void deserialize(InputBlob& blob) override;
		void update(float time_delta) override;
		ComponentType getType() const override { return s_type; }
		void drawGizmo(WorldEditor& editor, RenderScene& scene) override;

		static const ComponentType s_type;
		Entity m_entities[8];
		float m_bounce;
		int m_count;
	};


	struct LUMIX_RENDERER_API AttractorModule LUMIX_FINAL : public ModuleBase
	{
		explicit AttractorModule(ParticleEmitter& emitter);
		void serialize(OutputBlob& blob) override;
		void deserialize(InputBlob& blob) override;
		void update(float time_delta) override;
		ComponentType getType() const override { return s_type; }
		void drawGizmo(WorldEditor& editor, RenderScene& scene) override;

		static const ComponentType s_type;
		Entity m_entities[8];
		float m_force;
		int m_count;
	};


	struct LUMIX_RENDERER_API ForceModule LUMIX_FINAL : public ModuleBase
	{
		explicit ForceModule(ParticleEmitter& emitter);
		void serialize(OutputBlob& blob) override;
		void deserialize(InputBlob& blob) override;
		void update(float time_delta) override;
		ComponentType getType() const override { return s_type; }

		static const ComponentType s_type;
		Vec3 m_acceleration;
	};


	struct LUMIX_RENDERER_API AlphaModule LUMIX_FINAL : public ModuleBase
	{
		explicit AlphaModule(ParticleEmitter& emitter);
		void update(float time_delta) override;
		void serialize(OutputBlob&) override;
		void deserialize(InputBlob&) override;
		ComponentType getType() const override { return s_type; }
		void sample();

		static const ComponentType s_type;

		Array<Vec2> m_values;
		Array<float> m_sampled;
	};


	struct LUMIX_RENDERER_API SizeModule LUMIX_FINAL : public ModuleBase
	{
		explicit SizeModule(ParticleEmitter& emitter);
		void update(float time_delta) override;
		void serialize(OutputBlob&) override;
		void deserialize(InputBlob&) override;
		ComponentType getType() const override { return s_type; }
		void sample();

		static const ComponentType s_type;

		Array<Vec2> m_values;
		Array<float> m_sampled;
	};


	struct LUMIX_RENDERER_API RandomRotationModule LUMIX_FINAL : public ModuleBase
	{
		explicit RandomRotationModule(ParticleEmitter& emitter);
		void spawnParticle(int index) override;
		void serialize(OutputBlob&) override {}
		void deserialize(InputBlob&) override {}
		ComponentType getType() const override { return s_type; }

		static const ComponentType s_type;
	};


public:
	ParticleEmitter(Entity entity, Universe& universe, IAllocator& allocator);
	~ParticleEmitter();

	void reset();
	void init();
	void drawGizmo(WorldEditor& editor, RenderScene& scene);
	void serialize(OutputBlob& blob);
	void deserialize(InputBlob& blob, ResourceManager& manager);
	void update(float time_delta);
	Material* getMaterial() const { return m_material; }
	void setMaterial(Material* material);
	IAllocator& getAllocator() { return m_allocator; }
	void addModule(ModuleBase* module);
	ModuleBase* getModule(ComponentType hash);
	void emit();

public:
	Array<float> m_rel_life;
	Array<float> m_life;
	Array<float> m_size;
	Array<Vec3> m_position;
	Array<Vec3> m_velocity;
	Array<float> m_alpha;
	Array<float> m_rotation;
	Array<float> m_rotational_speed;

	Interval m_spawn_period;
	Interval m_initial_life;
	Interval m_initial_size;
	IntInterval m_spawn_count;

	Array<ModuleBase*> m_modules;
	SubimageModule* m_subimage_module;
	Entity m_entity;
	bool m_is_valid;
	bool m_autoemit;
	bool m_local_space;

private:
	void spawnParticle();
	void destroyParticle(int index);
	void spawnParticles(float time_delta);
	void updateLives(float time_delta);
	void updatePositions(float time_delta);
	void updateRotations(float time_delta);

private:
	IAllocator& m_allocator;
	float m_next_spawn_time;
	Universe& m_universe;
	Material* m_material;
};


} // namespace Lumix