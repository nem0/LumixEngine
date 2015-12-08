#include "editor/property_register.h"
#include "audio/audio_scene.h"
#include "core/crc32.h"
#include "core/resource_manager.h"
#include "editor/property_descriptor.h"
#include "editor/world_editor.h"
#include "universe/hierarchy.h"
#include "lua_script/lua_script_system.h"
#include "physics/physics_scene.h"
#include "renderer/render_scene.h"
#include "utils.h"
#include <cfloat>


using namespace Lumix;


template <class S> class EntityEnumPropertyDescriptor : public IEnumPropertyDescriptor
{
public:
	typedef int (S::*Getter)(ComponentIndex);
	typedef void (S::*Setter)(ComponentIndex, int);
	
public:
	EntityEnumPropertyDescriptor(const char* name,
		Getter _getter,
		Setter _setter,
		Lumix::WorldEditor& editor,
		IAllocator& allocator)
		: IEnumPropertyDescriptor(allocator)
		, m_editor(editor)
	{
		setName(name);
		m_getter = _getter;
		m_setter = _setter;
		m_type = ENUM;
	}


	void set(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		ASSERT(index == -1);
		int value;
		stream.read(&value, sizeof(value));
		auto entity = value < 0 ? INVALID_ENTITY : m_editor.getUniverse()->getEntityFromDenseIdx(value);
		(static_cast<S*>(cmp.scene)->*m_setter)(cmp.index, entity);
	};


	void get(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		ASSERT(index == -1);
		Entity value = (static_cast<S*>(cmp.scene)->*m_getter)(cmp.index);
		auto dense_idx = m_editor.getUniverse()->getDenseIdx(value);
		int len = sizeof(dense_idx);
		stream.write(&dense_idx, len);
	};


	int getEnumCount(IScene* scene) override { return scene->getUniverse().getEntityCount(); }


	const char* getEnumItemName(IScene* scene, int index) override { return nullptr; }


	void getEnumItemName(IScene* scene, int index, char* buf, int max_size) override
	{
		auto entity = scene->getUniverse().getEntityFromDenseIdx(index);
		getEntityListDisplayName(m_editor, buf, max_size, entity);
	}

private:
	Getter m_getter;
	Setter m_setter;
	Lumix::WorldEditor& m_editor;
};



void registerEngineProperties(Lumix::WorldEditor& editor)
{
	PropertyRegister::registerComponentType("hierarchy", "Hierarchy");
	IAllocator& allocator = editor.getAllocator();
	PropertyRegister::add("hierarchy",
		LUMIX_NEW(allocator, EntityEnumPropertyDescriptor<Hierarchy>)("parent",
									 &Hierarchy::getParent,
									 &Hierarchy::setParent,
									 editor,
									 allocator));
}


void registerLuaScriptProperties(IAllocator& allocator)
{
	PropertyRegister::registerComponentType("lua_script", "Lua script");

	PropertyRegister::add("lua_script",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<LuaScriptScene>)("source",
		&LuaScriptScene::getScriptPath,
		&LuaScriptScene::setScriptPath,
		"Lua (*.lua)",
		crc32("lua_script"),
		allocator));
}


void registerAudioProperties(IAllocator& allocator)
{
	PropertyRegister::registerComponentType("ambient_sound", "Ambient sound");
	PropertyRegister::registerComponentType("audio_listener", "Audio listener");
	PropertyRegister::registerComponentType("echo_zone", "Echo zone");
	
	PropertyRegister::add("ambient_sound",
		LUMIX_NEW(allocator, EnumPropertyDescriptor<AudioScene>)("Sound",
			&AudioScene::getAmbientSoundClipIndex,
			&AudioScene::setAmbientSoundClipIndex,
			&AudioScene::getClipCount,
			&AudioScene::getClipName,
			allocator
		));

	PropertyRegister::add("ambient_sound",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<AudioScene>)("3D",
		&AudioScene::isAmbientSound3D,
		&AudioScene::setAmbientSound3D,
		allocator));

	PropertyRegister::add("echo_zone",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<AudioScene>)("Radius",
		&AudioScene::getEchoZoneRadius,
		&AudioScene::setEchoZoneRadius,
		0.01f,
		FLT_MAX,
		0.1f,
		allocator));
	PropertyRegister::add("echo_zone",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<AudioScene>)("Delay (ms)",
		&AudioScene::getEchoZoneDelay,
		&AudioScene::setEchoZoneDelay,
		0.01f,
		FLT_MAX,
		100.0f,
		allocator));
}


