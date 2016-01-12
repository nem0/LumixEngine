#include "particle_system.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "renderer/material.h"
#include "renderer/render_scene.h"
#include "universe/universe.h"
#include <cmath>


enum class ParticleEmitterVersion : int
{
	SPAWN_COUNT,
	SIZE_ALPHA_SAVE,

	LATEST,
	INVALID = -1
};


namespace Lumix
{


template <typename T>
static ParticleEmitter::ModuleBase* create(ParticleEmitter& emitter)
{
	return LUMIX_NEW(emitter.getAllocator(), T)(emitter);
}


static ParticleEmitter::ModuleBase* createModule(uint32 type, ParticleEmitter& emitter)
{
	typedef ParticleEmitter::ModuleBase* (*Creator)(ParticleEmitter& emitter);
	static const struct { uint32 hash; Creator creator; } creators[] = {
		{ ParticleEmitter::ForceModule::s_type, create<ParticleEmitter::ForceModule> },
		{ ParticleEmitter::PlaneModule::s_type, create<ParticleEmitter::PlaneModule> },
		{ ParticleEmitter::LinearMovementModule::s_type, create<ParticleEmitter::LinearMovementModule> },
		{ ParticleEmitter::AlphaModule::s_type, create<ParticleEmitter::AlphaModule> },
		{ ParticleEmitter::RandomRotationModule::s_type, create<ParticleEmitter::RandomRotationModule> },
		{ ParticleEmitter::SizeModule::s_type, create<ParticleEmitter::SizeModule> },
		{ ParticleEmitter::AttractorModule::s_type, create<ParticleEmitter::AttractorModule> },
		{ ParticleEmitter::SpawnShapeModule::s_type, create<ParticleEmitter::SpawnShapeModule> }
	};

	for(auto& i : creators)
	{
		if(i.hash == type)
		{
			return i.creator(emitter);
		}
	}

	return nullptr;
}


ParticleEmitter::ModuleBase::ModuleBase(ParticleEmitter& emitter)
	: m_emitter(emitter)
{
}


ParticleEmitter::ForceModule::ForceModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
{
	m_acceleration.set(0, 0, 0);
}


void ParticleEmitter::ForceModule::serialize(OutputBlob& blob)
{
	blob.write(m_acceleration);
}


void ParticleEmitter::ForceModule::deserialize(InputBlob& blob, int)
{
	blob.read(m_acceleration);
}


void ParticleEmitter::ForceModule::update(float time_delta)
{
	if (m_emitter.m_velocity.empty()) return;

	Vec3* LUMIX_RESTRICT particle_velocity = &m_emitter.m_velocity[0];
	for (int i = 0, c = m_emitter.m_velocity.size(); i < c; ++i)
	{
		particle_velocity[i] += m_acceleration * time_delta;
	}
}


const uint32 ParticleEmitter::ForceModule::s_type = Lumix::crc32("force");


void ParticleEmitter::drawGizmo(RenderScene& scene)
{
	for (auto* module : m_modules)
	{
		module->drawGizmo(scene);
	}
}


ParticleEmitter::AttractorModule::AttractorModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, m_force(0)
{
	m_count = 0;
	for(auto& e : m_entities)
	{
		e = INVALID_ENTITY;
	}
}


void ParticleEmitter::AttractorModule::update(float time_delta)
{
	if(m_emitter.m_alpha.empty()) return;

	Vec3* LUMIX_RESTRICT particle_pos = &m_emitter.m_position[0];
	Vec3* LUMIX_RESTRICT particle_vel = &m_emitter.m_velocity[0];

	for(int i = 0; i < m_count; ++i)
	{
		auto entity = m_entities[i];
		if(entity == INVALID_ENTITY) continue;
		if (!m_emitter.m_universe.hasEntity(entity)) continue;
		Vec3 pos = m_emitter.m_universe.getPosition(entity);

		for(int i = m_emitter.m_position.size() - 1; i >= 0; --i)
		{
			Vec3 to_center = pos - particle_pos[i];
			float dist2 = to_center.squaredLength();
			to_center *= 1 / sqrt(dist2);
			particle_vel[i] = particle_vel[i] + to_center * (m_force / dist2) * time_delta;
		}
	}
}


void ParticleEmitter::AttractorModule::serialize(OutputBlob& blob)
{
	blob.write(m_force);
	blob.write(m_count);
	for(int i = 0; i < m_count; ++i)
	{
		blob.write(m_entities[i]);
	}
}


void ParticleEmitter::AttractorModule::deserialize(InputBlob& blob, int)
{
	blob.read(m_force);
	blob.read(m_count);
	for(int i = 0; i < m_count; ++i)
	{
		blob.read(m_entities[i]);
	}
}


const uint32 ParticleEmitter::AttractorModule::s_type = Lumix::crc32("attractor");



ParticleEmitter::PlaneModule::PlaneModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, m_bounce(0.5f)
{
	m_count = 0;
	for (auto& e : m_entities)
	{
		e = INVALID_ENTITY;
	}
}


void ParticleEmitter::PlaneModule::drawGizmo(RenderScene& scene)
{
	for (int i = 0; i < m_count; ++i)
	{
		Entity entity = m_entities[i];
		if (entity == INVALID_ENTITY) continue;
		if (!m_emitter.m_universe.hasEntity(entity)) continue;

		Matrix mtx = m_emitter.m_universe.getMatrix(entity);
		Vec3 pos = mtx.getTranslation();
		Vec3 right = mtx.getXVector();
		Vec3 normal = mtx.getYVector();
		Vec3 forward = mtx.getZVector();
		uint32 color = 0xffff0000;

		for (int i = 0; i < 9; ++i)
		{
			float w = i / 4.0f - 1.0f;
			scene.addDebugLine(pos - right - forward * w, pos + right - forward * w, color, 0);
			scene.addDebugLine(pos - right * w - forward, pos - right * w + forward, color, 0);
		}
	}
}


void ParticleEmitter::PlaneModule::update(float time_delta)
{
	if (m_emitter.m_alpha.empty()) return;

	Vec3* LUMIX_RESTRICT particle_pos = &m_emitter.m_position[0];
	Vec3* LUMIX_RESTRICT particle_vel = &m_emitter.m_velocity[0];

	for (int i = 0; i < m_count; ++i)
	{
		auto entity = m_entities[i];
		if (entity == INVALID_ENTITY) continue;
		if (!m_emitter.m_universe.hasEntity(entity)) continue;
		Vec3 normal = m_emitter.m_universe.getRotation(entity) * Vec3(0, 1, 0);
		float D = -dotProduct(normal, m_emitter.m_universe.getPosition(entity));

		for (int i = m_emitter.m_position.size() - 1; i >= 0; --i)
		{
			const auto& pos = particle_pos[i];
			if (dotProduct(normal, pos) + D < 0)
			{
				float NdotV = dotProduct(normal, particle_vel[i]);
				particle_vel[i] = (particle_vel[i] - normal * (2 * NdotV)) * m_bounce;
			}
		}
	}
}


void ParticleEmitter::PlaneModule::serialize(OutputBlob& blob)
{
	blob.write(m_bounce);
	blob.write(m_count);
	for (int i = 0; i < m_count; ++i)
	{
		blob.write(m_entities[i]);
	}
}


void ParticleEmitter::PlaneModule::deserialize(InputBlob& blob, int)
{
	blob.read(m_bounce);
	blob.read(m_count);
	for (int i = 0; i < m_count; ++i)
	{
		blob.read(m_entities[i]);
	}
}


const uint32 ParticleEmitter::PlaneModule::s_type = Lumix::crc32("plane");


ParticleEmitter::SpawnShapeModule::SpawnShapeModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, m_radius(1.0f)
	, m_shape(SPHERE)
{
}


void ParticleEmitter::SpawnShapeModule::spawnParticle(int index)
{
	// ugly and ~0.1% from uniform distribution, but still faster than the correct solution
	float r2 = m_radius * m_radius;
	for (int i = 0; i < 10; ++i)
	{
		Vec3 v(m_radius * Math::randFloat(-1, 1),
			m_radius * Math::randFloat(-1, 1),
			m_radius * Math::randFloat(-1, 1));

		if (v.squaredLength() < r2)
		{
			m_emitter.m_position[index] += v;
			return;
		}
	}
}


void ParticleEmitter::SpawnShapeModule::serialize(OutputBlob& blob)
{
	blob.write(m_shape);
	blob.write(m_radius);
}


void ParticleEmitter::SpawnShapeModule::deserialize(InputBlob& blob, int)
{
	blob.read(m_shape);
	blob.read(m_radius);
}


const uint32 ParticleEmitter::SpawnShapeModule::s_type = Lumix::crc32("spawn_shape");


ParticleEmitter::LinearMovementModule::LinearMovementModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
{
}


void ParticleEmitter::LinearMovementModule::spawnParticle(int index)
{
	m_emitter.m_velocity[index].x = m_x.getRandom();
	m_emitter.m_velocity[index].y = m_y.getRandom();
	m_emitter.m_velocity[index].z = m_z.getRandom();
}


void ParticleEmitter::LinearMovementModule::serialize(OutputBlob& blob)
{
	blob.write(m_x);
	blob.write(m_y);
	blob.write(m_z);
}


void ParticleEmitter::LinearMovementModule::deserialize(InputBlob& blob, int)
{
	blob.read(m_x);
	blob.read(m_y);
	blob.read(m_z);
}


const uint32 ParticleEmitter::LinearMovementModule::s_type = Lumix::crc32("linear_movement");


static void sampleBezier(float max_life, const Array<Vec2>& values, Array<float>& sampled)
{
	ASSERT(values.size() >= 6);
	ASSERT(values[values.size() - 2].x - values[1].x > 0);

	static const int SAMPLES_PER_SECOND = 10;
	sampled.resize(int(max_life * SAMPLES_PER_SECOND));

	float x_range = values[values.size() - 2].x - values[1].x;
	int last_idx = 0;
	for (int i = 1; i < values.size() - 3; i += 3)
	{
		int step_count = int(5 * sampled.size() * ((values[i + 3].x - values[i].x) / x_range));
		float t_step = 1.0f / (float)step_count;
		for (int i_step = 1; i_step <= step_count; i_step++)
		{
			float t = t_step * i_step;
			float u = 1.0f - t;
			float w1 = u * u * u;
			float w2 = 3 * u * u * t;
			float w3 = 3 * u * t * t;
			float w4 = t * t * t;
			auto p = values[i] * (w1 + w2) + values[i + 1] * w2 + values[i + 2] * w3 +
					 values[i + 3] * (w3 + w4);
			int idx = int(sampled.size() * ((p.x - values[1].x) / x_range));
			ASSERT(idx <= last_idx + 1);
			last_idx = idx;
			sampled[idx >= sampled.size() ? sampled.size() - 1 : idx] = p.y;
		}
	}
	sampled[0] = values[0].y;
	sampled.back() = values[values.size() - 2].y;
}


ParticleEmitter::AlphaModule::AlphaModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, m_values(emitter.getAllocator())
	, m_sampled(emitter.getAllocator())
{
	m_values.resize(9);
	m_values[0].set(-0.2f, 0);
	m_values[1].set(0, 0);
	m_values[2].set(0.2f, 0.0f);
	m_values[3].set(-0.2f, 0.0f);
	m_values[4].set(0.5f, 1.0f);
	m_values[5].set(0.2f, 0.0f);
	m_values[6].set(-0.2f, 0.0f);
	m_values[7].set(1, 0);
	m_values[8].set(0.2f, 0);
	sample();
}


void ParticleEmitter::AlphaModule::serialize(OutputBlob& blob)
{
	blob.write(m_values.size());
	blob.write(&m_values[0], sizeof(m_values[0]) * m_values.size());
}


void ParticleEmitter::AlphaModule::deserialize(InputBlob& blob, int version)
{
	if (version <= (int)ParticleEmitterVersion::SIZE_ALPHA_SAVE) return;

	int size;
	blob.read(size);
	m_values.resize(size);
	blob.read(&m_values[0], sizeof(m_values[0]) * m_values.size());
	sample();
}


void ParticleEmitter::AlphaModule::sample()
{
	sampleBezier(m_emitter.m_initial_life.to, m_values, m_sampled);
}


void ParticleEmitter::AlphaModule::update(float)
{
	if(m_emitter.m_alpha.empty()) return;

	float* LUMIX_RESTRICT particle_alpha = &m_emitter.m_alpha[0];
	float* LUMIX_RESTRICT rel_life = &m_emitter.m_rel_life[0];
	int size = m_sampled.size() - 1;
	float float_size = (float)size;
	for(int i = 0, c = m_emitter.m_size.size(); i < c; ++i)
	{
		float float_idx = float_size * rel_life[i];
		int idx = (int)float_idx;
		int next_idx = Math::minValue(idx + 1, size);
		float w = float_idx - idx;
		particle_alpha[i] = m_sampled[idx] * (1 - w) + m_sampled[next_idx] * w;
	}
}


const uint32 ParticleEmitter::AlphaModule::s_type = Lumix::crc32("alpha");


ParticleEmitter::SizeModule::SizeModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
	, m_values(emitter.getAllocator())
	, m_sampled(emitter.getAllocator())
{
	m_values.resize(9);
	m_values[0].set(-0.2f, 0);
	m_values[1].set(0, 0);
	m_values[2].set(0.2f, 0.0f);
	m_values[3].set(-0.2f, 0.0f);
	m_values[4].set(0.5f, 1.0f);
	m_values[5].set(0.2f, 0.0f);
	m_values[6].set(-0.2f, 0.0f);
	m_values[7].set(1, 0);
	m_values[8].set(0.2f, 0);
	sample();
}


void ParticleEmitter::SizeModule::serialize(OutputBlob& blob)
{
	blob.write(m_values.size());
	blob.write(&m_values[0], sizeof(m_values[0]) * m_values.size());
}


void ParticleEmitter::SizeModule::deserialize(InputBlob& blob, int version)
{
	if (version <= (int)ParticleEmitterVersion::SIZE_ALPHA_SAVE) return;

	int size;
	blob.read(size);
	m_values.resize(size);
	blob.read(&m_values[0], sizeof(m_values[0]) * m_values.size());
	sample();
}


void ParticleEmitter::SizeModule::sample()
{
	sampleBezier(m_emitter.m_initial_life.to, m_values, m_sampled);
}


void ParticleEmitter::SizeModule::update(float)
{
	if (m_emitter.m_size.empty()) return;

	float* LUMIX_RESTRICT particle_size = &m_emitter.m_size[0];
	float* LUMIX_RESTRICT rel_life = &m_emitter.m_rel_life[0];
	int size = m_sampled.size() - 1;
	float float_size = (float)size;
	for (int i = 0, c = m_emitter.m_size.size(); i < c; ++i)
	{
		float float_idx = float_size * rel_life[i];
		int idx = (int)float_idx;
		int next_idx = Math::minValue(idx + 1, size);
		float w = float_idx - idx;
		particle_size[i] = m_sampled[idx] * (1 - w) + m_sampled[next_idx] * w;
		PROFILE_INT("Test", int(particle_size[i] * 1000));
	}
}


const uint32 ParticleEmitter::SizeModule::s_type = Lumix::crc32("size");


ParticleEmitter::RandomRotationModule::RandomRotationModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
{
}


void ParticleEmitter::RandomRotationModule::spawnParticle(int index)
{
	m_emitter.m_rotation[index] = Math::randFloat(0, Math::PI * 2);
}


const uint32 ParticleEmitter::RandomRotationModule::s_type = Lumix::crc32("random_rotation");



Interval::Interval()
	: from(0)
	, to(0)
{
}


IntInterval::IntInterval()
{
	from = to = 1;
}


int IntInterval::getRandom() const
{
	if (from == to) return from;
	return Math::rand(from, to);
}


void Interval::checkZero()
{
	from = Math::maxValue(from, 0.0f);
	to = Math::maxValue(from, to);
}


void Interval::check()
{
	to = Math::maxValue(from, to);
}


float Interval::getRandom() const
{
	return Math::randFloat(from, to);
}


ParticleEmitter::ParticleEmitter(Entity entity, Universe& universe, IAllocator& allocator)
	: m_next_spawn_time(0)
	, m_allocator(allocator)
	, m_rel_life(allocator)
	, m_life(allocator)
	, m_modules(allocator)
	, m_position(allocator)
	, m_velocity(allocator)
	, m_rotation(allocator)
	, m_rotational_speed(allocator)
	, m_alpha(allocator)
	, m_universe(universe)
	, m_entity(entity)
	, m_size(allocator)
	, m_material(nullptr)
{
	m_spawn_period.from = 1;
	m_spawn_period.to = 2;
	m_initial_life.from = 1;
	m_initial_life.to = 2;
	m_initial_size.from = 1;
	m_initial_size.to = 1;
}


ParticleEmitter::~ParticleEmitter()
{
	setMaterial(nullptr);

	for (auto* module : m_modules)
	{
		LUMIX_DELETE(m_allocator, module);
	}
}


void ParticleEmitter::reset()
{
	m_rel_life.clear();
	m_life.clear();
	m_size.clear();
	m_position.clear();
	m_velocity.clear();
	m_alpha.clear();
	m_rotation.clear();
	m_rotational_speed.clear();
}


void ParticleEmitter::setMaterial(Material* material)
{
	if (m_material)
	{
		auto* manager = m_material->getResourceManager().get(ResourceManager::MATERIAL);
		manager->unload(*m_material);
	}
	m_material = material;
}


void ParticleEmitter::spawnParticle()
{
	m_position.push(m_universe.getPosition(m_entity));
	m_rotation.push(0);
	m_rotational_speed.push(0);
	m_life.push(m_initial_life.getRandom());
	m_rel_life.push(0.0f);
	m_alpha.push(1);
	m_velocity.push(Vec3(0, 0, 0));
	m_size.push(m_initial_size.getRandom());
	for (auto* module : m_modules)
	{
		module->spawnParticle(m_life.size() - 1);
	}
}


void ParticleEmitter::addModule(ModuleBase* module)
{
	m_modules.push(module);
}


void ParticleEmitter::destroyParticle(int index)
{
	for (auto* module : m_modules)
	{
		module->destoryParticle(index);
	}
	m_life.eraseFast(index);
	m_rel_life.eraseFast(index);
	m_position.eraseFast(index);
	m_velocity.eraseFast(index);
	m_rotation.eraseFast(index);
	m_rotational_speed.eraseFast(index);
	m_alpha.eraseFast(index);
	m_size.eraseFast(index);
}


void ParticleEmitter::updateLives(float time_delta)
{
	for (int i = 0, c = m_rel_life.size(); i < c; ++i)
	{
		float rel_life = m_rel_life[i];
		rel_life += time_delta / m_life[i];
		m_rel_life[i] = rel_life;

		if (rel_life > 1)
		{
			destroyParticle(i);
			--i;
			--c;
		}
	}
}


void ParticleEmitter::serialize(OutputBlob& blob)
{
	blob.write((int)ParticleEmitterVersion::LATEST);
	blob.write(m_spawn_count);
	blob.write(m_spawn_period);
	blob.write(m_initial_life);
	blob.write(m_initial_size);
	blob.write(m_entity);
	blob.writeString(m_material ? m_material->getPath().c_str() : "");
	blob.write(m_modules.size());
	for (auto* module : m_modules)
	{
		blob.write(module->getType());
		module->serialize(blob);
	}
}


void ParticleEmitter::deserialize(InputBlob& blob, ResourceManager& manager, bool has_version)
{
	int version = (int)ParticleEmitterVersion::INVALID;
	if (has_version)
	{
		blob.read(version);
		if (version > (int)ParticleEmitterVersion::SPAWN_COUNT) blob.read(m_spawn_count);
	}
	blob.read(m_spawn_period);
	blob.read(m_initial_life);
	blob.read(m_initial_size);
	blob.read(m_entity);
	char path[MAX_PATH_LENGTH];
	blob.readString(path, lengthOf(path));
	auto material_manager = manager.get(ResourceManager::MATERIAL);
	auto material = static_cast<Material*>(material_manager->load(Lumix::Path(path)));
	setMaterial(material);

	int size;
	blob.read(size);
	for (auto* module : m_modules)
	{
		LUMIX_DELETE(m_allocator, module);
	}
	m_modules.clear();
	for (int i = 0; i < size; ++i)
	{
		uint32 type;
		blob.read(type);
		auto* module = createModule(type, *this);
		m_modules.push(module);
		module->deserialize(blob, version);
	}
}


void ParticleEmitter::updatePositions(float time_delta)
{
	for (int i = 0, c = m_position.size(); i < c; ++i)
	{
		m_position[i] += m_velocity[i] * time_delta;
	}
}


void ParticleEmitter::updateRotations(float time_delta)
{
	for (int i = 0, c = m_rotation.size(); i < c; ++i)
	{
		m_rotation[i] += m_rotational_speed[i] * time_delta;
	}
}


void ParticleEmitter::update(float time_delta)
{
	spawnParticles(time_delta);
	updateLives(time_delta);
	updatePositions(time_delta);
	updateRotations(time_delta);
	for (auto* module : m_modules)
	{
		module->update(time_delta);
	}
}


void ParticleEmitter::spawnParticles(float time_delta)
{
	m_next_spawn_time -= time_delta;

	while (m_next_spawn_time < 0)
	{
		m_next_spawn_time += m_spawn_period.getRandom();

		int spawn_count = m_spawn_count.getRandom();
		for (int i = 0; i < spawn_count; ++i)
		{
			spawnParticle();
		}
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