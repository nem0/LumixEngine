#include "core/log.h"
#include "engine/engine.h"
#include "engine/input_system.h"
#include "engine/reflection.h"
#include "engine/world.h"
#include "imgui/imgui.h"
#include "evox/capi.h"
#include "evox/bytecode.h"
#include "evox/evox_module.h"
#include "evox/evox_capi.gen.h"
#include "evox/evox_wrapper.h"
#include "renderer/render_module.h"
#include <stddef.h>
#include <string.h>

namespace Lumix::Evox {

template <> inline ExEntity readArg<ExEntity>(ex_call_frame& frame) {
	EX_ARG(frame, i32, entity_index);
	EX_ARG(frame, u32, entity_padding);
	EX_ARG(frame, World*, world);
	return ExEntity{entity_index, world};
}

void writeResult(ex_runtime*, ex_call_frame& frame, const ExEntity& value) {
	EX_RESULT(frame, value);
}

namespace {

using NativeFunctionMap = HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash>;

static void panic(ex_call_frame& frame, const char* message) {
	*frame.panic = {message, (i64)strlen(message)};
}

enum class LoadRequestStatus : i32 { PENDING, SUCCESS, FAIL };

struct EvoxLoadRequest {
	World* world;
	LoadRequestStatus status = LoadRequestStatus::PENDING;
	char error[256] = {};

	void complete(Span<const u8> mem, bool success) {
		if (!success) {
			copyString(error, "Failed to read world");
			status = LoadRequestStatus::FAIL;
			return;
		}

		InputMemoryStream blob(mem);
		EntityMap entity_map(world->getAllocator());
		WorldVersion editor_version;
		if (!world->deserialize(blob, entity_map, editor_version)) {
			copyString(error, "Failed to deserialize world");
			status = LoadRequestStatus::FAIL;
			return;
		}
		status = LoadRequestStatus::SUCCESS;
	}
};

static void evox_world_load(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	EX_STRING_ARG(frame, path);
	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	if (path.length <= 0 || !path.begin) {
		panic(frame, "Invalid load path");
		return;
	}
	EvoxLoadRequest* request = LUMIX_NEW(world->getAllocator(), EvoxLoadRequest);
	request->world = world;
	Path file_path(StringView(path.begin, (u64)path.length));
	world->getEngine().getFileSystem().getContent(file_path, makeDelegate<&EvoxLoadRequest::complete>(request));
	EX_RESULT(frame, request);
}

static void evox_load_getStatus(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, EvoxLoadRequest*, request);
	if (!request) {
		panic(frame, "Invalid load request");
		return;
	}
	EX_RESULT(frame, (i32)request->status);
}

static void evox_load_getError(ex_runtime* runtime, ex_call_frame frame) {
	EX_ARG(frame, EvoxLoadRequest*, request);
	if (!request) {
		panic(frame, "Invalid load request");
		return;
	}
	const char* error = request->error;
	ex_result_string(runtime, &frame, ex_string_view{error, (i64)strlen(error)});
}

static void evox_load_destroy(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, EvoxLoadRequest*, request);
	if (!request) {
		panic(frame, "Invalid load request");
		return;
	}
	LUMIX_DELETE(request->world->getAllocator(), request);
}

// Matches core:procedural_geom.Vertex and shaders/procedural_geom.hlsl.
struct MeshVertex {
	Vec3 position;
	Vec2 uv;
	Vec3 normal;
	Vec3 tangent;
};
static_assert(sizeof(MeshVertex) == 44);
static_assert(offsetof(MeshVertex, uv) == 12);
static_assert(offsetof(MeshVertex, normal) == 20);
static_assert(offsetof(MeshVertex, tangent) == 32);

