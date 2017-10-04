#include "property_grid.h"
#include "asset_browser.h"
#include "editor/prefab_system.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/blob.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/iproperty_descriptor.h"
#include "engine/math_utils.h"
#include "engine/prefab.h"
#include "engine/property_register.h"
#include "engine/resource.h"
#include "engine/serializer.h"
#include "engine/vec.h"
#include "imgui/imgui.h"
#include "utils.h"
#include <cmath>
#include <cstdlib>


namespace Lumix
{


PropertyGrid::PropertyGrid(StudioApp& app)
	: m_app(app)
	, m_is_open(true)
	, m_editor(app.getWorldEditor())
	, m_plugins(app.getWorldEditor().getAllocator())
{
	m_particle_emitter_updating = true;
	m_particle_emitter_timescale = 1.0f;
	m_component_filter[0] = '\0';
	m_entity_filter[0] = '\0';
}


PropertyGrid::~PropertyGrid()
{
	for (auto* i : m_plugins)
	{
		LUMIX_DELETE(m_editor.getAllocator(), i);
	}
}


void PropertyGrid::showProperty(PropertyDescriptorBase& desc,
	int index,
	const Array<Entity>& entities,
	ComponentType cmp_type)
{
	if (desc.getType() == PropertyDescriptorBase::BLOB) return;

	OutputBlob stream(m_editor.getAllocator());
	ComponentUID first_entity_cmp;
	first_entity_cmp.type = cmp_type;
	first_entity_cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	first_entity_cmp.entity = entities[0];
	first_entity_cmp.handle = first_entity_cmp.scene->getComponent(entities[0], cmp_type);
	desc.get(first_entity_cmp, index, stream);
	InputBlob tmp(stream);

	StaticString<100> desc_name(desc.getName(), "###", (u64)&desc);

	switch (desc.getType())
	{
	case PropertyDescriptorBase::DECIMAL:
	{
		float f;
		tmp.read(f);
		auto& d = static_cast<NumericPropertyDescriptorBase<float>&>(desc);
		if (d.isInRadians()) f = Math::radiansToDegrees(f);
		if ((d.getMax() - d.getMin()) / d.getStep() <= 100)
		{
			if (ImGui::SliderFloat(desc_name, &f, d.getMin(), d.getMax()))
			{
				if (d.isInRadians()) f = Math::degreesToRadians(f);
				m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &f, sizeof(f));
			}
		}
		else
		{
			if (ImGui::DragFloat(desc_name, &f, d.getStep(), d.getMin(), d.getMax()))
			{
				if (d.isInRadians()) f = Math::degreesToRadians(f);
				m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &f, sizeof(f));
			}
		}
		break;
	}
	case PropertyDescriptorBase::INTEGER:
	{
		int i;
		tmp.read(i);
		auto& d = static_cast<NumericPropertyDescriptorBase<int>&>(desc);
		if (ImGui::DragInt(desc_name, &i, (float)d.getStep(), d.getMin(), d.getMax()))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &i, sizeof(i));
		}
		break;
	}
	case PropertyDescriptorBase::UNSIGNED_INTEGER:
	{
		unsigned int ui;
		tmp.read(ui);
		int i = (int)ui;
		if (ImGui::DragInt(desc_name, &i))
		{
			ui = (unsigned int)i;
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &ui, sizeof(ui));
		}
		break;
	}
	case PropertyDescriptorBase::BOOL:
	{
		bool b;
		tmp.read(b);
		if (ImGui::Checkbox(desc_name, &b))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &b, sizeof(b));
		}
		break;
	}
	case PropertyDescriptorBase::COLOR:
	{
		Vec3 v;
		tmp.read(v);
		if (ImGui::ColorEdit3(desc_name, &v.x))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
		}
		break;
	}
	case PropertyDescriptorBase::VEC2:
	{
		Vec2 v;
		tmp.read(v);
		if (desc.isInRadians())
		{
			v.x = Math::radiansToDegrees(v.x);
			v.y = Math::radiansToDegrees(v.y);
		}
		if (ImGui::DragFloat2(desc_name, &v.x))
		{
			if (desc.isInRadians())
			{
				v.x = Math::degreesToRadians(v.x);
				v.y = Math::degreesToRadians(v.y);
			}
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
		}
		break;
	}
	case PropertyDescriptorBase::INT2:
	{
		Int2 v;
		tmp.read(v);
		if (ImGui::DragInt2(desc_name, &v.x))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
		}
		break;
	}
	case PropertyDescriptorBase::VEC3:
	{
		Vec3 v;
		tmp.read(v);
		if (desc.isInRadians()) v = Math::radiansToDegrees(v);
		if (ImGui::DragFloat3(desc_name, &v.x))
		{
			if (desc.isInRadians()) v = Math::degreesToRadians(v);
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
		}
		break;
	}
	case PropertyDescriptorBase::VEC4:
	{
		Vec4 v;
		tmp.read(v);
		if (ImGui::DragFloat4(desc_name, &v.x))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
		}
		break;
	}
	case PropertyDescriptorBase::RESOURCE:
	{
		char buf[1024];
		copyString(buf, (const char*)stream.getData());
		auto& resource_descriptor = static_cast<IResourcePropertyDescriptor&>(desc);
		ResourceType rm_type = resource_descriptor.getResourceType();
		if (m_app.getAssetBrowser().resourceInput(
				desc.getName(), StaticString<20>("", (u64)&desc), buf, sizeof(buf), rm_type))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), buf, stringLength(buf) + 1);
		}
		break;
	}
	case PropertyDescriptorBase::STRING:
	case PropertyDescriptorBase::FILE:
	{
		char buf[1024];
		copyString(buf, (const char*)stream.getData());
		if (ImGui::InputText(desc_name, buf, sizeof(buf)))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), buf, stringLength(buf) + 1);
		}
		break;
	}
	case PropertyDescriptorBase::ARRAY:
		showArrayProperty(entities, cmp_type, static_cast<ArrayDescriptorBase&>(desc));
		break;
	case PropertyDescriptorBase::SAMPLED_FUNCTION:
		showSampledFunctionProperty(entities, cmp_type, static_cast<ISampledFunctionDescriptor&>(desc));
		break;
	case PropertyDescriptorBase::ENTITY:
		showEntityProperty(entities, cmp_type, index, static_cast<IEnumPropertyDescriptor&>(desc));
		break;
	case PropertyDescriptorBase::ENUM:
		showEnumProperty(entities, cmp_type, index, static_cast<IEnumPropertyDescriptor&>(desc));
		break;
	case PropertyDescriptorBase::BLOB:
	default:
		ASSERT(false);
		break;
	}
}


