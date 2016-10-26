#include "property_grid.h"
#include "asset_browser.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/blob.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "engine/iproperty_descriptor.h"
#include "engine/math_utils.h"
#include "engine/property_register.h"
#include "engine/resource.h"
#include "engine/vec.h"
#include "imgui/imgui.h"
#include "utils.h"
#include <cmath>
#include <cstdlib>


PropertyGrid::PropertyGrid(StudioApp& app)
	: m_app(app)
	, m_is_opened(true)
	, m_editor(*app.getWorldEditor())
	, m_plugins(app.getWorldEditor()->getAllocator())
{
	m_particle_emitter_updating = true;
	m_particle_emitter_timescale = 1.0f;
	m_component_filter[0] = '\0';
}


PropertyGrid::~PropertyGrid()
{
	for (auto* i : m_plugins)
	{
		LUMIX_DELETE(m_editor.getAllocator(), i);
	}
}


void PropertyGrid::showProperty(Lumix::IPropertyDescriptor& desc,
	int index,
	const Lumix::Array<Lumix::Entity>& entities,
	Lumix::ComponentType cmp_type)
{
	if (desc.getType() == Lumix::IPropertyDescriptor::BLOB) return;

	Lumix::OutputBlob stream(m_editor.getAllocator());
	Lumix::ComponentUID first_entity_cmp;
	first_entity_cmp.type = cmp_type;
	first_entity_cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	first_entity_cmp.entity = entities[0];
	first_entity_cmp.handle = first_entity_cmp.scene->getComponent(entities[0], cmp_type);
	desc.get(first_entity_cmp, index, stream);
	Lumix::InputBlob tmp(stream);

	Lumix::StaticString<100> desc_name(desc.getName(), "###", (Lumix::uint64)&desc);

	switch (desc.getType())
	{
	case Lumix::IPropertyDescriptor::DECIMAL:
	{
		float f;
		tmp.read(f);
		auto& d = static_cast<Lumix::IDecimalPropertyDescriptor&>(desc);
		if (d.isInRadians()) f = Lumix::Math::radiansToDegrees(f);
		if ((d.getMax() - d.getMin()) / d.getStep() <= 100)
		{
			if (ImGui::SliderFloat(desc_name, &f, d.getMin(), d.getMax()))
			{
				if (d.isInRadians()) f = Lumix::Math::degreesToRadians(f);
				m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &f, sizeof(f));
			}
		}
		else
		{
			if (ImGui::DragFloat(desc_name, &f, d.getStep(), d.getMin(), d.getMax()))
			{
				if (d.isInRadians()) f = Lumix::Math::degreesToRadians(f);
				m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &f, sizeof(f));
			}
		}
		break;
	}
	case Lumix::IPropertyDescriptor::INTEGER:
	{
		int i;
		tmp.read(i);
		if (ImGui::DragInt(desc_name, &i))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &i, sizeof(i));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::BOOL:
	{
		bool b;
		tmp.read(b);
		if (ImGui::Checkbox(desc_name, &b))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &b, sizeof(b));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::COLOR:
	{
		Lumix::Vec3 v;
		tmp.read(v);
		if (ImGui::ColorEdit3(desc_name, &v.x))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
		}
		if (ImGui::BeginPopupContextItem(Lumix::StaticString<50>(desc_name, "pu")))
		{
			if (ImGui::ColorPicker(&v.x, false))
			{
				m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
			}
			ImGui::EndPopup();
		}
		break;
	}
	case Lumix::IPropertyDescriptor::VEC2:
	{
		Lumix::Vec2 v;
		tmp.read(v);
		if (desc.isInRadians())
		{
			v.x = Lumix::Math::radiansToDegrees(v.x);
			v.y = Lumix::Math::radiansToDegrees(v.y);
		}
		if (ImGui::DragFloat2(desc_name, &v.x))
		{
			if (desc.isInRadians())
			{
				v.x = Lumix::Math::degreesToRadians(v.x);
				v.y = Lumix::Math::degreesToRadians(v.y);
			}
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::INT2:
	{
		Lumix::Int2 v;
		tmp.read(v);
		if (ImGui::DragInt2(desc_name, &v.x))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::VEC3:
	{
		Lumix::Vec3 v;
		tmp.read(v);
		if (desc.isInRadians()) v = Lumix::Math::radiansToDegrees(v);
		if (ImGui::DragFloat3(desc_name, &v.x))
		{
			if (desc.isInRadians()) v = Lumix::Math::degreesToRadians(v);
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::VEC4:
	{
		Lumix::Vec4 v;
		tmp.read(v);
		if (ImGui::DragFloat4(desc_name, &v.x))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &v, sizeof(v));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::RESOURCE:
	{
		char buf[1024];
		Lumix::copyString(buf, (const char*)stream.getData());
		auto& resource_descriptor = static_cast<Lumix::IResourcePropertyDescriptor&>(desc);
		Lumix::ResourceType rm_type = resource_descriptor.getResourceType();
		if (m_app.getAssetBrowser()->resourceInput(
				desc.getName(), Lumix::StaticString<20>("", (Lumix::uint64)&desc), buf, sizeof(buf), rm_type))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), buf, Lumix::stringLength(buf) + 1);
		}
		break;
	}
	case Lumix::IPropertyDescriptor::STRING:
	case Lumix::IPropertyDescriptor::FILE:
	{
		char buf[1024];
		Lumix::copyString(buf, (const char*)stream.getData());
		if (ImGui::InputText(desc_name, buf, sizeof(buf)))
		{
			m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), buf, Lumix::stringLength(buf) + 1);
		}
		break;
	}
	case Lumix::IPropertyDescriptor::ARRAY:
		showArrayProperty(entities, cmp_type, static_cast<Lumix::IArrayDescriptor&>(desc));
		break;
	case Lumix::IPropertyDescriptor::SAMPLED_FUNCTION:
		showSampledFunctionProperty(entities, cmp_type, static_cast<Lumix::ISampledFunctionDescriptor&>(desc));
		break;
	case Lumix::IPropertyDescriptor::ENTITY:
		showEntityProperty(entities, cmp_type, index, static_cast<Lumix::IEnumPropertyDescriptor&>(desc));
		break;
	case Lumix::IPropertyDescriptor::ENUM:
		showEnumProperty(entities, cmp_type, index, static_cast<Lumix::IEnumPropertyDescriptor&>(desc));
		break;
	case Lumix::IPropertyDescriptor::BLOB:
	default:
		ASSERT(false);
		break;
	}
}