static void setMesh(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, i32, entity_index);
	EX_ARG(frame, u32, entity_padding);
	EX_ARG(frame, World*, world);
	EX_ARG(frame, ex_slice, vertices);
	EX_ARG(frame, ex_slice, indices);

	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	if (entity_index < 0 || !world->hasEntity(EntityRef{entity_index})
		|| !world->hasComponent(EntityRef{entity_index}, reflection::getComponentType("procedural_geom"))) {
		panic(frame, "Invalid procedural geometry entity");
		return;
	}
	if (!vertices.data || vertices.length <= 0 || vertices.length > 1'000'000
		|| !indices.data || indices.length <= 0 || indices.length > 3'000'000 || indices.length % 3 != 0) {
		panic(frame, "Invalid procedural mesh data");
		return;
	}
	const u32 vertex_count = u32(vertices.length);
	const u32* index_data = (const u32*)indices.data;
	for (i64 i = 0; i < indices.length; ++i) {
		if (index_data[i] >= vertex_count) {
			panic(frame, "Procedural mesh index out of bounds");
			return;
		}
	}
	RenderModule* renderer = (RenderModule*)world->getModule("renderer");
	if (!renderer) {
		panic(frame, "Renderer module not found");
		return;
	}
	gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLES);
	decl.addAttribute(0, 3, gpu::AttributeType::FLOAT, 0);
	decl.addAttribute(12, 2, gpu::AttributeType::FLOAT, 0);
	decl.addAttribute(20, 3, gpu::AttributeType::FLOAT, 0);
	decl.addAttribute(32, 3, gpu::AttributeType::FLOAT, 0);
	renderer->setProceduralGeometry(EntityRef{entity_index}
		, Span<const u8>(vertices.data, vertex_count * sizeof(MeshVertex))
		, decl
		, Span<const u8>(indices.data, u32(indices.length) * sizeof(u32))
		, gpu::DataType::U32);
	EX_RESULT(frame, u8(1));
}

static void logErrorString(ex_string_view v) {
	logError(StringView(v.begin, (u64)v.length));
}

static void logInfoString(ex_string_view v) {
	logInfo(StringView(v.begin, (u64)v.length));
}

static void inputGetInput(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, Engine*, engine);
	if (!engine) {
		panic(frame, "Invalid engine");
		return;
	}
	EX_RESULT(frame, &engine->getInputSystem());
}

static void inputGetEventCount(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, InputSystem*, input);
	if (!input) {
		panic(frame, "Invalid input system");
		return;
	}
	EX_RESULT(frame, (i32)input->getEvents().length());
}

static void inputGetEvent(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, InputSystem*, input);
	EX_ARG(frame, i32, idx);
	if (!input) {
		panic(frame, "Invalid input system");
		return;
	}
	if (idx < 0 || (u32)idx >= input->getEvents().length()) {
		panic(frame, "Invalid input event index");
		return;
	}
	const InputSystem::Event& event = input->getEvents()[idx];
	EX_RESULT(frame, (i32)event.type);
	EX_RESULT(frame, (i32)event.device->type);
	EX_RESULT(frame, (i32)event.device->index);

	switch (event.type) {
		case InputEventType::BUTTON:
			EX_RESULT(frame, (i32)event.data.button.key_id);
			EX_RESULT(frame, event.data.button.down);
			EX_RESULT(frame, event.data.button.is_repeat);
			EX_RESULT(frame, event.data.button.x);
			EX_RESULT(frame, event.data.button.y);
			break;
		case InputEventType::KEYBOARD:
			EX_RESULT(frame, (u8)event.data.keyboard.keycode);
			EX_RESULT(frame, event.data.keyboard.down);
			EX_RESULT(frame, event.data.keyboard.is_repeat);
			break;
		case InputEventType::MOUSE_BUTTON:
			EX_RESULT(frame, (i32)event.data.mouse_button.button);
			EX_RESULT(frame, event.data.mouse_button.down);
			EX_RESULT(frame, event.data.mouse_button.x);
			EX_RESULT(frame, event.data.mouse_button.y);
			break;
		case InputEventType::AXIS:
			EX_RESULT(frame, event.data.axis.x);
			EX_RESULT(frame, event.data.axis.y);
			EX_RESULT(frame, event.data.axis.x_abs);
			EX_RESULT(frame, event.data.axis.y_abs);
			EX_RESULT(frame, (i32)event.data.axis.axis);
			break;
		case InputEventType::MOUSE_WHEEL:
			EX_RESULT(frame, event.data.mouse_wheel.x);
			EX_RESULT(frame, event.data.mouse_wheel.y);
			break;
		case InputEventType::TEXT_INPUT: EX_RESULT(frame, (i32)event.data.text.utf8); break;
		case InputEventType::DEVICE_ADDED:
		case InputEventType::DEVICE_REMOVED: break;
	}
}

static bool imguiBegin(ex_string_view sv) {
	StaticString<256> title(StringView{sv.begin, (u64)sv.length});
	return ImGui::Begin(title);
}

static void imguiEnd() {
	ImGui::End();
}

static void imguiTextUnformatted(ex_string_view sv) {
	ImGui::TextUnformatted(sv.begin, sv.begin + sv.length);
}

static bool imguiButton(ex_string_view sv) {
	StaticString<256> label(StringView{sv.begin, (u64)sv.length});
	return ImGui::Button(label);
}