void registerPhysicsProperties(IAllocator& allocator)
{
	PropertyRegister::registerComponentType("box_rigid_actor", "Physics Box");
	PropertyRegister::registerComponentType("physical_controller",
		"Physics Controller");
	PropertyRegister::registerComponentType("mesh_rigid_actor", "Physics Mesh");
	PropertyRegister::registerComponentType("physical_heightfield",
		"Physics Heightfield");

	PropertyRegister::add("box_rigid_actor",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<PhysicsScene>)("dynamic",
									 &PhysicsScene::isDynamic,
									 &PhysicsScene::setIsDynamic,
									 allocator));
	PropertyRegister::add("box_rigid_actor",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec3, PhysicsScene>)("size",
									 &PhysicsScene::getHalfExtents,
									 &PhysicsScene::setHalfExtents,
									 allocator));
	PropertyRegister::add("mesh_rigid_actor",
		LUMIX_NEW(allocator, FilePropertyDescriptor<PhysicsScene>)("source",
									 &PhysicsScene::getShapeSource,
									 &PhysicsScene::setShapeSource,
									 "Physics (*.pda)",
									 allocator));
	PropertyRegister::add("physical_heightfield",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<PhysicsScene>)("heightmap",
									 &PhysicsScene::getHeightmap,
									 &PhysicsScene::setHeightmap,
									 "Image (*.raw)",
									 ResourceManager::TEXTURE,
									 allocator));
	PropertyRegister::add("physical_heightfield",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("xz_scale",
									 &PhysicsScene::getHeightmapXZScale,
									 &PhysicsScene::setHeightmapXZScale,
									 0.0f,
									 FLT_MAX,
									 0.0f,
									 allocator));
	PropertyRegister::add("physical_heightfield",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("y_scale",
									 &PhysicsScene::getHeightmapYScale,
									 &PhysicsScene::setHeightmapYScale,
									 0.0f,
									 FLT_MAX,
									 0.0f,
									 allocator));
}


