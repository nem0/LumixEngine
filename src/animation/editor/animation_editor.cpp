#include "animation_editor.h"
#include "animation/animation.h"
#include "animation/animation_system.h"
#include "animation/controller.h"
#include "animation/editor/state_machine_editor.h"
#include "animation/state_machine.h"
#include "editor/asset_browser.h"
#include "editor/platform_interface.h"
#include "editor/property_grid.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
#include "engine/hash_map.h"
#include "engine/log.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"


static ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y); }

using namespace Lumix;

static const ComponentType ANIMABLE_HASH = PropertyRegister::getComponentType("animable");
static const ComponentType CONTROLLER_TYPE = PropertyRegister::getComponentType("anim_controller");
static const ResourceType ANIMATION_TYPE("animation");
static const ResourceType CONTROLLER_RESOURCE_TYPE("anim_controller");


namespace AnimEditor
{


AnimationEditor::AnimationEditor(StudioApp& app)
	: m_app(app)
	, m_editor_opened(false)
	, m_inputs_opened(false)
	, m_offset(0, 0)
{
	m_path[0] = 0;
	IAllocator& allocator = app.getWorldEditor()->getAllocator();

	auto* action = LUMIX_NEW(allocator, Action)("Animation Editor", "animation_editor");
	action->func.bind<AnimationEditor, &AnimationEditor::toggleEditorOpened>(this);
	action->is_selected.bind<AnimationEditor, &AnimationEditor::isEditorOpened>(this);
	app.addWindowAction(action);

	action = LUMIX_NEW(allocator, Action)("Animation Inputs", "animation_inputs");
	action->func.bind<AnimationEditor, &AnimationEditor::toggleInputsOpened>(this);
	action->is_selected.bind<AnimationEditor, &AnimationEditor::isInputsOpened>(this);
	app.addWindowAction(action);

	auto* manager = m_app.getWorldEditor()->getEngine().getResourceManager().get(CONTROLLER_RESOURCE_TYPE);
	m_resource = LUMIX_NEW(allocator, ControllerResource)(*this, *manager, allocator);
	m_container = (Container*)m_resource->getRoot();
}


AnimationEditor::~AnimationEditor()
{
	IAllocator& allocator = m_app.getWorldEditor()->getAllocator();
	LUMIX_DELETE(allocator, m_resource);
}



void AnimationEditor::onWindowGUI()
{
	editorGUI();
	inputsGUI();
}


void AnimationEditor::saveAs()
{
	if (!PlatformInterface::getSaveFilename(m_path, lengthOf(m_path), "Animation controllers\0*.act\0", "")) return;
	save();
}


void AnimationEditor::save()
{
	if (m_path[0] == 0 && !PlatformInterface::getSaveFilename(m_path, lengthOf(m_path), "Animation controllers\0*.act\0", "")) return;
	IAllocator& allocator = m_app.getWorldEditor()->getAllocator();
	OutputBlob blob(allocator);
	m_resource->serialize(blob);
	FS::OsFile file;
	file.open(m_path, FS::Mode::CREATE_AND_WRITE, allocator);
	file.write(blob.getData(), blob.getPos());
	file.close();
}


void AnimationEditor::drawGraph()
{
	ImGui::BeginChild("canvas", ImVec2(0, 0), true);
	if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
	{
		m_offset = m_offset + ImGui::GetIO().MouseDelta;
	}

	auto* scene = (AnimationScene*)m_app.getWorldEditor()->getUniverse()->getScene(ANIMABLE_HASH);
	auto& entities = m_app.getWorldEditor()->getSelectedEntities();
	Anim::ComponentInstance* runtime = nullptr;
	if (!entities.empty())
	{
		ComponentHandle ctrl = scene->getComponent(entities[0], CONTROLLER_TYPE);
		if (isValid(ctrl))
		{
			runtime = scene->getControllerRoot(ctrl);
		}
	}

	ImDrawList* draw = ImGui::GetWindowDrawList();
	auto canvas_screen_pos = ImGui::GetCursorScreenPos() + m_offset;
	m_container->drawInside(draw, canvas_screen_pos);
	if(runtime) m_resource->getRoot()->debugInside(draw, canvas_screen_pos, runtime, m_container);

	if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(0) && m_app.getDragData().type == StudioApp::DragData::PATH)
	{
		dropFile((const char*)m_app.getDragData().data, canvas_screen_pos);
	}
	ImGui::EndChild();
}


