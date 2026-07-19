#include "core/log.h"
#include "engine/input_system.h"
#include "engine/reflection.h"
#include "engine/world.h"
#include "imgui/imgui.h"
#include "lumscript/capi.h"
#include "lumscript/lumscript_module.h"
#include "lumscript/lumscript_capi.gen.h"
#include "lumscript/lumscript_wrapper.h"
#include <string.h>

namespace Lumix::LumScript {

struct LumScriptEntity {
	World* world;
	EntityRef entity;
};

template <> inline LumScriptEntity readArg<LumScriptEntity>(ls_call_frame& frame) {
	LS_ARG(frame, i32, entity_index);
	LS_ARG(frame, World*, world);
	return {world, EntityRef(entity_index)};
}

void writeResult(ls_runtime*, ls_call_frame& frame, const LumScriptEntity& value) {
	LS_RESULT(frame, value.entity.index);
	LS_RESULT(frame, value.world);
}

namespace {

using NativeFunctionMap = HashMap<NativeFunctionKey, ls_native_fn, NativeFunctionKeyHash>;

static void logLogError(ls_string_view v) {
	logError(StringView(v.begin, v.end));
}

static void logLogInfo(ls_string_view v) {
	logInfo(StringView(v.begin, v.end));
}

static i32 inputGetEventCount(InputSystem* input) {
	return input ? input->getEvents().length() : 0;
}

static void inputGetEvent(ls_runtime*, ls_call_frame frame) {
	LS_ARG(frame, InputSystem*, input);
	LS_ARG(frame, i32, idx);
	const InputSystem::Event& event = input->getEvents()[idx];
	LS_RESULT(frame, (i32)event.type);
	LS_RESULT(frame, (i32)event.device->type);
	LS_RESULT(frame, (i32)event.device->index);

	switch (event.type) {
		case InputEventType::BUTTON:
			LS_RESULT(frame, (i32)event.data.button.key_id);
			LS_RESULT(frame, event.data.button.down);
			LS_RESULT(frame, event.data.button.is_repeat);
			LS_RESULT(frame, event.data.button.x);
			LS_RESULT(frame, event.data.button.y);
			break;
		case InputEventType::AXIS:
			LS_RESULT(frame, event.data.axis.x);
			LS_RESULT(frame, event.data.axis.y);
			LS_RESULT(frame, event.data.axis.x_abs);
			LS_RESULT(frame, event.data.axis.y_abs);
			LS_RESULT(frame, (i32)event.data.axis.axis);
			break;
		case InputEventType::MOUSE_WHEEL:
			LS_RESULT(frame, event.data.mouse_wheel.x);
			LS_RESULT(frame, event.data.mouse_wheel.y);
			break;
		case InputEventType::TEXT_INPUT: LS_RESULT(frame, (i32)event.data.text.utf8); break;
		case InputEventType::DEVICE_ADDED:
		case InputEventType::DEVICE_REMOVED: break;
	}
}

static bool imguiBegin(ls_string_view sv) {
	StaticString<256> title(StringView{sv.begin, sv.end});
	return ImGui::Begin(title);
}

static void imguiEnd() {
	ImGui::End();
}

static void imguiTextUnformatted(ls_string_view sv) {
	ImGui::TextUnformatted(sv.begin, sv.end);
}

static bool imguiButton(ls_string_view sv) {
	StaticString<256> label(StringView{sv.begin, sv.end});
	return ImGui::Button(label);
}

static LumScriptEntity lumscript_world_createEntity(World* world) {
	return {world, world->createEntity({0, 0, 0}, Quat::IDENTITY)};
}

static void lumscript_world_destroyEntity(LumScriptEntity entity) {
	entity.world->destroyEntity(entity.entity);
}

static bool lumscript_world_hasEntity(LumScriptEntity entity) {
	return entity.entity.index >= 0 && entity.world->hasEntity(entity.entity);
}

static void lumscript_world_findByName(ls_runtime*, ls_call_frame frame) {
	LS_ARG(frame, World*, world);
	char name[128];
	LS_STRING_ARG(frame, name_sv);
	const i32 name_len = (i32)(name_sv.end - name_sv.begin);
	if (name_len >= (i32)sizeof(name)) {
		LS_RESULT(frame, u8(0));
		return;
	}
	if (name_len > 0) memcpy(name, name_sv.begin, (size_t)name_len);
	name[name_len] = '\0';
	const EntityPtr entity = world->findByName(INVALID_ENTITY, name);
	if (!entity.isValid()) {
		LS_RESULT(frame, u8(0));
		return;
	}
	LS_RESULT(frame, u8(1));
	LS_RESULT(frame, entity.index);
	LS_RESULT(frame, world);
}

static void lumscript_entity_findChildByName(ls_runtime*, ls_call_frame frame) {
	const LumScriptEntity parent = readArg<LumScriptEntity>(frame);
	char name[128];
	LS_STRING_ARG(frame, name_sv);
	const i32 name_len = (i32)(name_sv.end - name_sv.begin);
	if (name_len >= (i32)sizeof(name)) {
		LS_RESULT(frame, u8(0));
		return;
	}
	if (name_len > 0) memcpy(name, name_sv.begin, (size_t)name_len);
	name[name_len] = '\0';
	const EntityPtr entity = parent.world->findByName(parent.entity, name);
	if (!entity.isValid()) {
		LS_RESULT(frame, u8(0));
		return;
	}
	LS_RESULT(frame, u8(1));
	LS_RESULT(frame, entity.index);
	LS_RESULT(frame, parent.world);
}

static void lumscript_entity_destroy(LumScriptEntity entity) {
	entity.world->destroyEntity(entity.entity);
}

static bool lumscript_entity_isValid(LumScriptEntity entity) {
	return entity.world && entity.entity.index >= 0 && entity.world->hasEntity(entity.entity);
}

static void lumscript_entity_setPosition(LumScriptEntity entity, double x, double y, double z) {
	entity.world->setPosition(entity.entity, DVec3(x, y, z));
}

static DVec3 lumscript_entity_getPosition(LumScriptEntity entity) {
	return entity.world->getPosition(entity.entity);
}

static void lumscript_entity_setRotation(LumScriptEntity entity, float x, float y, float z, float w) {
	entity.world->setRotation(entity.entity, Quat(x, y, z, w));
}

static Quat lumscript_entity_getRotation(LumScriptEntity entity) {
	return entity.world->getRotation(entity.entity);
}

static void lumscript_entity_setScale(LumScriptEntity entity, float x, float y, float z) {
	entity.world->setScale(entity.entity, Vec3(x, y, z));
}

static Vec3 lumscript_entity_getScale(LumScriptEntity entity) {
	return entity.world->getScale(entity.entity);
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
	functions.insert({"core:log", "logError"}, &wrap<logLogError>);
	functions.insert({"core:log", "logInfo"}, &wrap<logLogInfo>);
	// entity
	functions.insert({"core:entity", "destroy"}, &wrap<lumscript_entity_destroy>);
	functions.insert({"core:entity", "isValid"}, &wrap<lumscript_entity_isValid>);
	functions.insert({"core:entity", "getPosition"}, &wrap<lumscript_entity_getPosition>);
	functions.insert({"core:entity", "getRotation"}, &wrap<lumscript_entity_getRotation>);
	functions.insert({"core:entity", "getScale"}, &wrap<lumscript_entity_getScale>);
	functions.insert({"core:entity", "setPosition"}, &wrap<lumscript_entity_setPosition>);
	functions.insert({"core:entity", "setScale"}, &wrap<lumscript_entity_setScale>);
	functions.insert({"core:entity", "setRotation"}, &wrap<lumscript_entity_setRotation>);
	functions.insert({"core:entity", "findChildByName"}, &lumscript_entity_findChildByName);
	// world
	functions.insert({"core:world", "createEntity"}, &wrap<lumscript_world_createEntity>);
	functions.insert({"core:world", "destroyEntity"}, &wrap<lumscript_world_destroyEntity>);
	functions.insert({"core:world", "findByName"}, &lumscript_world_findByName);
	functions.insert({"core:world", "hasEntity"}, &wrap<lumscript_world_hasEntity>);
}

} // namespace Lumix::LumScript