static ExEntity evox_world_createEntity(World* world) {
	return ExEntity(world->createEntity({0, 0, 0}, Quat::IDENTITY).index, world);
}

static bool evox_world_hasEntity(World* world, ExEntity entity) {
	return world && entity.world == world && entity.index >= 0 && world->hasEntity(EntityRef{entity.index});
}

static void evox_world_destroyEntity(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	const ExEntity entity = readArg<ExEntity>(frame);
	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	if (!evox_world_hasEntity(world, entity)) {
		panic(frame, "Invalid entity for world");
		return;
	}
	world->destroyEntity(EntityRef{entity.index});
}

static void evox_world_findByName(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	char name[128];
	EX_STRING_ARG(frame, name_sv);
	const i64 name_len = name_sv.length;
	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	if (name_len < 0 || name_len >= sizeof(name) || (name_len > 0 && !name_sv.begin)) {
		panic(frame, "Invalid entity name");
		return;
	}
	if (name_len > 0) memcpy(name, name_sv.begin, name_len);
	name[name_len] = '\0';
	const EntityPtr entity = world->findByName(INVALID_ENTITY, name);
	if (!entity.isValid()) {
		EX_RESULT(frame, u8(0));
		EX_RESULT(frame, ExEntity(i32(0), nullptr));
		return;
	}
	EX_RESULT(frame, u8(1));
	EX_RESULT(frame, ExEntity(entity.index, world));
}

static void evox_world_getPartitionCount(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	EX_RESULT(frame, (i32)world->getPartitions().size());
}

static void evox_world_getPartitionHandle(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	EX_ARG(frame, i32, index);
	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	if (index < 0 || index >= world->getPartitions().size()) {
		panic(frame, "Invalid partition index");
		return;
	}
	EX_RESULT(frame, u32(world->getPartitions()[index].handle));
}

static void evox_world_getActivePartition(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	EX_RESULT(frame, u32(world->getActivePartition()));
}

static void evox_world_createPartition(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	EX_STRING_ARG(frame, name_sv);
	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	if (name_sv.length < 0 || name_sv.length >= 64 || (name_sv.length > 0 && !name_sv.begin)) {
		panic(frame, "Invalid partition name");
		return;
	}
	char name[64];
	if (name_sv.length > 0) memcpy(name, name_sv.begin, name_sv.length);
	name[name_sv.length] = '\0';
	EX_RESULT(frame, u32(world->createPartition(name)));
}

static void evox_world_setActivePartition(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	EX_ARG(frame, u32, handle);
	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	for (const World::Partition& partition : world->getPartitions()) {
		if (partition.handle == handle) {
			world->setActivePartition(World::PartitionHandle(handle));
			return;
		}
	}
	panic(frame, "Partition not found");
}

static void evox_world_destroyPartition(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	EX_ARG(frame, u32, handle);
	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	for (const World::Partition& partition : world->getPartitions()) {
		if (partition.handle == handle) {
			world->destroyPartition(World::PartitionHandle(handle));
			return;
		}
	}
	panic(frame, "Partition not found");
}

static void evox_world_getPartitionName(ex_runtime* runtime, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	EX_ARG(frame, u32, handle);
	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	for (const World::Partition& partition : world->getPartitions()) {
		if (partition.handle == handle) {
			ex_result_string(runtime, &frame, ex_string_view{partition.name, (i64)strlen(partition.name)});
			return;
		}
	}
	panic(frame, "Partition not found");
}

static void evox_world_getEvoxDataRaw(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	EX_ARG(frame, u32, type_index);

	if (!world) {
		panic(frame, "Invalid world");
		return;
	}
	EvoxModule* module = static_cast<EvoxModule*>(world->getModule(reflection::getComponentType("evox")));
	if (!module) {
		panic(frame, "Evox module not found");
		return;
	}
	const Span<const ex_type*> types = module->getEvoxDataTypes();
	if (types.size() == 0) {
		panic(frame, "Evox module has no data types");
		return;
	}
	const ex_type* type = ex_bytecode_type(types[0]->bytecode, type_index);
	if (!type) {
		panic(frame, "Invalid Evox data type");
		return;
	}

	const ex_string_view name = ex_type_get_name(type);
	StaticString<256> type_name(StringView{name.begin, (u64)name.length});
	const Span<const u8> data = module->getEvoxData(type_name);
	const ex_slice result = {const_cast<u8*>(data.begin()), data.length()};
	EX_RESULT(frame, result);
}

