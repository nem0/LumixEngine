#include "property_grid.h"
#include "asset_browser.h"
#include "core/crc32.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/property_descriptor.h"
#include "lua_script/lua_script_system.h"
#include "ocornut-imgui/imgui.h"
#include "terrain_editor.h"
#include "utils.h"


const char* PropertyGrid::getComponentTypeName(Lumix::ComponentUID cmp) const
{
	auto& engine = m_editor.getEngine();
	for (int i = 0; i < engine.getComponentTypesCount(); ++i)
	{
		if (cmp.type ==
			Lumix::crc32(engine.getComponentTypeID(i)))
		{
			return engine.getComponentTypeName(i);
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
	m_editor.getAllocator().deleteObject(m_terrain_editor);
}


void PropertyGrid::showProperty(Lumix::IPropertyDescriptor& desc, int index, Lumix::ComponentUID cmp)
{
	Lumix::OutputBlob stream(m_editor.getAllocator());
	if (index < 0)
		desc.get(cmp, stream);
	else
		desc.get(cmp, index, stream);
	Lumix::InputBlob tmp(stream);

	StringBuilder<100> desc_name(desc.getName(), "###", (uint64_t)&desc);

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
				StringBuilder<20>("", (uint64_t)&desc),
				buf,
				sizeof(buf),
				asset_type))
		{
			m_editor.setProperty(cmp.type, index, desc, buf, (int)strlen(buf) + 1);
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
			m_editor.setProperty(cmp.type, index, desc, buf, (int)strlen(buf) + 1);
		}
		break;
	}
	case Lumix::IPropertyDescriptor::ARRAY:
		showArrayProperty(cmp, static_cast<Lumix::IArrayDescriptor&>(desc));
		break;
	default:
		ASSERT(false);
		break;
	}
}


void PropertyGrid::showArrayProperty(Lumix::ComponentUID cmp, Lumix::IArrayDescriptor& desc)
{
	StringBuilder<100> desc_name(desc.getName(), "###", (uint64_t)&desc);

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
	if (ImGui::Button(
		StringBuilder<30>("Remove component##", cmp.type)))
	{
		m_editor.destroyComponent(cmp);
		return;
	}

	auto& descs = m_editor.getEngine().getPropertyDescriptors(cmp.type);

	for (auto* desc : descs)
	{
		showProperty(*desc, -1, cmp);
	}

	if (cmp.type == Lumix::crc32("lua_script"))
	{
		onLuaScriptGui(cmp);
	}

	if (cmp.type == Lumix::crc32("terrain"))
	{
		m_terrain_editor->setComponent(cmp);
		m_terrain_editor->onGUI();
	}
}


void PropertyGrid::onLuaScriptGui(Lumix::ComponentUID cmp)
{
	auto* scene = static_cast<Lumix::LuaScriptScene*>(cmp.scene);

	for (int i = 0; i < scene->getPropertyCount(cmp.index); ++i)
	{
		char buf[256];
		Lumix::copyString(buf, scene->getPropertyValue(cmp.index, i));
		const char* property_name = scene->getPropertyName(cmp.index, i);
		if (ImGui::InputText(property_name, buf, sizeof(buf)))
		{
			scene->setPropertyValue(cmp.index, property_name, buf);
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
	if (ImGui::DragFloat4("Rotation", &rot.x))
	{
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
			for (int i = 0;
				i < m_editor.getEngine().getComponentTypesCount();
				++i)
			{
				if (ImGui::Selectable(
					m_editor.getEngine().getComponentTypeName(i)))
				{
					m_editor.addComponent(Lumix::crc32(
						m_editor.getEngine().getComponentTypeID(i)));
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


