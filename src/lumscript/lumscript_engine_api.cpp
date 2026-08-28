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

template <> inline LsEntity readArg<LsEntity>(ls_call_frame& frame) {
	LS_ARG(frame, i32, entity_index);
	LS_ARG(frame, u32, entity_padding);
	LS_ARG(frame, World*, world);
	return LsEntity{entity_index, world};
}

void writeResult(ls_runtime*, ls_call_frame& frame, const LsEntity& value) {
	LS_RESULT(frame, value);
}

namespace {

using NativeFunctionMap = HashMap<NativeFunctionKey, ls_native_fn, NativeFunctionKeyHash>;

static void logLogError(ls_string_view v) {
	logError(StringView(v.begin, (u64)v.length));
}

static void logLogInfo(ls_string_view v) {
	logInfo(StringView(v.begin, (u64)v.length));
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
	StaticString<256> title(StringView{sv.begin, (u64)sv.length});
	return ImGui::Begin(title);
}

static void imguiEnd() {
	ImGui::End();
}

static void imguiTextUnformatted(ls_string_view sv) {
	ImGui::TextUnformatted(sv.begin, sv.begin + sv.length);
}

static bool imguiButton(ls_string_view sv) {
	StaticString<256> label(StringView{sv.begin, (u64)sv.length});
	return ImGui::Button(label);
}

static LsEntity lumscript_world_createEntity(World* world) {
	return LsEntity(world->createEntity({0, 0, 0}, Quat::IDENTITY).index, world);
}

static void lumscript_world_destroyEntity(LsEntity entity) {
	entity.world->destroyEntity(EntityRef{entity.index});
}

static bool lumscript_world_hasEntity(LsEntity entity) {
	return entity.index >= 0 && entity.world->hasEntity(EntityRef{entity.index});
}

static void lumscript_world_findByName(ls_runtime*, ls_call_frame frame) {
	LS_ARG(frame, World*, world);
	char name[128];
	LS_STRING_ARG(frame, name_sv);
	const i64 name_len = name_sv.length;
	if (name_len >= sizeof(name)) {
		LS_RESULT(frame, u8(0));
		LS_RESULT(frame, LsEntity(i32(0), nullptr));
		return;
	}
	if (name_len > 0) memcpy(name, name_sv.begin, name_len);
	name[name_len] = '\0';
	const EntityPtr entity = world->findByName(INVALID_ENTITY, name);
	if (!entity.isValid()) {
		LS_RESULT(frame, u8(0));
		LS_RESULT(frame, LsEntity(i32(0), nullptr));
		return;
	}
	LS_RESULT(frame, u8(1));
	LS_RESULT(frame, LsEntity(entity.index, world));
}

static void lumscript_entity_findChildByName(ls_runtime*, ls_call_frame frame) {
	const LsEntity parent = readArg<LsEntity>(frame);
	char name[128];
	LS_STRING_ARG(frame, name_sv);
	const i64 name_len = name_sv.length;
	if (name_len >= sizeof(name)) {
		LS_RESULT(frame, u8(0));
		LS_RESULT(frame, LsEntity(i32(0), nullptr));
		return;
	}
	if (name_len > 0) memcpy(name, name_sv.begin, name_len);
	name[name_len] = '\0';
	const EntityPtr entity = parent.world->findByName(EntityPtr{parent.index}, name);
	if (!entity.isValid()) {
		LS_RESULT(frame, u8(0));
		LS_RESULT(frame, LsEntity(i32(0), nullptr));
		return;
	}
	LS_RESULT(frame, u8(1));
	LS_RESULT(frame, LsEntity(entity.index, parent.world));
}

static void lumscript_entity_destroy(LsEntity entity) {
	entity.world->destroyEntity(EntityRef{entity.index});
}

static bool lumscript_entity_isValid(LsEntity entity) {
	return entity.world && entity.index >= 0 && entity.world->hasEntity(EntityRef{entity.index});
}

static void lumscript_entity_setPosition(LsEntity entity, double x, double y, double z) {
	entity.world->setPosition(EntityRef{entity.index}, DVec3(x, y, z));
}

static DVec3 lumscript_entity_getPosition(LsEntity entity) {
	return entity.world->getPosition(EntityRef{entity.index});
}

static void lumscript_entity_setRotation(LsEntity entity, float x, float y, float z, float w) {
	entity.world->setRotation(EntityRef{entity.index}, Quat(x, y, z, w));
}

static Quat lumscript_entity_getRotation(LsEntity entity) {
	return entity.world->getRotation(EntityRef{entity.index});
}

static void lumscript_entity_setScale(LsEntity entity, float x, float y, float z) {
	entity.world->setScale(EntityRef{entity.index}, Vec3(x, y, z));
}

static Vec3 lumscript_entity_getScale(LsEntity entity) {
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