void registerRendererProperties(IAllocator& allocator)
{
	PropertyRegister::registerComponentType("camera", "Camera");
	PropertyRegister::registerComponentType("global_light", "Global light");
	PropertyRegister::registerComponentType("renderable", "Mesh");
	PropertyRegister::registerComponentType("particle_emitter", "Particle emitter");
	PropertyRegister::registerComponentType("particle_emitter_fade", "Particle emitter - fade");
	PropertyRegister::registerComponentType(
		"particle_emitter_linear_movement", "Particle emitter - linear movement");
	PropertyRegister::registerComponentType(
		"particle_emitter_random_rotation", "Particle emitter - random rotation");
	PropertyRegister::registerComponentType("particle_emitter_size", "Particle emitter - size");
	PropertyRegister::registerComponentType("point_light", "Point light");
	PropertyRegister::registerComponentType("terrain", "Terrain");

	PropertyRegister::registerComponentDependency("particle_emitter_fade", "particle_emitter");
	PropertyRegister::registerComponentDependency(
		"particle_emitter_linear_movement", "particle_emitter");
	PropertyRegister::registerComponentDependency(
		"particle_emitter_random_rotation", "particle_emitter");

	PropertyRegister::add("particle_emitter_fade",
		LUMIX_NEW(allocator, SampledFunctionDescriptor<RenderScene>)("alpha",
		&RenderScene::getParticleEmitterAlpha,
		&RenderScene::setParticleEmitterAlpha,
		&RenderScene::getParticleEmitterAlphaCount,
		1, 
		1,
		allocator));

	PropertyRegister::add("particle_emitter_size",
		LUMIX_NEW(allocator, SampledFunctionDescriptor<RenderScene>)("size",
		&RenderScene::getParticleEmitterSize,
		&RenderScene::setParticleEmitterSize,
		&RenderScene::getParticleEmitterSizeCount,
		1,
		1,
		allocator));

	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("x",
		&RenderScene::getParticleEmitterLinearMovementX,
		&RenderScene::setParticleEmitterLinearMovementX,
		allocator));
	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("y",
		&RenderScene::getParticleEmitterLinearMovementY,
		&RenderScene::setParticleEmitterLinearMovementY,
		allocator));
	PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("z",
		&RenderScene::getParticleEmitterLinearMovementZ,
		&RenderScene::setParticleEmitterLinearMovementZ,
		allocator));

	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("Life",
		&RenderScene::getParticleEmitterInitialLife,
		&RenderScene::setParticleEmitterInitialLife,
		allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("Initial size",
		&RenderScene::getParticleEmitterInitialSize,
		&RenderScene::setParticleEmitterInitialSize,
		allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec2, RenderScene>)("Spawn period",
		&RenderScene::getParticleEmitterSpawnPeriod,
		&RenderScene::setParticleEmitterSpawnPeriod,
		allocator));
	PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
		&RenderScene::getParticleEmitterMaterialPath,
		&RenderScene::setParticleEmitterMaterialPath,
		"Material (*.mat)",
		ResourceManager::MATERIAL,
		allocator));

	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, StringPropertyDescriptor<RenderScene>)("Slot",
		&RenderScene::getCameraSlot,
		&RenderScene::setCameraSlot,
		allocator));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("FOV",
		&RenderScene::getCameraFOV,
		&RenderScene::setCameraFOV,
		1.0f,
		179.0f,
		1.0f,
		allocator));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Near",
		&RenderScene::getCameraNearPlane,
		&RenderScene::setCameraNearPlane,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));
	PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Far",
		&RenderScene::getCameraFarPlane,
		&RenderScene::setCameraFarPlane,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));

	PropertyRegister::add("renderable",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Source",
		&RenderScene::getRenderablePath,
		&RenderScene::setRenderablePath,
		"Mesh (*.msh)",
		ResourceManager::MODEL,
		allocator));

	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Ambient intensity",
		&RenderScene::getLightAmbientIntensity,
		&RenderScene::setLightAmbientIntensity,
		0.0f,
		1.0f,
		0.05f,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, SimplePropertyDescriptor<Vec4, RenderScene>)("Shadow cascades",
		&RenderScene::getShadowmapCascades,
		&RenderScene::setShadowmapCascades,
		allocator));

	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Diffuse intensity",
		&RenderScene::getGlobalLightIntensity,
		&RenderScene::setGlobalLightIntensity,
		0.0f,
		1.0f,
		0.05f,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog density",
		&RenderScene::getFogDensity,
		&RenderScene::setFogDensity,
		0.0f,
		1.0f,
		0.01f,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog bottom",
		&RenderScene::getFogBottom,
		&RenderScene::setFogBottom,
		-FLT_MAX,
		FLT_MAX,
		1.0f,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog height",
		&RenderScene::getFogHeight,
		&RenderScene::setFogHeight,
		0.01f,
		FLT_MAX,
		1.0f,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Ambient color",
		&RenderScene::getLightAmbientColor,
		&RenderScene::setLightAmbientColor,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Diffuse color",
		&RenderScene::getGlobalLightColor,
		&RenderScene::setGlobalLightColor,
		allocator));
	PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Fog color",
		&RenderScene::getFogColor,
		&RenderScene::setFogColor,
		allocator));

	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<RenderScene>)("Cast shadows",
		&RenderScene::getLightCastShadows,
		&RenderScene::setLightCastShadows,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Diffuse intensity",
		&RenderScene::getPointLightIntensity,
		&RenderScene::setPointLightIntensity,
		0.0f,
		1.0f,
		0.05f,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Diffuse color",
		&RenderScene::getPointLightColor,
		&RenderScene::setPointLightColor,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Specular color",
		&RenderScene::getPointLightSpecularColor,
		&RenderScene::setPointLightSpecularColor,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("FOV",
		&RenderScene::getLightFOV,
		&RenderScene::setLightFOV,
		0.0f,
		360.0f,
		5.0f,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Attenuation",
		&RenderScene::getLightAttenuation,
		&RenderScene::setLightAttenuation,
		0.0f,
		1000.0f,
		0.1f,
		allocator));
	PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Range",
		&RenderScene::getLightRange,
		&RenderScene::setLightRange,
		0.0f,
		FLT_MAX,
		1.0f,
		allocator));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
		&RenderScene::getTerrainMaterialPath,
		&RenderScene::setTerrainMaterialPath,
		"Material (*.mat)",
		ResourceManager::MATERIAL,
		allocator));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("XZ scale",
		&RenderScene::getTerrainXZScale,
		&RenderScene::setTerrainXZScale,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));
	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Height scale",
		&RenderScene::getTerrainYScale,
		&RenderScene::setTerrainYScale,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));

	PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)("Grass distance",
		&RenderScene::getGrassDistance,
		&RenderScene::setGrassDistance,
		allocator));

	auto grass = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Grass",
		&RenderScene::getGrassCount,
		&RenderScene::addGrass,
		&RenderScene::removeGrass,
		allocator);
	grass->addChild(LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Mesh",
		&RenderScene::getGrassPath,
		&RenderScene::setGrassPath,
		"Mesh (*.msh)",
		crc32("model"),
		allocator));
	auto ground = LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)("Ground",
		&RenderScene::getGrassGround,
		&RenderScene::setGrassGround,
		allocator);
	ground->setLimit(0, 4);
	grass->addChild(ground);
	grass->addChild(LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)("Density",
		&RenderScene::getGrassDensity,
		&RenderScene::setGrassDensity,
		allocator));
	PropertyRegister::add("terrain", grass);
}


void registerProperties(WorldEditor& editor)
{
	registerEngineProperties(editor);
	registerRendererProperties(editor.getAllocator());
	registerLuaScriptProperties(editor.getAllocator());
	registerPhysicsProperties(editor.getAllocator());
	registerAudioProperties(editor.getAllocator());
}