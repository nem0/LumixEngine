#include "editor/property_register.h"
#include "core/crc32.h"
#include "core/resource_manager.h"
#include "editor/property_descriptor.h"
#include "editor/world_editor.h"
#include "lua_script/lua_script_system.h"
#include "physics/physics_scene.h"
#include "renderer/render_scene.h"
#include <cfloat>


using namespace Lumix;


void registerLuaScriptProperties(Lumix::IAllocator& allocator)
{
	PropertyRegister::registerComponentType("lua_script", "Lua script");

	Lumix::PropertyRegister::add("lua_script",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<LuaScriptScene>)("source",
									 &LuaScriptScene::getScriptPath,
									 &LuaScriptScene::setScriptPath,
									 "Lua (*.lua)",
									 crc32("lua_script"),
									 allocator));
}


void registerPhysicsProperties(Lumix::IAllocator& allocator)
{
	PropertyRegister::registerComponentType("box_rigid_actor", "Physics Box");
	PropertyRegister::registerComponentType("physical_controller",
		"Physics Controller");
	PropertyRegister::registerComponentType("mesh_rigid_actor", "Physics Mesh");
	PropertyRegister::registerComponentType("physical_heightfield",
		"Physics Heightfield");

	Lumix::PropertyRegister::add("box_rigid_actor",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<PhysicsScene>)("dynamic",
									 &PhysicsScene::isDynamic,
									 &PhysicsScene::setIsDynamic,
									 allocator));
	Lumix::PropertyRegister::add("box_rigid_actor",
		LUMIX_NEW(allocator, Vec3PropertyDescriptor<PhysicsScene>)("size",
									 &PhysicsScene::getHalfExtents,
									 &PhysicsScene::setHalfExtents,
									 allocator));
	Lumix::PropertyRegister::add("mesh_rigid_actor",
		LUMIX_NEW(allocator, FilePropertyDescriptor<PhysicsScene>)("source",
									 &PhysicsScene::getShapeSource,
									 &PhysicsScene::setShapeSource,
									 "Physics (*.pda)",
									 allocator));
	Lumix::PropertyRegister::add("physical_heightfield",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<PhysicsScene>)("heightmap",
									 &PhysicsScene::getHeightmap,
									 &PhysicsScene::setHeightmap,
									 "Image (*.raw)",
									 Lumix::ResourceManager::TEXTURE,
									 allocator));
	Lumix::PropertyRegister::add("physical_heightfield",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("xz_scale",
									 &PhysicsScene::getHeightmapXZScale,
									 &PhysicsScene::setHeightmapXZScale,
									 0.0f,
									 FLT_MAX,
									 0.0f,
									 allocator));
	Lumix::PropertyRegister::add("physical_heightfield",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<PhysicsScene>)("y_scale",
									 &PhysicsScene::getHeightmapYScale,
									 &PhysicsScene::setHeightmapYScale,
									 0.0f,
									 FLT_MAX,
									 0.0f,
									 allocator));
}