void PropertyGrid::showEntityProperty(const Lumix::Array<Lumix::Entity>& entities,
	Lumix::ComponentType cmp_type,
	int index,
	Lumix::IPropertyDescriptor& desc)
{
	Lumix::OutputBlob blob(m_editor.getAllocator());
	
	Lumix::ComponentUID cmp;
	cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	cmp.type = cmp_type;
	cmp.entity = entities[0];
	cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
	desc.get(cmp, index, blob);
	Lumix::Entity entity = *(Lumix::Entity*)blob.getData();
	int value = m_editor.getUniverse()->getDenseIdx(entity);
	int count = m_editor.getUniverse()->getEntityCount();

	struct Data
	{
		Lumix::IPropertyDescriptor* descriptor;
		Lumix::WorldEditor* editor;
	};

	auto getter = [](void* data, int index, const char** out) -> bool {
		auto* combo_data = static_cast<Data*>(data);
		static char buf[128];
		Lumix::Entity entity = combo_data->editor->getUniverse()->getEntityFromDenseIdx(index);
		getEntityListDisplayName(*combo_data->editor, buf, Lumix::lengthOf(buf), entity);
		*out = buf;

		return true;
	};

	Data data;
	data.descriptor = &desc;
	data.editor = &m_editor;

	if(ImGui::Combo(desc.getName(), &value, getter, &data, count))
	{
		Lumix::Entity entity = m_editor.getUniverse()->getEntityFromDenseIdx(value);
		m_editor.setProperty(cmp_type, index, desc, &entities[0], entities.size(), &entity, sizeof(entity));
	}
}


