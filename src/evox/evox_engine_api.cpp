#include "core/log.h"
#include "engine/input_system.h"
#include "engine/reflection.h"
#include "engine/world.h"
#include "imgui/imgui.h"
#include "evox/capi.h"
#include "evox/bytecode.h"
#include "evox/evox_module.h"
#include "evox/evox_capi.gen.h"
#include "evox/evox_wrapper.h"
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

static EvoxLoadRequest* evox_world_load(World* world, ex_string_view path) {
	if (!world || path.length == 0) return nullptr;
	EvoxLoadRequest* request = LUMIX_NEW(world->getAllocator(), EvoxLoadRequest);
	request->world = world;
	Path file_path(StringView(path.begin, (u64)path.length));
	world->getEngine().getFileSystem().getContent(file_path, makeDelegate<&EvoxLoadRequest::complete>(request));
	return request;
}

static i32 evox_load_getStatus(EvoxLoadRequest* request) {
	return request ? (i32)request->status : (i32)LoadRequestStatus::FAIL;
}

static void evox_load_getError(ex_runtime* runtime, ex_call_frame frame) {
	EX_ARG(frame, EvoxLoadRequest*, request);
	const char* error = request ? request->error : "Invalid load request";
	ex_result_string(runtime, &frame, ex_string_view{error, (i64)strlen(error)});
}

static void evox_load_destroy(EvoxLoadRequest* request) {
	if (!request) return;
	LUMIX_DELETE(request->world->getAllocator(), request);
}

static void logErrorString(ex_string_view v) {
	logError(StringView(v.begin, (u64)v.length));
}

static void logInfoString(ex_string_view v) {
	logInfo(StringView(v.begin, (u64)v.length));
}

static i32 inputGetEventCount(InputSystem* input) {
	return input ? input->getEvents().length() : 0;
}

static void inputGetEvent(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, InputSystem*, input);
	EX_ARG(frame, i32, idx);
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

static void evox_world_destroyEntity(World* world, ExEntity entity) {
	if (world && entity.world == world && entity.index >= 0 && world->hasEntity(EntityRef{entity.index})) {
		world->destroyEntity(EntityRef{entity.index});
	}
}

static bool evox_world_hasEntity(World* world, ExEntity entity) {
	return world && entity.world == world && entity.index >= 0 && world->hasEntity(EntityRef{entity.index});
}

