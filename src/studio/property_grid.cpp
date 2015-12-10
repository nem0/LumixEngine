#include "property_grid.h"
#include "asset_browser.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/vec.h"
#include "editor/iproperty_descriptor.h"
#include "editor/property_register.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "lua_script/lua_script_manager.h"
#include "lua_script/lua_script_system.h"
#include "ocornut-imgui/imgui.h"
#include "terrain_editor.h"
#include "utils.h"
#include <cmath>


const char* PropertyGrid::getComponentTypeName(Lumix::ComponentUID cmp) const
{
	for (int i = 0; i < Lumix::PropertyRegister::getComponentTypesCount(); ++i)
	{
		if (cmp.type == Lumix::crc32(Lumix::PropertyRegister::getComponentTypeID(i)))
		{
			return Lumix::PropertyRegister::getComponentTypeName(i);
		}
	}
	return "Unknown";
}


PropertyGrid::PropertyGrid(Lumix::WorldEditor& editor,
	AssetBrowser& asset_browser,
	Lumix::Array<Action*>& actions)
	: m_is_opened(true)
	, m_editor(editor)
	, m_asset_browser(asset_browser)
{
	m_filter[0] = '\0';
	m_terrain_editor = LUMIX_NEW(editor.getAllocator(), TerrainEditor)(editor, actions);
}


PropertyGrid::~PropertyGrid()
{
	LUMIX_DELETE(m_editor.getAllocator(), m_terrain_editor);
}