static bool evox_entity_isValid(ExEntity entity) {
	return entity.world && entity.index >= 0 && entity.world->hasEntity(EntityRef{entity.index});
}

static void evox_entity_findChildByName(ex_runtime*, ex_call_frame frame) {
	const ExEntity parent = readArg<ExEntity>(frame);
	char name[128];
	EX_STRING_ARG(frame, name_sv);
	const i64 name_len = name_sv.length;
	if (!evox_entity_isValid(parent)) {
		panic(frame, "Invalid parent entity");
		return;
	}
	if (name_len < 0 || name_len >= sizeof(name) || (name_len > 0 && !name_sv.begin)) {
		panic(frame, "Invalid child name");
		return;
	}
	if (name_len > 0) memcpy(name, name_sv.begin, name_len);
	name[name_len] = '\0';
	const EntityPtr entity = parent.world->findByName(EntityPtr{parent.index}, name);
	if (!entity.isValid()) {
		EX_RESULT(frame, u8(0));
		EX_RESULT(frame, ExEntity(i32(0), nullptr));
		return;
	}
	EX_RESULT(frame, u8(1));
	EX_RESULT(frame, ExEntity(entity.index, parent.world));
}

static void evox_entity_getFirstChild(ex_runtime*, ex_call_frame frame) {
	const ExEntity parent = readArg<ExEntity>(frame);
	if (!evox_entity_isValid(parent)) {
		panic(frame, "Invalid parent entity");
		return;
	}
	const EntityPtr entity = parent.world->getFirstChild(EntityRef{parent.index});
	if (!entity.isValid()) {
		EX_RESULT(frame, u8(0));
		EX_RESULT(frame, ExEntity(i32(0), nullptr));
		return;
	}
	EX_RESULT(frame, u8(1));
	EX_RESULT(frame, ExEntity(entity.index, parent.world));
}

static void evox_entity_getNextSibling(ex_runtime*, ex_call_frame frame) {
	const ExEntity entity = readArg<ExEntity>(frame);
	if (!evox_entity_isValid(entity)) {
		panic(frame, "Invalid entity");
		return;
	}
	const EntityPtr sibling = entity.world->getNextSibling(EntityRef{entity.index});
	if (!sibling.isValid()) {
		EX_RESULT(frame, u8(0));
		EX_RESULT(frame, ExEntity(i32(0), nullptr));
		return;
	}
	EX_RESULT(frame, u8(1));
	EX_RESULT(frame, ExEntity(sibling.index, entity.world));
}

static void evox_entity_getParent(ex_runtime*, ex_call_frame frame) {
	const ExEntity entity = readArg<ExEntity>(frame);
	if (!evox_entity_isValid(entity)) {
		panic(frame, "Invalid entity");
		return;
	}
	const EntityPtr parent = entity.world->getParent(EntityRef{entity.index});
	if (!parent.isValid()) {
		EX_RESULT(frame, u8(0));
		EX_RESULT(frame, ExEntity(i32(0), nullptr));
		return;
	}
	EX_RESULT(frame, u8(1));
	EX_RESULT(frame, ExEntity(parent.index, entity.world));
}

static void evox_entity_destroy(ExEntity entity) {
	entity.world->destroyEntity(EntityRef{entity.index});
}

static void evox_entity_getName(ex_runtime*, ex_call_frame frame) {
	const ExEntity entity = readArg<ExEntity>(frame);
	if (!evox_entity_isValid(entity)) {
		panic(frame, "Invalid entity");
		return;
	}
	const char* name = entity.world->getEntityName(EntityRef{entity.index});
	const ex_slice result = {(u8*)name, (i64)strlen(name)};
	EX_RESULT(frame, result);
}

static void evox_entity_setName(ex_runtime*, ex_call_frame frame) {
	const ExEntity entity = readArg<ExEntity>(frame);
	EX_STRING_ARG(frame, name);
	if (!evox_entity_isValid(entity)) {
		panic(frame, "Invalid entity");
		return;
	}
	if (name.length < 0 || (name.length > 0 && !name.begin)) {
		panic(frame, "Invalid entity name");
		return;
	}
	entity.world->setEntityName(EntityRef{entity.index}, StringView{name.begin, (u64)name.length});
}

static void evox_entity_setPosition(ExEntity entity, double x, double y, double z) {
	entity.world->setPosition(EntityRef{entity.index}, DVec3(x, y, z));
}

static DVec3 evox_entity_getPosition(ExEntity entity) {
	return entity.world->getPosition(EntityRef{entity.index});
}