void registerRendererProperties(Lumix::IAllocator& allocator)
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

	typedef SampledFunctionDescriptor<RenderScene, 10> SampledFunctionDescT;
	Lumix::PropertyRegister::add("particle_emitter_fade",
		LUMIX_NEW(allocator, SampledFunctionDescT)("alpha",
		&RenderScene::getParticleEmitterAlpha,
		&RenderScene::setParticleEmitterAlpha,
		0, 
		1,
		allocator));

	Lumix::PropertyRegister::add("particle_emitter_size",
		LUMIX_NEW(allocator, SampledFunctionDescT)("size",
		&RenderScene::getParticleEmitterSize,
		&RenderScene::setParticleEmitterSize,
		0,
		1,
		allocator));

	Lumix::PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, Vec2PropertyDescriptor<RenderScene>)("x",
		&RenderScene::getParticleEmitterLinearMovementX,
		&RenderScene::setParticleEmitterLinearMovementX,
		allocator));
	Lumix::PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, Vec2PropertyDescriptor<RenderScene>)("y",
		&RenderScene::getParticleEmitterLinearMovementY,
		&RenderScene::setParticleEmitterLinearMovementY,
		allocator));
	Lumix::PropertyRegister::add("particle_emitter_linear_movement",
		LUMIX_NEW(allocator, Vec2PropertyDescriptor<RenderScene>)("z",
		&RenderScene::getParticleEmitterLinearMovementZ,
		&RenderScene::setParticleEmitterLinearMovementZ,
		allocator));

	Lumix::PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, Vec2PropertyDescriptor<RenderScene>)("Life",
		&RenderScene::getParticleEmitterInitialLife,
		&RenderScene::setParticleEmitterInitialLife,
		allocator));
	Lumix::PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, Vec2PropertyDescriptor<RenderScene>)("Initial size",
		&RenderScene::getParticleEmitterInitialSize,
		&RenderScene::setParticleEmitterInitialSize,
		allocator));
	Lumix::PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, Vec2PropertyDescriptor<RenderScene>)("Spawn period",
		&RenderScene::getParticleEmitterSpawnPeriod,
		&RenderScene::setParticleEmitterSpawnPeriod,
		allocator));
	Lumix::PropertyRegister::add("particle_emitter",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
		&RenderScene::getParticleEmitterMaterialPath,
		&RenderScene::setParticleEmitterMaterialPath,
		"Material (*.mat)",
		Lumix::ResourceManager::MATERIAL,
		allocator));

	Lumix::PropertyRegister::add("camera",
		LUMIX_NEW(allocator, StringPropertyDescriptor<RenderScene>)("Slot",
		&RenderScene::getCameraSlot,
		&RenderScene::setCameraSlot,
		allocator));
	Lumix::PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("FOV",
		&RenderScene::getCameraFOV,
		&RenderScene::setCameraFOV,
		1.0f,
		179.0f,
		1.0f,
		allocator));
	Lumix::PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Near",
		&RenderScene::getCameraNearPlane,
		&RenderScene::setCameraNearPlane,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));
	Lumix::PropertyRegister::add("camera",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Far",
		&RenderScene::getCameraFarPlane,
		&RenderScene::setCameraFarPlane,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));

	Lumix::PropertyRegister::add("renderable",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Source",
		&RenderScene::getRenderablePath,
		&RenderScene::setRenderablePath,
		"Mesh (*.msh)",
		Lumix::ResourceManager::MODEL,
		allocator));

	Lumix::PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Ambient intensity",
		&RenderScene::getLightAmbientIntensity,
		&RenderScene::setLightAmbientIntensity,
		0.0f,
		1.0f,
		0.05f,
		allocator));
	Lumix::PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, Vec4PropertyDescriptor<RenderScene>)("Shadow cascades",
		&RenderScene::getShadowmapCascades,
		&RenderScene::setShadowmapCascades,
		allocator));

	Lumix::PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Diffuse intensity",
		&RenderScene::getGlobalLightIntensity,
		&RenderScene::setGlobalLightIntensity,
		0.0f,
		1.0f,
		0.05f,
		allocator));
	Lumix::PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog density",
		&RenderScene::getFogDensity,
		&RenderScene::setFogDensity,
		0.0f,
		1.0f,
		0.01f,
		allocator));
	Lumix::PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog bottom",
		&RenderScene::getFogBottom,
		&RenderScene::setFogBottom,
		-FLT_MAX,
		FLT_MAX,
		1.0f,
		allocator));
	Lumix::PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Fog height",
		&RenderScene::getFogHeight,
		&RenderScene::setFogHeight,
		0.01f,
		FLT_MAX,
		1.0f,
		allocator));
	Lumix::PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Ambient color",
		&RenderScene::getLightAmbientColor,
		&RenderScene::setLightAmbientColor,
		allocator));
	Lumix::PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Diffuse color",
		&RenderScene::getGlobalLightColor,
		&RenderScene::setGlobalLightColor,
		allocator));
	Lumix::PropertyRegister::add("global_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Fog color",
		&RenderScene::getFogColor,
		&RenderScene::setFogColor,
		allocator));

	Lumix::PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, BoolPropertyDescriptor<RenderScene>)("Cast shadows",
		&RenderScene::getLightCastShadows,
		&RenderScene::setLightCastShadows,
		allocator));
	Lumix::PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Diffuse intensity",
		&RenderScene::getPointLightIntensity,
		&RenderScene::setPointLightIntensity,
		0.0f,
		1.0f,
		0.05f,
		allocator));
	Lumix::PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Diffuse color",
		&RenderScene::getPointLightColor,
		&RenderScene::setPointLightColor,
		allocator));
	Lumix::PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, ColorPropertyDescriptor<RenderScene>)("Specular color",
		&RenderScene::getPointLightSpecularColor,
		&RenderScene::setPointLightSpecularColor,
		allocator));
	Lumix::PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("FOV",
		&RenderScene::getLightFOV,
		&RenderScene::setLightFOV,
		0.0f,
		360.0f,
		5.0f,
		allocator));
	Lumix::PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Attenuation",
		&RenderScene::getLightAttenuation,
		&RenderScene::setLightAttenuation,
		0.0f,
		1000.0f,
		0.1f,
		allocator));
	Lumix::PropertyRegister::add("point_light",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Range",
		&RenderScene::getLightRange,
		&RenderScene::setLightRange,
		0.0f,
		FLT_MAX,
		1.0f,
		allocator));
	Lumix::PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, ResourcePropertyDescriptor<RenderScene>)("Material",
		&RenderScene::getTerrainMaterialPath,
		&RenderScene::setTerrainMaterialPath,
		"Material (*.mat)",
		Lumix::ResourceManager::MATERIAL,
		allocator));
	Lumix::PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("XZ scale",
		&RenderScene::getTerrainXZScale,
		&RenderScene::setTerrainXZScale,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));
	Lumix::PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, DecimalPropertyDescriptor<RenderScene>)("Height scale",
		&RenderScene::getTerrainYScale,
		&RenderScene::setTerrainYScale,
		0.0f,
		FLT_MAX,
		0.0f,
		allocator));

	Lumix::PropertyRegister::add("terrain",
		LUMIX_NEW(allocator, IntPropertyDescriptor<RenderScene>)("Grass distance",
		&RenderScene::getGrassDistance,
		&RenderScene::setGrassDistance,
		allocator));

	auto grass = LUMIX_NEW(allocator, ArrayDescriptor<RenderScene>)("Grass",
		&RenderScene::getGrassCount,
		&RenderScene::addGrass,
		&RenderScene::removeGrass,
		allocator);
	grass->addChild(LUMIX_NEW(allocator, ResourceArrayObjectDescriptor<RenderScene>)("Mesh",
		&RenderScene::getGrassPath,
		&RenderScene::setGrassPath,
		"Mesh (*.msh)",
		crc32("model"),
		allocator));
	auto ground = LUMIX_NEW(allocator, IntArrayObjectDescriptor<RenderScene>)("Ground",
		&RenderScene::getGrassGround,
		&RenderScene::setGrassGround,
		allocator);
	ground->setLimit(0, 4);
	grass->addChild(ground);
	grass->addChild(LUMIX_NEW(allocator, IntArrayObjectDescriptor<RenderScene>)("Density",
		&RenderScene::getGrassDensity,
		&RenderScene::setGrassDensity,
		allocator));
	Lumix::PropertyRegister::add("terrain", grass);
}


void registerProperties(Lumix::WorldEditor& editor)
{
	registerRendererProperties(editor.getAllocator());
	registerLuaScriptProperties(editor.getAllocator());
	registerPhysicsProperties(editor.getAllocator());
}