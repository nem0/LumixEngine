#include "particle_system.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/math_utils.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "renderer/material.h"
#include "universe/universe.h"


namespace Lumix
{


static ParticleEmitter::ModuleBase* createModule(uint32_t type, ParticleEmitter& emitter)
{
	if (type == ParticleEmitter::LinearMovementModule::s_type)
	{
		return LUMIX_NEW(emitter.getAllocator(), ParticleEmitter::LinearMovementModule)(emitter);
	}
	if (type == ParticleEmitter::AlphaModule::s_type)
	{
		return LUMIX_NEW(emitter.getAllocator(), ParticleEmitter::AlphaModule)(emitter);
	}
	if (type == ParticleEmitter::RandomRotationModule::s_type)
	{
		return LUMIX_NEW(emitter.getAllocator(), ParticleEmitter::RandomRotationModule)(emitter);
	}

	return nullptr;
}


ParticleEmitter::ModuleBase::ModuleBase(ParticleEmitter& emitter)
	: m_emitter(emitter)
{
}


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


void ParticleEmitter::LinearMovementModule::deserialize(InputBlob& blob)
{
	blob.read(m_x);
	blob.read(m_y);
	blob.read(m_z);
}


const uint32_t ParticleEmitter::LinearMovementModule::s_type = Lumix::crc32("linear_movement");


ParticleEmitter::AlphaModule::AlphaModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
{
}


void ParticleEmitter::AlphaModule::update(float time_delta)
{
	if (m_emitter.m_alpha.empty()) return;

	float* alpha = &m_emitter.m_alpha[0];
	for (int i = 0, c = m_emitter.m_alpha.size(); i < c; ++i)
	{
		alpha[i] = Math::minValue(m_emitter.m_life[i], 1.0f);
	}
}


const uint32_t ParticleEmitter::AlphaModule::s_type = Lumix::crc32("alpha");


ParticleEmitter::RandomRotationModule::RandomRotationModule(ParticleEmitter& emitter)
	: ModuleBase(emitter)
{
}


void ParticleEmitter::RandomRotationModule::spawnParticle(int index)
{
	m_emitter.m_rotation[index] = Math::degreesToRadians(float(rand() % 360));
}


const uint32_t ParticleEmitter::RandomRotationModule::s_type = Lumix::crc32("random_rotation");



Interval::Interval()
	: from(0)
	, to(0)
{
}


void Interval::check()
{
	from = Math::maxValue(from, 0.0f);
	to = Math::maxValue(from, to);
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
	m_position.eraseFast(index);
	m_velocity.eraseFast(index);
	m_rotation.eraseFast(index);
	m_rotational_speed.eraseFast(index);
	m_alpha.eraseFast(index);
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


void ParticleEmitter::serialize(OutputBlob& blob)
{
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


void ParticleEmitter::deserialize(InputBlob& blob, ResourceManager& manager)
{
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
		uint32_t type;
		blob.read(type);
		auto* module = createModule(type, *this);
		m_modules.push(module);
		module->deserialize(blob);
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