void PropertyGrid::showEntityProperty(const Array<Entity>& entities,
	ComponentType cmp_type,
	int index,
	PropertyDescriptorBase& desc)
{
	OutputBlob blob(m_editor.getAllocator());
	
	ComponentUID cmp;
	cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	cmp.type = cmp_type;
	cmp.entity = entities[0];
	cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
	desc.get(cmp, index, blob);
	Entity entity = *(Entity*)blob.getData();

	char buf[128];
	getEntityListDisplayName(m_editor, buf, lengthOf(buf), entity);
	ImGui::LabelText(desc.getName(), "%s", buf);
	ImGui::SameLine();
	ImGui::PushID(desc.getName());
	if (ImGui::Button("...")) ImGui::OpenPopup(desc.getName());
	Universe& universe = *m_editor.getUniverse();
	if (ImGui::BeginPopup(desc.getName()))
	{
		if (entity.isValid() && ImGui::Button("Select")) m_deferred_select = entity;

		ImGui::FilterInput("Filter", m_entity_filter, sizeof(m_entity_filter));
		for (auto i = universe.getFirstEntity(); i.isValid(); i = universe.getNextEntity(i))
		{
			getEntityListDisplayName(m_editor, buf, lengthOf(buf), i);
			bool show = m_entity_filter[0] == '\0' || stristr(buf, m_entity_filter) != 0;
			if (show && ImGui::Selectable(buf))
			{
				m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &i, sizeof(i));
			}
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
}


void PropertyGrid::showEnumProperty(const Array<Entity>& entities,
	ComponentType cmp_type,
	int index,
	IEnumPropertyDescriptor& desc)
{
	if(entities.size() > 1)
	{
		ImGui::LabelText(desc.getName(), "Multi-object editing not supported.");
		return;
	}

	ComponentUID cmp;
	cmp.type = cmp_type;
	cmp.entity = entities[0];
	cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
	OutputBlob blob(m_editor.getAllocator());
	desc.get(cmp, index, blob);
	int value = *(int*)blob.getData();
	int count = desc.getEnumCount(cmp.scene, cmp.handle);

	struct Data
	{
		IEnumPropertyDescriptor* descriptor;
		ComponentHandle cmp;
		IScene* scene;
	};

	auto getter = [](void* data, int index, const char** out) -> bool {
		auto* combo_data = static_cast<Data*>(data);
		*out = combo_data->descriptor->getEnumItemName(combo_data->scene, combo_data->cmp, index);
		if (!*out)
		{
			static char buf[100];
			combo_data->descriptor->getEnumItemName(
				combo_data->scene, combo_data->cmp, index, buf, lengthOf(buf));
			*out = buf;
		}

		return true;
	};

	Data data;
	data.cmp = cmp.handle;
	data.scene = cmp.scene;
	data.descriptor = &desc;

	if (ImGui::Combo(desc.getName(), &value, getter, &data, count))
	{
		m_editor.setProperty(cmp.type, index, desc, &cmp.entity, 1, &value, sizeof(value));
	}
}


void PropertyGrid::showSampledFunctionProperty(const Array<Entity>& entities,
	ComponentType cmp_type,
	ISampledFunctionDescriptor& desc)
{
	static const int MIN_COUNT = 6;

	OutputBlob blob(m_editor.getAllocator());
	ComponentUID cmp;
	cmp.type = cmp_type;
	cmp.entity = entities[0];
	cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
	desc.get(cmp, -1, blob);
	int count;
	InputBlob input(blob);
	input.read(count);
	Vec2* f = (Vec2*)input.skip(sizeof(Vec2) * count);

	auto editor = ImGui::BeginCurveEditor(desc.getName());
	if (editor.valid)
	{
		bool changed = false;

		changed |= ImGui::CurveSegment((ImVec2*)(f + 1), editor);

		for (int i = 1; i < count - 3; i += 3)
		{
			changed |= ImGui::CurveSegment((ImVec2*)(f + i), editor);

			if (changed)
			{
				f[i + 3].x = Math::maximum(f[i].x + 0.001f, f[i + 3].x);

				if (i + 3 < count)
				{
					f[i + 3].x = Math::minimum(f[i + 6].x - 0.001f, f[i + 3].x);
				}
			}

			if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0)
				&& count > MIN_COUNT && i + 3 < count - 2)
			{
				for (int j = i + 2; j < count - 3; ++j)
				{
					f[j] = f[j + 3];
				}
				count -= 3;
				*(int*)blob.getData() = count;
				changed = true;
			}
		}

		f[count - 2].x = 1;
		f[1].x = 0;
		ImGui::EndCurveEditor(editor);

		if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0))
		{
			auto mp = ImGui::GetMousePos();
			mp.x -= editor.inner_bb_min.x - 1;
			mp.y -= editor.inner_bb_min.y - 1;
			mp.x /= (editor.inner_bb_max.x - editor.inner_bb_min.x);
			mp.y /= (editor.inner_bb_max.y - editor.inner_bb_min.y);
			mp.y = 1 - mp.y;
			blob.write(ImVec2(-0.2f, 0));
			blob.write(mp);
			blob.write(ImVec2(0.2f, 0));
			count += 3;
			*(int*)blob.getData() = count;
			f = (Vec2*)((int*)blob.getData() + 1);
			changed = true;

			auto compare = [](const void* a, const void* b) -> int
			{
				float fa = ((const float*)a)[2];
				float fb = ((const float*)b)[2];
				return fa < fb ? -1 : (fa > fb) ? 1 : 0;
			};

			qsort(f, count / 3, 3 * sizeof(f[0]), compare);
		}

		if (changed)
		{
			for (int i = 2; i < count - 3; i += 3)
			{
				auto prev_p = ((Vec2*)f)[i - 1];
				auto next_p = ((Vec2*)f)[i + 2];
				auto& tangent = ((Vec2*)f)[i];
				auto& tangent2 = ((Vec2*)f)[i + 1];
				float half = 0.5f * (next_p.x - prev_p.x);
				tangent = tangent.normalized() * half;
				tangent2 = tangent2.normalized() * half;
			}

			f[0].x = 0;
			f[count - 1].x = desc.getMaxX();
			m_editor.setProperty(cmp_type, -1, desc, &entities[0], entities.size(), blob.getData(), blob.getPos());
		}
	}
}