static void evox_entity_setRotation(ExEntity entity, float x, float y, float z, float w) {
	entity.world->setRotation(EntityRef{entity.index}, Quat(x, y, z, w));
}

static Quat evox_entity_getRotation(ExEntity entity) {
	return entity.world->getRotation(EntityRef{entity.index});
}

static void evox_entity_setScale(ExEntity entity, float x, float y, float z) {
	entity.world->setScale(EntityRef{entity.index}, Vec3(x, y, z));
}

static Vec3 evox_entity_getScale(ExEntity entity) {
	return entity.world->getScale(EntityRef{entity.index});
}

void registerImguiModule(NativeFunctionMap& functions) {
	functions.insert({"core:imgui", "begin"}, &wrap<imguiBegin>);
	functions.insert({"core:imgui", "textUnformatted"}, &wrap<imguiTextUnformatted>);
	functions.insert({"core:imgui", "button"}, &wrap<imguiButton>);
	functions.insert({"core:imgui", "end"}, &wrap<imguiEnd>);
}

} // namespace

void gatherCoreFunctions(NativeFunctionMap& functions) {
	generated::registerGeneratedEngineImport(functions);
	registerImguiModule(functions);
	functions.insert({"core:procedural_geom", "setMeshRaw"}, &setMesh);
	// input
	functions.insert({"core:input", "input"}, &inputGetInput);
	functions.insert({"core:input", "getEventCount"}, &inputGetEventCount);
	functions.insert({"core:input", "getEvent"}, &inputGetEvent);
	// log
	functions.insert({"core:log", "logErrorString"}, &wrap<logErrorString>);
	functions.insert({"core:log", "logInfoString"}, &wrap<logInfoString>);
	// entity
	functions.insert({"core:entity", "destroy"}, &wrap<evox_entity_destroy>);
	functions.insert({"core:entity", "isValid"}, &wrap<evox_entity_isValid>);
	functions.insert({"core:entity", "getName"}, &evox_entity_getName);
	functions.insert({"core:entity", "setName"}, &evox_entity_setName);
	functions.insert({"core:entity", "getPosition"}, &wrap<evox_entity_getPosition>);
	functions.insert({"core:entity", "getRotation"}, &wrap<evox_entity_getRotation>);
	functions.insert({"core:entity", "getScale"}, &wrap<evox_entity_getScale>);
	functions.insert({"core:entity", "setPosition"}, &wrap<evox_entity_setPosition>);
	functions.insert({"core:entity", "setScale"}, &wrap<evox_entity_setScale>);
	functions.insert({"core:entity", "setRotation"}, &wrap<evox_entity_setRotation>);
	functions.insert({"core:entity", "findChildByName"}, &evox_entity_findChildByName);
	functions.insert({"core:entity", "getFirstChild"}, &evox_entity_getFirstChild);
	functions.insert({"core:entity", "getNextSibling"}, &evox_entity_getNextSibling);
	functions.insert({"core:entity", "getParent"}, &evox_entity_getParent);
	// world
	functions.insert({"core:world", "createEntity"}, &wrap<evox_world_createEntity>);
	functions.insert({"core:world", "destroyEntity"}, &evox_world_destroyEntity);
	functions.insert({"core:world", "findByName"}, &evox_world_findByName);
	functions.insert({"core:world", "getPartitionCount"}, &evox_world_getPartitionCount);
	functions.insert({"core:world", "getPartitionHandle"}, &evox_world_getPartitionHandle);
	functions.insert({"core:world", "getPartitionName"}, &evox_world_getPartitionName);
	functions.insert({"core:world", "getActivePartition"}, &evox_world_getActivePartition);
	functions.insert({"core:world", "createPartition"}, &evox_world_createPartition);
	functions.insert({"core:world", "setActivePartition"}, &evox_world_setActivePartition);
	functions.insert({"core:world", "destroyPartition"}, &evox_world_destroyPartition);
	functions.insert({"core:world", "load"}, &evox_world_load);
	functions.insert({"core:world", "getStatus"}, &evox_load_getStatus);
	functions.insert({"core:world", "getError"}, &evox_load_getError);
	functions.insert({"core:world", "destroy"}, &evox_load_destroy);
	functions.insert({"core:world", "hasEntity"}, &wrap<evox_world_hasEntity>);
	functions.insert({"core:world", "getEvoxDataRaw"}, &evox_world_getEvoxDataRaw);
}

} // namespace Lumix::Evox