void AnimationEditor::dropFile(const char* path, const ImVec2& canvas_screen_pos)
{
	if (!PathUtils::hasExtension(path, "ani")) return;

	Anim::ControllerResource* ctrl_res = m_resource->getEngineResource();
	auto& anim_set = ctrl_res->getAnimSet();
	u32 anim_slot = 0;
	char name[64];
	for (auto iter = anim_set.begin(), end = anim_set.end(); iter != end; ++iter)
	{
		if (!iter.value()) continue;
		if (equalStrings(iter.value()->getPath().c_str(), path))
		{
			anim_slot = iter.key();
			copyString(name, m_resource->getAnimationSlot(anim_slot));
			break;
		}
	}

	if (anim_slot == 0)
	{
		PathUtils::getBasename(name, lengthOf(name), path);
		m_resource->createAnimSlot(name, path);
		anim_slot = crc32(name);
	}

	m_container->dropSlot(name, anim_slot, canvas_screen_pos);
}


void AnimationEditor::loadFromEntity()
{
	auto& entities = m_app.getWorldEditor()->getSelectedEntities();
	if (entities.empty()) return;
	auto* scene = (AnimationScene*)m_app.getWorldEditor()->getUniverse()->getScene(ANIMABLE_HASH);
	ComponentHandle ctrl = scene->getComponent(entities[0], CONTROLLER_TYPE);
	if (!isValid(ctrl)) return;
	copyString(m_path, scene->getControllerSource(ctrl).c_str());
	load();
}


void AnimationEditor::load()
{
	IAllocator& allocator = m_app.getWorldEditor()->getAllocator();
	FS::OsFile file;
	file.open(m_path, FS::Mode::OPEN_AND_READ, allocator);
	Array<u8> data(allocator);
	data.resize((int)file.size());
	file.read(&data[0], data.size());
	InputBlob blob(&data[0], data.size());
	m_resource->deserialize(blob, m_app.getWorldEditor()->getEngine(), allocator);
	m_container = (Container*)m_resource->getRoot();
	file.close();
}


void AnimationEditor::loadFromFile()
{
	if (!PlatformInterface::getOpenFilename(m_path, lengthOf(m_path), "Animation controllers\0*.act\0", "")) return;
	load();
}


void AnimationEditor::menuGUI()
{
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Save")) save();
			if (ImGui::MenuItem("Save As")) saveAs();
			if (ImGui::MenuItem("Open")) loadFromFile();
			if (ImGui::MenuItem("Open from selected entity")) loadFromEntity();
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Go up", nullptr, false, m_container->getParent() != nullptr))
		{
			m_container = m_container->getParent();
		}
		ImGui::EndMenuBar();
	}
}


void AnimationEditor::editorGUI()
{
	if (ImGui::BeginDock("Animation Editor", &m_editor_opened, ImGuiWindowFlags_MenuBar))
	{
		menuGUI();
		ImGui::Columns(2);
		drawGraph();
		ImGui::NextColumn();
		ImGui::Text("Properties");
		if(m_container->getSelectedComponent()) m_container->getSelectedComponent()->onGUI();
		ImGui::Columns();
	}
	ImGui::EndDock();
}


void AnimationEditor::inputsGUI()
{
	if (ImGui::BeginDock("Animation inputs", &m_inputs_opened))
	{
		if (ImGui::CollapsingHeader("Inputs"))
		{
			const auto& selected_entities = m_app.getWorldEditor()->getSelectedEntities();
			auto* scene = (AnimationScene*)m_app.getWorldEditor()->getUniverse()->getScene(ANIMABLE_HASH);
			ComponentHandle cmp = selected_entities.empty() ? INVALID_COMPONENT : scene->getComponent(selected_entities[0], CONTROLLER_TYPE);
			u8* input_data = isValid(cmp) ? scene->getControllerInput(cmp) : nullptr;
			Anim::InputDecl& input_decl = m_resource->getEngineResource()->getInputDecl();

			for (int i = 0; i < input_decl.inputs_count; ++i)
			{
				ImGui::PushID(i);
				auto& input = input_decl.inputs[i];
				ImGui::PushItemWidth(100);
				ImGui::InputText("##name", input.name, lengthOf(input.name));
				ImGui::SameLine();
				if (ImGui::Combo("##type", (int*)&input.type, "float\0int\0bool\0"))
				{
					input_decl.recalculateOffsets();
				}
				if (input_data)
				{
					ImGui::SameLine();
					switch (input.type)
					{
						case Anim::InputDecl::FLOAT: ImGui::DragFloat("##value", (float*)(input_data + input.offset)); break;
						case Anim::InputDecl::BOOL: ImGui::Checkbox("##value", (bool*)(input_data + input.offset)); break;
						case Anim::InputDecl::INT: ImGui::InputInt("##value", (int*)(input_data + input.offset)); break;
						default: ASSERT(false); break;
					}
				}
				ImGui::PopItemWidth();
				ImGui::PopID();
			}

			if (ImGui::Button("Add"))
			{
				auto& input = input_decl.inputs[input_decl.inputs_count];
				input.name[0] = 0;
				input.type = Anim::InputDecl::BOOL;
				input.offset = input_decl.getSize();
				++input_decl.inputs_count;
			}
		}

		constantsGUI();
		animSetGUI();
	}
	ImGui::EndDock();
}