void PropertyGrid::showArrayProperty(const Array<Entity>& entities,
	ComponentType cmp_type,
	ArrayDescriptorBase& desc)
{
	ComponentUID cmp;
	cmp.type = cmp_type;
	cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	cmp.entity = entities[0];
	cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
	StaticString<100> desc_name(desc.getName(), "###", (u64)&desc);

	if (!ImGui::CollapsingHeader(desc_name, nullptr, ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) return;
	if (entities.size() > 1)
	{
		ImGui::Text("Multi-object editing not supported.");
		return;
	}

	int count = desc.getCount(cmp);
	ImGui::PushID(&desc);
	if (desc.canAdd() && ImGui::Button("Add"))
	{
		m_editor.addArrayPropertyItem(cmp, desc);
	}
	count = desc.getCount(cmp);

	for (int i = 0; i < count; ++i)
	{
		char tmp[10];
		toCString(i, tmp, sizeof(tmp));
		ImGui::PushID(i);
		if (!desc.canRemove() || ImGui::TreeNode(tmp))
		{
			if (desc.canRemove() && ImGui::Button("Remove"))
			{
				m_editor.removeArrayPropertyItem(cmp, i, desc);
				--i;
				count = desc.getCount(cmp);
				ImGui::TreePop();
				ImGui::PopID();
				continue;
			}

			for (int j = 0; j < desc.getChildren().size(); ++j)
			{
				auto* child = desc.getChildren()[j];
				showProperty(*child, i, entities, cmp_type);
			}
			if (desc.canRemove()) ImGui::TreePop();
		}
		ImGui::PopID();
	}
	ImGui::PopID();

	if (m_deferred_select.isValid())
	{
		m_editor.selectEntities(&m_deferred_select, 1);
		m_deferred_select = INVALID_ENTITY;
	}
}


void PropertyGrid::showComponentProperties(const Array<Entity>& entities, ComponentType cmp_type)
{
	ImGuiTreeNodeFlags flags =
		ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlapMode;
	bool is_opened = ImGui::CollapsingHeader(m_app.getComponentTypeName(cmp_type), nullptr, flags);

	ImGui::PushID(cmp_type.index);
	float w = ImGui::GetContentRegionAvailWidth();
	ImGui::SameLine(w - 45);
	if (ImGui::Button("Remove"))
	{
		m_editor.destroyComponent(&entities[0], entities.size(), cmp_type);
		ImGui::PopID();
		return;
	}

	if (!is_opened)
	{
		ImGui::PopID();
		return;
	}

	auto& descs = PropertyRegister::getDescriptors(cmp_type);

	for (auto* desc : descs)
	{
		showProperty(*desc, -1, entities, cmp_type);
	}

	if (entities.size() == 1)
	{
		ComponentUID cmp;
		cmp.type = cmp_type;
		cmp.scene = m_editor.getUniverse()->getScene(cmp.type);
		cmp.entity = entities[0];
		cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
		for (auto* i : m_plugins)
		{
			i->onGUI(*this, cmp);
		}
	}

	ImGui::PopID();
}


bool PropertyGrid::entityInput(const char* label, const char* str_id, Entity& entity) const
{
	const auto& style = ImGui::GetStyle();
	float item_w = ImGui::CalcItemWidth();
	ImGui::PushItemWidth(
		item_w - ImGui::CalcTextSize("...").x - style.FramePadding.x * 2 - style.ItemSpacing.x);
	char buf[50];
	getEntityListDisplayName(m_editor, buf, sizeof(buf), entity);
	ImGui::LabelText("", "%s", buf);
	ImGui::SameLine();
	StaticString<30> popup_name("pu", str_id);
	if (ImGui::Button(StaticString<30>("...###br", str_id)))
	{
		ImGui::OpenPopup(popup_name);
	}

	if (ImGui::IsItemHoveredRect())
	{
		if (ImGui::IsMouseReleased(0) && m_app.getDragData().type == StudioApp::DragData::ENTITY)
		{
			entity = *(Entity*)m_app.getDragData().data;
			return true;
		}
	}

	ImGui::SameLine();
	ImGui::Text("%s", label);
	ImGui::PopItemWidth();

	if (ImGui::BeginPopup(popup_name))
	{
		if (entity.isValid())
		{
			if (ImGui::Button("Select current")) m_editor.selectEntities(&entity, 1);
			ImGui::SameLine();
			if (ImGui::Button("Empty"))
			{
				entity = INVALID_ENTITY;
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return true;
			}
		}
		Universe* universe = m_editor.getUniverse();
		static int current_item;
		if (ImGui::ListBoxHeader("Entities"))
		{
			for (auto i = universe->getFirstEntity(); i.isValid(); i = universe->getNextEntity(i))
			{
				getEntityListDisplayName(m_editor, buf, lengthOf(buf), i);
				if (ImGui::Selectable(buf))
				{
					ImGui::ListBoxFooter();
					entity = i;
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					return true;
				}
			}
			ImGui::ListBoxFooter();
		}

		ImGui::EndPopup();
	}
	return false;
}


void PropertyGrid::showCoreProperties(const Array<Entity>& entities)
{
	if (entities.size() == 1)
	{
		PrefabSystem& prefab_system = m_editor.getPrefabSystem();
		PrefabResource* prefab = prefab_system.getPrefabResource(entities[0]);
		if (prefab)
		{
			ImGui::SameLine();
			if (ImGui::Button("Save prefab"))
			{
				prefab_system.savePrefab(prefab->getPath());
			}
		}

		char name[256];

		ImGui::LabelText("ID", "%d", entities[0].index);
		EntityGUID guid = m_editor.getEntityGUID(entities[0]);
		if (guid == INVALID_ENTITY_GUID)
		{
			ImGui::LabelText("GUID", "%s", "runtime");
		}
		else
		{
			char guid_str[32];
			toCString(guid.value, guid_str, lengthOf(guid_str));
			ImGui::LabelText("GUID", "%s", guid_str);
		}

		Entity parent = m_editor.getUniverse()->getParent(entities[0]);
		if (parent.isValid())
		{
			getEntityListDisplayName(m_editor, name, lengthOf(name), parent);
			ImGui::LabelText("Parent", "%s", name);

			Transform tr = m_editor.getUniverse()->getLocalTransform(entities[0]);
			Vec3 old_pos = tr.pos;
			if (ImGui::DragFloat3("Local position", &tr.pos.x))
			{
				WorldEditor::Coordinate coord;
				if (tr.pos.x != old_pos.x) coord = WorldEditor::Coordinate::X;
				if (tr.pos.y != old_pos.y) coord = WorldEditor::Coordinate::Y;
				if (tr.pos.z != old_pos.z) coord = WorldEditor::Coordinate::Z;
				m_editor.setEntitiesLocalCoordinate(&entities[0], entities.size(), (&tr.pos.x)[(int)coord], coord);
			}
		}

		const char* tmp = m_editor.getUniverse()->getEntityName(entities[0]);
		copyString(name, tmp);
		if (ImGui::InputText("Name", name, sizeof(name))) m_editor.setEntityName(entities[0], name);
	}
	else
	{
		ImGui::LabelText("ID", "%s", "Multiple objects");
		ImGui::LabelText("Name", "%s", "Multi-object editing not supported.");
	}


	Vec3 pos = m_editor.getUniverse()->getPosition(entities[0]);
	Vec3 old_pos = pos;
	if (ImGui::DragFloat3("Position", &pos.x))
	{
		WorldEditor::Coordinate coord;
		if (pos.x != old_pos.x) coord = WorldEditor::Coordinate::X;
		if (pos.y != old_pos.y) coord = WorldEditor::Coordinate::Y;
		if (pos.z != old_pos.z) coord = WorldEditor::Coordinate::Z;
		m_editor.setEntitiesCoordinate(&entities[0], entities.size(), (&pos.x)[(int)coord], coord);
	}

	Universe* universe = m_editor.getUniverse();
	Quat rot = universe->getRotation(entities[0]);
	Vec3 old_euler = rot.toEuler();
	Vec3 euler = Math::radiansToDegrees(old_euler);
	if (ImGui::DragFloat3("Rotation", &euler.x))
	{
		if (euler.x <= -90.0f || euler.x >= 90.0f) euler.y = 0;
		euler.x = Math::degreesToRadians(Math::clamp(euler.x, -90.0f, 90.0f));
		euler.y = Math::degreesToRadians(fmodf(euler.y + 180, 360.0f) - 180);
		euler.z = Math::degreesToRadians(fmodf(euler.z + 180, 360.0f) - 180);
		rot.fromEuler(euler);
		
		Array<Quat> rots(m_editor.getAllocator());
		for (Entity entity : entities)
		{
			Vec3 tmp = universe->getRotation(entity).toEuler();
			
			if (fabs(euler.x - old_euler.x) > 0.01f) tmp.x = euler.x;
			if (fabs(euler.y - old_euler.y) > 0.01f) tmp.y = euler.y;
			if (fabs(euler.z - old_euler.z) > 0.01f) tmp.z = euler.z;
			rots.emplace().fromEuler(tmp);
		}
		m_editor.setEntitiesRotations(&entities[0], &rots[0], entities.size());
	}

	float scale = m_editor.getUniverse()->getScale(entities[0]);
	if (ImGui::DragFloat("Scale", &scale, 0.1f))
	{
		m_editor.setEntitiesScale(&entities[0], entities.size(), scale);
	}
}


static void showAddComponentNode(const StudioApp::AddCmpTreeNode* node, const char* filter)
{
	if (!node) return;

	if (filter[0])
	{
		if (!node->plugin) showAddComponentNode(node->child, filter);
		else if (stristr(node->plugin->getLabel(), filter)) node->plugin->onGUI(false, true);
		showAddComponentNode(node->next, filter);
		return;
	}

	if (node->plugin)
	{
		node->plugin->onGUI(false, false);
		showAddComponentNode(node->next, filter);
		return;
	}

	const char* last = reverseFind(node->label, nullptr, '/');
	if (ImGui::BeginMenu(last ? last + 1 : node->label))
	{
		showAddComponentNode(node->child, filter);
		ImGui::EndMenu();
	}
	showAddComponentNode(node->next, filter);
}


void PropertyGrid::onGUI()
{
	auto& ents = m_editor.getSelectedEntities();
	if (ImGui::BeginDock("Properties", &m_is_open) && !ents.empty())
	{
		if (ImGui::Button("Add component"))
		{
			ImGui::OpenPopup("AddComponentPopup");
		}
		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			ImGui::FilterInput("Filter", m_component_filter, sizeof(m_component_filter));
			showAddComponentNode(m_app.getAddComponentTreeRoot().child, m_component_filter);
			ImGui::EndPopup();
		}

		showCoreProperties(ents);

		Universe& universe = *m_editor.getUniverse();
		for (ComponentUID cmp = universe.getFirstComponent(ents[0]); cmp.isValid();
			 cmp = universe.getNextComponent(cmp))
		{
			showComponentProperties(ents, cmp.type);
		}
	}
	ImGui::EndDock();
}


} // namespace Lumix