void PropertyGrid::showEnumProperty(const Lumix::Array<Lumix::Entity>& entities,
	Lumix::ComponentType cmp_type,
	int index,
	Lumix::IEnumPropertyDescriptor& desc)
{
	if(entities.size() > 1)
	{
		ImGui::LabelText(desc.getName(), "Multi-object editing not supported.");
		return;
	}

	Lumix::ComponentUID cmp;
	cmp.type = cmp_type;
	cmp.entity = entities[0];
	cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
	Lumix::OutputBlob blob(m_editor.getAllocator());
	desc.get(cmp, index, blob);
	int value = *(int*)blob.getData();
	int count = desc.getEnumCount(cmp.scene, cmp.handle);

	struct Data
	{
		Lumix::IEnumPropertyDescriptor* descriptor;
		Lumix::ComponentHandle cmp;
		Lumix::IScene* scene;
	};

	auto getter = [](void* data, int index, const char** out) -> bool {
		auto* combo_data = static_cast<Data*>(data);
		*out = combo_data->descriptor->getEnumItemName(combo_data->scene, combo_data->cmp, index);
		if (!*out)
		{
			static char buf[100];
			combo_data->descriptor->getEnumItemName(
				combo_data->scene, combo_data->cmp, index, buf, Lumix::lengthOf(buf));
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


void PropertyGrid::showSampledFunctionProperty(const Lumix::Array<Lumix::Entity>& entities,
	Lumix::ComponentType cmp_type,
	Lumix::ISampledFunctionDescriptor& desc)
{
	Lumix::OutputBlob blob(m_editor.getAllocator());
	Lumix::ComponentUID cmp;
	cmp.type = cmp_type;
	cmp.entity = entities[0];
	cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
	desc.get(cmp, -1, blob);
	int count;
	Lumix::InputBlob input(blob);
	input.read(count);
	Lumix::Vec2* f = (Lumix::Vec2*)input.skip(sizeof(Lumix::Vec2) * count);

	auto editor = ImGui::BeginCurveEditor(desc.getName());
	if (editor.valid)
	{
		bool changed = false;
		ImVec2 editor_size = ImVec2(ImGui::CalcItemWidth(), ImGui::GetItemRectSize().y);

		changed |= ImGui::CurveSegment((ImVec2*)(f + 1), editor);//first point

		for (int i = 1; i < count - 3; i += 3)
		{
			changed |= ImGui::CurveSegment((ImVec2*)(f + i), editor);

			if (changed)
			{
				f[i + 3].x = Lumix::Math::maximum(f[i].x + 0.001f, f[i + 3].x);

				if (i + 3 < count)
				{
					f[i + 3].x = Lumix::Math::minimum(f[i + 6].x - 0.001f, f[i + 3].x);
				}
			}

			if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0))//remove point
			{
				for (int j = i - 1; j < count - 3; ++j)
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

		if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0))//add new point
		{
			auto mp = ImGui::GetMousePos();
			mp.x -= ImGui::GetItemRectMin().x - 1;
			mp.y -= ImGui::GetItemRectMin().y - 1;
			mp.x /= editor_size.x;
			mp.y /= editor_size.y;
			mp.y = 1 - mp.y;
			blob.write(ImVec2(-0.2f, 0));
			blob.write(mp);
			blob.write(ImVec2(0.2f, 0));
			count += 3;
			*(int*)blob.getData() = count;
			f = (Lumix::Vec2*)((int*)blob.getData() + 1);
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
				auto prev_p = ((Lumix::Vec2*)f)[i - 1];
				auto next_p = ((Lumix::Vec2*)f)[i + 2];
				auto& tangent = ((Lumix::Vec2*)f)[i];
				auto& tangent2 = ((Lumix::Vec2*)f)[i + 1];
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


void PropertyGrid::showArrayProperty(const Lumix::Array<Lumix::Entity>& entities,
	Lumix::ComponentType cmp_type,
	Lumix::IArrayDescriptor& desc)
{
	Lumix::ComponentUID cmp;
	cmp.type = cmp_type;
	cmp.scene = m_editor.getUniverse()->getScene(cmp_type);
	cmp.entity = entities[0];
	cmp.handle = cmp.scene->getComponent(cmp.entity, cmp.type);
	Lumix::StaticString<100> desc_name(desc.getName(), "###", (Lumix::uint64)&desc);

	if (!ImGui::CollapsingHeader(desc_name, nullptr, ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) return;
	if (entities.size() > 1)
	{
		ImGui::Text("Multi-object editing not supported.");
		return;
	}

	int count = desc.getCount(cmp);
	if (desc.canAdd() && ImGui::Button("Add"))
	{
		m_editor.addArrayPropertyItem(cmp, desc);
	}
	count = desc.getCount(cmp);

	for (int i = 0; i < count; ++i)
	{
		char tmp[10];
		Lumix::toCString(i, tmp, sizeof(tmp));
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
}


void PropertyGrid::showComponentProperties(const Lumix::Array<Lumix::Entity>& entities, Lumix::ComponentType cmp_type)
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

	auto& descs = Lumix::PropertyRegister::getDescriptors(cmp_type);

	for (auto* desc : descs)
	{
		showProperty(*desc, -1, entities, cmp_type);
	}

	if (entities.size() == 1)
	{
		Lumix::ComponentUID cmp;
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


bool PropertyGrid::entityInput(const char* label, const char* str_id, Lumix::Entity& entity) const
{
	const auto& style = ImGui::GetStyle();
	float item_w = ImGui::CalcItemWidth();
	ImGui::PushItemWidth(
		item_w - ImGui::CalcTextSize("...").x - style.FramePadding.x * 2 - style.ItemSpacing.x);
	char buf[50];
	getEntityListDisplayName(m_editor, buf, sizeof(buf), entity);
	ImGui::LabelText("", "%s", buf);
	ImGui::SameLine();
	Lumix::StaticString<30> popup_name("pu", str_id);
	if (ImGui::Button(Lumix::StaticString<30>("...###br", str_id)))
	{
		ImGui::OpenPopup(popup_name);
	}

	ImGui::SameLine();
	ImGui::Text("%s", label);
	ImGui::PopItemWidth();

	if (ImGui::BeginPopup(popup_name))
	{
		struct ListBoxData
		{
			Lumix::WorldEditor* m_editor;
			Lumix::Universe* universe;
			char buffer[1024];
			static bool itemsGetter(void* data, int idx, const char** txt)
			{
				auto* d = static_cast<ListBoxData*>(data);
				auto entity = d->universe->getEntityFromDenseIdx(idx);
				getEntityListDisplayName(*d->m_editor, d->buffer, sizeof(d->buffer), entity);
				*txt = d->buffer;
				return true;
			}
		};
		ListBoxData data;
		Lumix::Universe* universe = m_editor.getUniverse();
		data.universe = universe;
		data.m_editor = &m_editor;
		static int current_item;
		if (ImGui::ListBox("Entities",
			&current_item,
			&ListBoxData::itemsGetter,
			&data,
			universe->getEntityCount(),
			15))
		{
			entity = universe->getEntityFromDenseIdx(current_item);
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return true;
		};

		ImGui::EndPopup();
	}
	return false;
}


void PropertyGrid::showCoreProperties(const Lumix::Array<Lumix::Entity>& entities)
{
	if (entities.size() == 1)
	{
		char name[256];
		const char* tmp = m_editor.getUniverse()->getEntityName(entities[0]);

		ImGui::LabelText("ID", "%d", entities[0].index);

		Lumix::copyString(name, tmp);
		if (ImGui::InputText("Name", name, sizeof(name))) m_editor.setEntityName(entities[0], name);
	}
	else
	{
		ImGui::LabelText("ID", "%s", "Multiple objects");
		ImGui::LabelText("Name", "%s", "Multi-object editing not supported.");
	}

	Lumix::Vec3 pos = m_editor.getUniverse()->getPosition(entities[0]);
	Lumix::Vec3 old_pos = pos;
	if (ImGui::DragFloat3("Position", &pos.x))
	{
		Lumix::WorldEditor::Coordinate coord;
		if (pos.x != old_pos.x) coord = Lumix::WorldEditor::Coordinate::X;
		if (pos.y != old_pos.y) coord = Lumix::WorldEditor::Coordinate::Y;
		if (pos.z != old_pos.z) coord = Lumix::WorldEditor::Coordinate::Z;
		m_editor.setEntitiesCoordinate(&entities[0], entities.size(), (&pos.x)[(int)coord], coord);
	}

	Lumix::Universe* universe = m_editor.getUniverse();
	Lumix::Quat rot = universe->getRotation(entities[0]);
	Lumix::Vec3 old_euler = rot.toEuler();
	Lumix::Vec3 euler = Lumix::Math::radiansToDegrees(old_euler);
	if (ImGui::DragFloat3("Rotation", &euler.x))
	{
		if (euler.x <= -90.0f || euler.x >= 90.0f) euler.y = 0;
		euler.x = Lumix::Math::degreesToRadians(Lumix::Math::clamp(euler.x, -90.0f, 90.0f));
		euler.y = Lumix::Math::degreesToRadians(fmodf(euler.y + 180, 360.0f) - 180);
		euler.z = Lumix::Math::degreesToRadians(fmodf(euler.z + 180, 360.0f) - 180);
		rot.fromEuler(euler);
		
		Lumix::Array<Lumix::Quat> rots(m_editor.getAllocator());
		for (Lumix::Entity entity : entities)
		{
			Lumix::Vec3 tmp = universe->getRotation(entity).toEuler();
			
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
		else if (Lumix::stristr(node->plugin->getLabel(), filter)) node->plugin->onGUI(false, true);
		showAddComponentNode(node->next, filter);
		return;
	}

	if (node->plugin)
	{
		node->plugin->onGUI(false, false);
		showAddComponentNode(node->next, filter);
		return;
	}

	const char* last = Lumix::reverseFind(node->label, nullptr, '/');
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
	if (ImGui::BeginDock("Properties", &m_is_opened) && !ents.empty())
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

		Lumix::Universe& universe = *m_editor.getUniverse();
		for (Lumix::ComponentUID cmp = universe.getFirstComponent(ents[0]); cmp.isValid();
			 cmp = universe.getNextComponent(cmp))
		{
			showComponentProperties(ents, cmp.type);
		}
	}
	ImGui::EndDock();
}