void AnimationEditor::constantsGUI()
{
	if (!ImGui::CollapsingHeader("Constants")) return;

	Anim::InputDecl& input_decl = m_resource->getEngineResource()->getInputDecl();
	for (int i = 0; i < input_decl.constants_count; ++i)
	{
		auto& constant = input_decl.constants[i];
		StaticString<20> tmp("###", i);
		ImGui::PushItemWidth(100);
		ImGui::InputText(tmp, constant.name, lengthOf(constant.name));
		ImGui::SameLine();
		tmp << "*";
		if (ImGui::Combo(tmp, (int*)&constant.type, "float\0int\0bool\0"))
		{
			input_decl.recalculateOffsets();
		}
		ImGui::SameLine();
		tmp << "*";
		switch (constant.type)
		{
			case Anim::InputDecl::FLOAT: ImGui::DragFloat(tmp, &constant.f_value); break;
			case Anim::InputDecl::BOOL: ImGui::Checkbox(tmp, &constant.b_value); break;
			case Anim::InputDecl::INT: ImGui::InputInt(tmp, &constant.i_value); break;
			default: ASSERT(false); break;
		}
		ImGui::PopItemWidth();
	}

	if (ImGui::Button("Add##add_const"))
	{
		auto& constant = input_decl.constants[input_decl.constants_count];
		constant.name[0] = 0;
		constant.type = Anim::InputDecl::BOOL;
		constant.b_value = true;
		++input_decl.constants_count;
	}
}


void AnimationEditor::animSetGUI()
{
	if (!ImGui::CollapsingHeader("Anim sets")) return;
	ImGui::PushID("anim_set");
	auto& engine_anim_set = m_resource->getEngineResource()->getAnimSet();
	auto& slots = m_resource->getAnimationSlots();
	int i = 0;
	ImGui::Columns(3);
	for (auto& slot : slots)
	{
		ImGui::PushID(i);
		++i;
		char slot_cstr[64];
		copyString(slot_cstr, slot.c_str());

		auto iter = engine_anim_set.find(crc32(slot.c_str()));
		ASSERT(iter.isValid());
		Animation* anim = iter.value();
		ImGui::PushItemWidth(-1);
		
		if (ImGui::InputText("##name", slot_cstr, lengthOf(slot_cstr), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			if (engine_anim_set.find(crc32(slot_cstr)).isValid())
			{
				g_log_error.log("Animation") << "Slot " << slot_cstr << " already exists.";
			}
			else
			{
				engine_anim_set.erase(iter);
				slot = slot_cstr;
				engine_anim_set.insert(crc32(slot_cstr), anim);
			}
		}
		ImGui::PopItemWidth();
		ImGui::NextColumn();
		char tmp[MAX_PATH_LENGTH];
		copyString(tmp, anim ? anim->getPath().c_str() : "");
		ImGui::PushItemWidth(ImGui::GetColumnWidth());
		if (m_app.getAssetBrowser()->resourceInput("", "##res", tmp, lengthOf(tmp), ANIMATION_TYPE))
		{
			if (anim) anim->getResourceManager().unload(*anim);
			auto* manager = m_app.getWorldEditor()->getEngine().getResourceManager().get(ANIMATION_TYPE);
			anim = (Animation*)manager->load(Path(tmp));
			engine_anim_set[crc32(slot_cstr)] = anim;
		}
		ImGui::PopItemWidth();
		ImGui::NextColumn();
		if (ImGui::Button("Remove"))
		{
			m_resource->getEngineResource()->getAnimSet().erase(crc32(slot.c_str()));
			slots.eraseItemFast(slot);
		}
		ImGui::NextColumn();
		ImGui::PopID();
	}
	ImGui::Columns();
	if (ImGui::Button("Add"))
	{
		if (!engine_anim_set.find(0).isValid())
		{
			slots.emplace("", m_app.getWorldEditor()->getAllocator());
			engine_anim_set.insert(0, nullptr);
		}
	}
	ImGui::PopID();
}


}