void PropertyGrid::showProperty(Lumix::IPropertyDescriptor& desc, int index, Lumix::ComponentUID cmp)
{
	Lumix::OutputBlob stream(m_editor.getAllocator());
	desc.get(cmp, index, stream);
	Lumix::InputBlob tmp(stream);

	StringBuilder<100> desc_name(desc.getName(), "###", (Lumix::uint64)&desc);

	switch (desc.getType())
	{
	case Lumix::IPropertyDescriptor::DECIMAL:
	{
		float f;
		tmp.read(f);
		auto& d = static_cast<Lumix::IDecimalPropertyDescriptor&>(desc);
		if ((d.getMax() - d.getMin()) / d.getStep() <= 100)
		{
			if (ImGui::SliderFloat(desc_name, &f, d.getMin(), d.getMax()))
			{
				m_editor.setProperty(cmp.type, index, desc, &f, sizeof(f));
			}
		}
		else
		{
			if (ImGui::DragFloat(desc_name, &f, d.getStep(), d.getMin(), d.getMax()))
			{
				m_editor.setProperty(cmp.type, index, desc, &f, sizeof(f));
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
			m_editor.setProperty(cmp.type, index, desc, &i, sizeof(i));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::BOOL:
	{
		bool b;
		tmp.read(b);
		if (ImGui::Checkbox(desc_name, &b))
		{
			m_editor.setProperty(cmp.type, index, desc, &b, sizeof(b));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::COLOR:
	{
		Lumix::Vec3 v;
		tmp.read(v);
		if (ImGui::ColorEdit3(desc_name, &v.x))
		{
			m_editor.setProperty(cmp.type, index, desc, &v, sizeof(v));
		}
		if (ImGui::BeginPopupContextItem(StringBuilder<50>(desc_name, "pu")))
		{
			if (ColorPicker(StringBuilder<50>(desc_name, "cp"), &v.x))
			{
				m_editor.setProperty(cmp.type, index, desc, &v, sizeof(v));
			}
			ImGui::EndPopup();
		}
		break;
	}
	case Lumix::IPropertyDescriptor::VEC2:
	{
		Lumix::Vec2 v;
		tmp.read(v);
		if (ImGui::DragFloat2(desc_name, &v.x))
		{
			m_editor.setProperty(cmp.type, index, desc, &v, sizeof(v));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::VEC3:
	{
		Lumix::Vec3 v;
		tmp.read(v);
		if (ImGui::DragFloat3(desc_name, &v.x))
		{
			m_editor.setProperty(cmp.type, index, desc, &v, sizeof(v));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::VEC4:
	{
		Lumix::Vec4 v;
		tmp.read(v);
		if (ImGui::DragFloat4(desc_name, &v.x))
		{
			m_editor.setProperty(cmp.type, index, desc, &v, sizeof(v));
		}
		break;
	}
	case Lumix::IPropertyDescriptor::RESOURCE:
	{
		char buf[1024];
		Lumix::copyString(buf, (const char*)stream.getData());
		auto& resource_descriptor = dynamic_cast<Lumix::ResourcePropertyDescriptorBase&>(desc);
		auto rm_type = resource_descriptor.getResourceType();
		auto asset_type = m_asset_browser.getTypeFromResourceManagerType(rm_type);
		if (m_asset_browser.resourceInput(desc.getName(),
				StringBuilder<20>("", (Lumix::uint64)&desc),
				buf,
				sizeof(buf),
				asset_type))
		{
			m_editor.setProperty(cmp.type, index, desc, buf, Lumix::stringLength(buf) + 1);
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
			m_editor.setProperty(cmp.type, index, desc, buf, Lumix::stringLength(buf) + 1);
		}
		break;
	}
	case Lumix::IPropertyDescriptor::ARRAY:
		showArrayProperty(cmp, static_cast<Lumix::IArrayDescriptor&>(desc));
		break;
	case Lumix::IPropertyDescriptor::SAMPLED_FUNCTION:
		showSampledFunctionProperty(cmp, static_cast<Lumix::ISampledFunctionDescriptor&>(desc));
		break;
	case Lumix::IPropertyDescriptor::ENUM:
		showEnumProperty(cmp, static_cast<Lumix::IEnumPropertyDescriptor&>(desc));
		break;
	default:
		ASSERT(false);
		break;
	}
}


void PropertyGrid::showEnumProperty(Lumix::ComponentUID cmp, Lumix::IEnumPropertyDescriptor& desc)
{
	Lumix::OutputBlob blob(m_editor.getAllocator());
	desc.get(cmp, -1, blob);
	int value = *(int*)blob.getData();
	int count = desc.getEnumCount(cmp.scene);

	struct Data
	{
		Lumix::IEnumPropertyDescriptor* descriptor;
		Lumix::IScene* scene;
	};

	auto getter = [](void* data, int index, const char** out) -> bool {
		auto* combo_data = static_cast<Data*>(data);
		*out = combo_data->descriptor->getEnumItemName(combo_data->scene, index);
		if (!*out)
		{
			static char buf[100];
			combo_data->descriptor->getEnumItemName(combo_data->scene, index, buf, Lumix::lengthOf(buf));
			*out = buf;
		}

		return true;
	};

	Data data;
	data.scene = cmp.scene;
	data.descriptor = &desc;

	if(ImGui::Combo(desc.getName(), &value, getter, &data, count))
	{
		m_editor.setProperty(cmp.type, -1, desc, &value, sizeof(value));
	}
}


void PropertyGrid::showSampledFunctionProperty(Lumix::ComponentUID cmp, Lumix::ISampledFunctionDescriptor& desc)
{
	Lumix::OutputBlob blob(m_editor.getAllocator());
	desc.get(cmp, -1, blob);
	int count;
	Lumix::InputBlob input(blob);
	input.read(count);
	Lumix::Vec2* f = (Lumix::Vec2*)input.skip(sizeof(Lumix::Vec2) * count);

	bool changed = false;
	auto cp = ImGui::GetCursorScreenPos();
	
	ImVec2 editor_size;
	auto editor = ImGui::BeginCurveEditor(desc.getName());
	if (editor.valid)
	{
		editor_size = ImVec2(ImGui::CalcItemWidth(), ImGui::GetItemRectSize().y);

		for (int i = 1; i < count; i += 3)
		{
			if (ImGui::CurvePoint((ImVec2*)(f + i - 1), editor))
			{
				changed = true;
				if (i > 1)
				{
					f[i].x = Lumix::Math::maxValue(f[i - 3].x + 0.001f, f[i].x);
				}
				if (i + 3 < count)
				{
					f[i].x = Lumix::Math::minValue(f[i + 3].x - 0.001f, f[i].x);
				}
			}
			if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0))
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
	}
	ImGui::EndCurveEditor(editor);
	if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(0))
	{
		auto mp = ImGui::GetMousePos();
		mp.x -= cp.x;
		mp.y -= cp.y;
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
			auto& tangent = ((Lumix::Vec2*)f)[i];
			auto& tangent2 = ((Lumix::Vec2*)f)[i + 1];

			tangent = tangent.normalized() * 0.1f;
			tangent2 = tangent2.normalized() * 0.1f;
		}
	}

	if (changed)
	{
		f[0].x = 0;
		f[count - 1].x = desc.getMaxX();
		m_editor.setProperty(cmp.type, -1, desc, blob.getData(), blob.getSize());
	}
}


void PropertyGrid::showArrayProperty(Lumix::ComponentUID cmp, Lumix::IArrayDescriptor& desc)
{
	StringBuilder<100> desc_name(desc.getName(), "###", (Lumix::uint64)&desc);

	if (!ImGui::CollapsingHeader(desc_name, nullptr, true, true)) return;

	int count = desc.getCount(cmp);
	if (ImGui::Button("Add"))
	{
		m_editor.addArrayPropertyItem(cmp, desc);
	}
	count = desc.getCount(cmp);

	for (int i = 0; i < count; ++i)
	{
		char tmp[10];
		Lumix::toCString(i, tmp, sizeof(tmp));
		if (ImGui::TreeNode(tmp))
		{
			ImGui::SameLine();
			if (ImGui::Button("Remove"))
			{
				m_editor.removeArrayPropertyItem(cmp, i, desc);
				--i;
				count = desc.getCount(cmp);
				ImGui::TreePop();
				continue;
			}

			for (int j = 0; j < desc.getChildren().size(); ++j)
			{
				auto* child = desc.getChildren()[j];
				showProperty(*child, i, cmp);
			}
			ImGui::TreePop();
		}
	}
}


void PropertyGrid::showComponentProperties(Lumix::ComponentUID cmp)
{
	if (!ImGui::CollapsingHeader(
		getComponentTypeName(cmp), nullptr, true, true))
		return;

	if (!m_editor.canRemove(cmp))
	{
		ImGui::Text("Remove dependents first.");
	}
	else if (ImGui::Button(
		StringBuilder<30>("Remove component##", cmp.type)))
	{
		m_editor.destroyComponent(cmp);
		return;
	}

	auto& descs = Lumix::PropertyRegister::getDescriptors(cmp.type);

	for (auto* desc : descs)
	{
		showProperty(*desc, -1, cmp);
	}

	if (cmp.type == Lumix::crc32("lua_script"))
	{
		onLuaScriptGUI(cmp);
	}

	if (cmp.type == Lumix::crc32("terrain"))
	{
		m_terrain_editor->setComponent(cmp);
		m_terrain_editor->onGUI();
	}
}


bool PropertyGrid::entityInput(const char* label, const char* str_id, Lumix::Entity& entity) const
{
	const auto& style = ImGui::GetStyle();
	float item_w = ImGui::CalcItemWidth();
	ImGui::PushItemWidth(
		item_w - ImGui::CalcTextSize("...").x - style.FramePadding.x * 2 - style.ItemSpacing.x);
	char buf[50];
	getEntityListDisplayName(m_editor, buf, sizeof(buf), entity);
	ImGui::LabelText("", buf);
	ImGui::SameLine();
	StringBuilder<30> popup_name("pu", str_id);
	if (ImGui::Button(StringBuilder<30>("...###br", str_id)))
	{
		ImGui::OpenPopup(popup_name);
	}

	ImGui::SameLine();
	ImGui::Text(label);
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


void PropertyGrid::onLuaScriptGUI(Lumix::ComponentUID cmp)
{
	auto* scene = static_cast<Lumix::LuaScriptScene*>(cmp.scene);

	for (int i = 0; i < scene->getPropertyCount(cmp.index); ++i)
	{
		char buf[256];
		Lumix::copyString(buf, scene->getPropertyValue(cmp.index, i));
		const char* property_name = scene->getPropertyName(cmp.index, i);
		auto* script_res = scene->getScriptResource(cmp.index);
		switch (script_res->getProperties()[i].type)
		{
			case Lumix::LuaScript::Property::FLOAT:
			{
				float f = (float)atof(buf);
				if (ImGui::DragFloat(property_name, &f))
				{
					Lumix::toCString(f, buf, sizeof(buf), 5);
					scene->setPropertyValue(cmp.index, property_name, buf);
				}
			}
			break;
			case Lumix::LuaScript::Property::ENTITY:
			{
				Lumix::Entity e;
				Lumix::fromCString(buf, sizeof(buf), &e);
				if (entityInput(property_name, StringBuilder<50>(property_name, cmp.index), e))
				{
					Lumix::toCString(e, buf, sizeof(buf));
					scene->setPropertyValue(cmp.index, property_name, buf);
				}
			}
			break;
			case Lumix::LuaScript::Property::ANY:
				if (ImGui::InputText(property_name, buf, sizeof(buf)))
				{
					scene->setPropertyValue(cmp.index, property_name, buf);
				}
				break;
		}
	}
}


void PropertyGrid::showCoreProperties(Lumix::Entity entity)
{
	char name[256];
	const char* tmp = m_editor.getUniverse()->getEntityName(entity);
	Lumix::copyString(name, tmp);
	if (ImGui::InputText("Name", name, sizeof(name)))
	{
		m_editor.setEntityName(entity, name);
	}

	auto pos = m_editor.getUniverse()->getPosition(entity);
	if (ImGui::DragFloat3("Position", &pos.x))
	{
		m_editor.setEntitiesPositions(&entity, &pos, 1);
	}

	auto rot = m_editor.getUniverse()->getRotation(entity);
	auto euler = rot.toEuler();
	euler.x = Lumix::Math::radiansToDegrees(fmodf(euler.x, Lumix::Math::PI));
	euler.y = Lumix::Math::radiansToDegrees(fmodf(euler.y, Lumix::Math::PI));
	euler.z = Lumix::Math::radiansToDegrees(fmodf(euler.z, Lumix::Math::PI));
	if (ImGui::DragFloat3("Rotation", &euler.x))
	{
		euler.x = Lumix::Math::degreesToRadians(fmodf(euler.x, 180));
		euler.y = Lumix::Math::degreesToRadians(fmodf(euler.y, 180));
		euler.z = Lumix::Math::degreesToRadians(fmodf(euler.z, 180));
		rot.fromEuler(euler);
		m_editor.setEntitiesRotations(&entity, &rot, 1);
	}

	float scale = m_editor.getUniverse()->getScale(entity);
	if (ImGui::DragFloat("Scale", &scale, 0.1f))
	{
		m_editor.setEntitiesScales(&entity, &scale, 1);
	}
}


void PropertyGrid::onGUI()
{
	if (!m_is_opened) return;

	auto& ents = m_editor.getSelectedEntities();
	if (ImGui::Begin("Properties", &m_is_opened) && ents.size() == 1)
	{
		if (ImGui::Button("Add component"))
		{
			ImGui::OpenPopup("AddComponentPopup");
		}
		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			for (int i = 0; i < Lumix::PropertyRegister::getComponentTypesCount(); ++i)
			{
				if (ImGui::Selectable(Lumix::PropertyRegister::getComponentTypeName(i)))
				{
					m_editor.addComponent(
						Lumix::crc32(Lumix::PropertyRegister::getComponentTypeID(i)));
				}
			}
			ImGui::EndPopup();
		}

		showCoreProperties(ents[0]);

		auto& cmps = m_editor.getComponents(ents[0]);
		for (auto cmp : cmps)
		{
			showComponentProperties(cmp);
		}

	}
	ImGui::End();
}