static void evox_world_findByName(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	char name[128];
	EX_STRING_ARG(frame, name_sv);
	const i64 name_len = name_sv.length;
	if (name_len >= sizeof(name)) {
		EX_RESULT(frame, u8(0));
		EX_RESULT(frame, ExEntity(i32(0), nullptr));
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

static i32 evox_world_getPartitionCount(World* world) {
	return world ? world->getPartitions().size() : 0;
}

static u32 evox_world_getPartitionHandle(World* world, i32 index) {
	if (!world || index < 0 || index >= world->getPartitions().size()) return 0xffff;
	return world->getPartitions()[index].handle;
}

static u32 evox_world_getActivePartition(World* world) {
	return world ? world->getActivePartition() : 0xffff;
}

static void evox_world_createPartition(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	EX_STRING_ARG(frame, name_sv);
	if (!world || name_sv.length >= 64) {
		EX_RESULT(frame, u32(0xffff));
		return;
	}
	char name[64];
	if (name_sv.length > 0) memcpy(name, name_sv.begin, name_sv.length);
	name[name_sv.length] = '\0';
	EX_RESULT(frame, u32(world->createPartition(name)));
}

static void evox_world_setActivePartition(World* world, u32 handle) {
	if (world) world->setActivePartition(World::PartitionHandle(handle));
}

static void evox_world_getPartitionName(ex_runtime* runtime, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	EX_ARG(frame, u32, handle);
	if (!world) {
		ex_result_string(runtime, &frame, ex_string_view{nullptr, 0});
		return;
	}
	for (const World::Partition& partition : world->getPartitions()) {
		if (partition.handle == handle) {
			ex_result_string(runtime, &frame, ex_string_view{partition.name, (i64)strlen(partition.name)});
			return;
		}
	}
	ex_result_string(runtime, &frame, ex_string_view{nullptr, 0});
}

static void evox_world_getEvoxDataRaw(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, World*, world);
	EX_ARG(frame, u32, type_index);

	ex_slice result = {};
	if (world) {
		IModule* base = world->getModule(reflection::getComponentType("evox"));
		EvoxModule* module = static_cast<EvoxModule*>(base);
		const Span<const ex_type*> types = module ? module->getEvoxDataTypes() : Span<const ex_type*>();

		if (types.size() > 0) {
			const ex_type* type = ex_bytecode_type(types[0]->bytecode, type_index);
			if (!type) {
				EX_RESULT(frame, result);
				return;
			}

			const ex_string_view name = ex_type_get_name(type);
			StaticString<256> type_name(StringView{name.begin, (u64)name.length});
			const Span<const u8> data = module->getEvoxData(type_name);
			result.data = const_cast<u8*>(data.begin());
			result.length = data.length();
		}
	}
	EX_RESULT(frame, result);
}

static void evox_entity_findChildByName(ex_runtime*, ex_call_frame frame) {
	const ExEntity parent = readArg<ExEntity>(frame);
	char name[128];
	EX_STRING_ARG(frame, name_sv);
	const i64 name_len = name_sv.length;
	if (name_len >= sizeof(name)) {
		EX_RESULT(frame, u8(0));
		EX_RESULT(frame, ExEntity(i32(0), nullptr));
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
	const EntityPtr sibling = entity.world->getNextSibling(EntityRef{entity.index});
	if (!sibling.isValid()) {
		EX_RESULT(frame, u8(0));
		EX_RESULT(frame, ExEntity(i32(0), nullptr));
		return;
	}
	EX_RESULT(frame, u8(1));
	EX_RESULT(frame, ExEntity(sibling.index, entity.world));
}

static void evox_entity_destroy(ExEntity entity) {
	entity.world->destroyEntity(EntityRef{entity.index});
}

static bool evox_entity_isValid(ExEntity entity) {
	return entity.world && entity.index >= 0 && entity.world->hasEntity(EntityRef{entity.index});
}

static void evox_entity_getName(ex_runtime*, ex_call_frame frame) {
	const ExEntity entity = readArg<ExEntity>(frame);
	ex_slice result = {};
	if (entity.world && entity.index >= 0 && entity.world->hasEntity(EntityRef{entity.index})) {
		const char* name = entity.world->getEntityName(EntityRef{entity.index});
		result.data = (u8*)name;
		result.length = (i64)strlen(name);
	}
	EX_RESULT(frame, result);
}

static void evox_entity_setName(ex_runtime*, ex_call_frame frame) {
	const ExEntity entity = readArg<ExEntity>(frame);
	EX_STRING_ARG(frame, name);
	if (entity.world && entity.index >= 0 && entity.world->hasEntity(EntityRef{entity.index})) {
		entity.world->setEntityName(EntityRef{entity.index}, StringView{name.begin, (u64)name.length});
	}
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
	// input
	functions.insert({"core:input", "getEventCount"}, &wrap<inputGetEventCount>);
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
	// world
	functions.insert({"core:world", "createEntity"}, &wrap<evox_world_createEntity>);
	functions.insert({"core:world", "destroyEntity"}, &wrap<evox_world_destroyEntity>);
	functions.insert({"core:world", "findByName"}, &evox_world_findByName);
	functions.insert({"core:world", "getPartitionCount"}, &wrap<evox_world_getPartitionCount>);
	functions.insert({"core:world", "getPartitionHandle"}, &wrap<evox_world_getPartitionHandle>);
	functions.insert({"core:world", "getPartitionName"}, &evox_world_getPartitionName);
	functions.insert({"core:world", "getActivePartition"}, &wrap<evox_world_getActivePartition>);
	functions.insert({"core:world", "createPartition"}, &evox_world_createPartition);
	functions.insert({"core:world", "setActivePartition"}, &wrap<evox_world_setActivePartition>);
	functions.insert({"core:world", "load"}, &wrap<evox_world_load>);
	functions.insert({"core:world", "getStatus"}, &wrap<evox_load_getStatus>);
	functions.insert({"core:world", "getError"}, &evox_load_getError);
	functions.insert({"core:world", "destroy"}, &wrap<evox_load_destroy>);
	functions.insert({"core:world", "hasEntity"}, &wrap<evox_world_hasEntity>);
	functions.insert({"core:world", "getEvoxDataRaw"}, &evox_world_getEvoxDataRaw);
}

} // namespace Lumix::Evox
