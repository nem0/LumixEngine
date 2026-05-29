#include "core/log.h"
#include "engine/input_system.h"
#include "engine/reflection.h"
#include "engine/world.h"
#include "lumscript/lumscript_capi.gen.h"
#include "lumscript/capi.h"
#include "imgui/imgui.h"
#include <string.h>

namespace Lumix::LumScript {

namespace {

static void logLogError(ls_runtime* runtime) {
	ls_string_view v = ls_to_string(runtime, -1);
	logError(StringView(v.begin, v.end));
}

static void logLogInfo(ls_runtime* runtime) {
	ls_string_view v = ls_to_string(runtime, -1);
	logInfo(StringView(v.begin, v.end));
}

static const InputSystem::Event* getInputEvent(ls_runtime* runtime) {
	return (const InputSystem::Event*)ls_to_ptr(runtime, -1);
}

static void inputGetEventCount(ls_runtime* runtime) {
	InputSystem* input = (InputSystem*)ls_to_ptr(runtime, -1);
	ls_push_i32(runtime, input ? input->getEvents().length() : 0);
}

static void inputGetEvent(ls_runtime* runtime) {
	InputSystem* input = (InputSystem*)ls_to_ptr(runtime, -1);
	const int idx = ls_to_i32(runtime, -2);
	const Span<const InputSystem::Event> events = input->getEvents();
	ls_push_ptr(runtime, (void*)&events[idx]);
}

static void inputGetType(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	ls_push_i32(runtime, event ? (i32)event->type : -1);
}

static void inputGetDeviceType(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	ls_push_i32(runtime, event && event->device ? (i32)event->device->type : -1);
}

static void inputGetDeviceIndex(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	ls_push_i32(runtime, event && event->device ? (i32)event->device->index : -1);
}

static void inputGetKeyId(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	ls_push_i32(runtime, event && event->type == InputEventType::BUTTON ? (i32)event->data.button.key_id : -1);
}

static void inputIsDown(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	ls_push_bool(runtime, event && event->type == InputEventType::BUTTON && event->data.button.down);
}

static void inputIsRepeat(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	ls_push_bool(runtime, event && event->type == InputEventType::BUTTON && event->data.button.is_repeat);
}

static void inputGetX(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	float value = 0;
	if (event) {
		switch (event->type) {
			case InputEventType::BUTTON: value = event->data.button.x; break;
			case InputEventType::AXIS: value = event->data.axis.x; break;
			case InputEventType::MOUSE_WHEEL: value = event->data.mouse_wheel.x; break;
			default: break;
		}
	}
	ls_push_f32(runtime, value);
}

static void inputGetY(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	float value = 0;
	if (event) {
		switch (event->type) {
			case InputEventType::BUTTON: value = event->data.button.y; break;
			case InputEventType::AXIS: value = event->data.axis.y; break;
			case InputEventType::MOUSE_WHEEL: value = event->data.mouse_wheel.y; break;
			default: break;
		}
	}
	ls_push_f32(runtime, value);
}

static void inputGetValue(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	float value = 0;
	if (event) {
		switch (event->type) {
			case InputEventType::AXIS: value = event->data.axis.y; break;
			case InputEventType::MOUSE_WHEEL: value = event->data.mouse_wheel.y; break;
			case InputEventType::BUTTON: value = event->data.button.down ? 1.0f : 0.0f; break;
			default: break;
		}
	}
	ls_push_f32(runtime, value);
}

static void inputGetAxis(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	ls_push_i32(runtime, event && event->type == InputEventType::AXIS ? (i32)event->data.axis.axis : -1);
}

static void inputGetText(ls_runtime* runtime) {
	const InputSystem::Event* event = getInputEvent(runtime);
	ls_push_i32(runtime, event && event->type == InputEventType::TEXT_INPUT ? (i32)event->data.text.utf8 : 0);
}

static void imguiBegin(ls_runtime* runtime) {
	ls_string_view sv = ls_to_string(runtime, -1);
	StaticString<256> title(StringView{sv.begin, sv.end});
	bool open = ImGui::Begin(title);
	ls_push_bool(runtime, open);
}

static void imguiEnd(ls_runtime* runtime) {
	ImGui::End();
}

static void imguiTextUnformatted(ls_runtime* runtime) {
	ls_string_view sv = ls_to_string(runtime, -1);
	ImGui::TextUnformatted(sv.begin, sv.end);
}

static void imguiButton(ls_runtime* runtime) {
	ls_string_view sv = ls_to_string(runtime, -1);
	StaticString<256> label(StringView{sv.begin, sv.end});
	bool clicked = ImGui::Button(label);
	ls_push_bool(runtime, clicked);
}

static void lumscript_world_createEntity(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	EntityRef entity = world->createEntity({0, 0, 0}, Quat::IDENTITY);
	ls_push_ptr(runtime, world);
	ls_push_i32(runtime, entity.index);
}

static void lumscript_world_destroyEntity(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	i32 entity_idx = ls_to_i32(runtime, -2);
	world->destroyEntity(EntityRef(entity_idx));
}

static void lumscript_world_hasEntity(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	i32 entity_idx = ls_to_i32(runtime, -2);
	ls_push_bool(runtime, entity_idx >= 0 && world->hasEntity(EntityRef(entity_idx)));
}

static void lumscript_world_findByName(ls_runtime* runtime) {
	/*World* world = (World*)ls_to_ptr(runtime, -1);
	char name[128];
	copyString(name, ls_to_string(runtime, -2));
	const EntityPtr entity = world->findByName(INVALID_ENTITY, name);
	if (!entity.isValid()) {
		ls_push_null(runtime);
		return true;
	}
	ls_push_ptr(runtime, world);
	ls_push_i32(runtime, entity.index);*/
}

static void lumscript_entity_destroy(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	i32 entity_idx = ls_to_i32(runtime, -2);
	world->destroyEntity(EntityRef(entity_idx));
}

static void lumscript_entity_isValid(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	i32 entity_idx = ls_to_i32(runtime, -2);
	ls_push_bool(runtime, world && entity_idx >= 0 && world->hasEntity(EntityRef(entity_idx)));
}

static void lumscript_entity_setPosition(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	i32 entity_idx = ls_to_i32(runtime, -2);
	//world->setPosition(EntityRef(entity_idx), toDVec3(args[1]));
}

static void lumscript_entity_getPosition(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	i32 entity_idx = ls_to_i32(runtime, -2);
	//*result = makeDVec3Value(world->getPosition(EntityRef(entity_idx)));
}

static void lumscript_entity_setRotation(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	i32 entity_idx = ls_to_i32(runtime, -2);
	//world->setRotation(EntityRef(entity_idx), toQuat(args[1]));
}

static void lumscript_entity_getRotation(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	i32 entity_idx = ls_to_i32(runtime, -2);
	//*result = makeQuatValue(world->getRotation(EntityRef(entity_idx)));
}

static void lumscript_entity_setScale(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	i32 entity_idx = ls_to_i32(runtime, -2);
	//world->setScale(EntityRef(entity_idx), toVec3(args[1]));
}

static void lumscript_entity_getScale(ls_runtime* runtime) {
	World* world = (World*)ls_to_ptr(runtime, -1);
	i32 entity_idx = ls_to_i32(runtime, -2);
	//*result = makeVec3Value(Vec3(world->getScale(EntityRef(entity_idx))));
}

void registerImguiModule(HashMap<StringView, ls_native_fn>& functions) {
	functions.insert(StringView("core:imgui.begin"), &imguiBegin);
	functions.insert(StringView("core:imgui.textUnformatted"), &imguiTextUnformatted);
	functions.insert(StringView("core:imgui.button"), &imguiButton);
	functions.insert(StringView("core:imgui.end"), &imguiEnd);
}

} // namespace

void resolveEngineImport(HashMap<StringView, ls_native_fn>& functions) {
	generated::registerGeneratedEngineImport(functions);
	registerImguiModule(functions);
	// input
	functions.insert(StringView("core:input.getEventCount"), &inputGetEventCount);
	functions.insert(StringView("core:input.getEvent"), &inputGetEvent);
	functions.insert(StringView("core:input.getType"), &inputGetType);
	functions.insert(StringView("core:input.getDeviceType"), &inputGetDeviceType);
	functions.insert(StringView("core:input.getDeviceIndex"), &inputGetDeviceIndex);
	functions.insert(StringView("core:input.getKeyId"), &inputGetKeyId);
	functions.insert(StringView("core:input.isDown"), &inputIsDown);
	functions.insert(StringView("core:input.isRepeat"), &inputIsRepeat);
	functions.insert(StringView("core:input.getX"), &inputGetX);
	functions.insert(StringView("core:input.getY"), &inputGetY);
	functions.insert(StringView("core:input.getValue"), &inputGetValue);
	functions.insert(StringView("core:input.getAxis"), &inputGetAxis);
	functions.insert(StringView("core:input.getText"), &inputGetText);
	// log
	functions.insert(StringView("core:log.logError"), &logLogError);
	functions.insert(StringView("core:log.logInfo"), &logLogInfo);
	// entity
	functions.insert(StringView("core:entity.destroy"), &lumscript_entity_destroy);
	functions.insert(StringView("core:entity.isValid"), &lumscript_entity_isValid);
	functions.insert(StringView("core:entity.getPosition"), &lumscript_entity_getPosition);
	functions.insert(StringView("core:entity.getRotation"), &lumscript_entity_getRotation);
	functions.insert(StringView("core:entity.getScale"), &lumscript_entity_getScale);
	functions.insert(StringView("core:entity.setPosition"), &lumscript_entity_setPosition);
	functions.insert(StringView("core:entity.setScale"), &lumscript_entity_setScale);
	functions.insert(StringView("core:entity.setRotation"), &lumscript_entity_setRotation);
	// world
	functions.insert(StringView("core:world.createEntity"), &lumscript_world_createEntity);
	functions.insert(StringView("core:world.destroyEntity"), &lumscript_world_destroyEntity);
	functions.insert(StringView("core:world.findByName"), &lumscript_world_findByName);
	functions.insert(StringView("core:world.hasEntity"), &lumscript_world_hasEntity);
}

} // namespace Lumix::LumScript
