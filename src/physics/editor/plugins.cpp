#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "engine/crc32.h"
#include "engine/math_utils.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/universe/universe.h"
#include "physics/physics_scene.h"
#include "renderer/render_scene.h"


using namespace Lumix;


namespace
{


static const uint32 BOX_ACTOR_HASH = crc32("box_rigid_actor");
static const uint32 CONTROLLER_HASH = crc32("physical_controller");


struct EditorPlugin : public WorldEditor::Plugin
{
	explicit EditorPlugin(WorldEditor& editor)
		: m_editor(editor)
	{
	}

	bool showGizmo(ComponentUID cmp) override
	{
		PhysicsScene* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		if (cmp.type == CONTROLLER_HASH)
		{
			auto* scene = static_cast<RenderScene*>(m_editor.getScene(crc32("renderer")));
			float height = phy_scene->getControllerHeight(cmp.index);
			float radius = phy_scene->getControllerRadius(cmp.index);

			Universe& universe = scene->getUniverse();
			Vec3 pos = universe.getPosition(cmp.entity);
			scene->addDebugCapsule(pos, height, radius, 0xff0000ff, 0);
			return true;
		}

		if (cmp.type == BOX_ACTOR_HASH)
		{
			auto* scene = static_cast<RenderScene*>(m_editor.getScene(crc32("renderer")));
			Vec3 extents = phy_scene->getHalfExtents(cmp.index);

			Universe& universe = scene->getUniverse();
			Matrix mtx = universe.getPositionAndRotation(cmp.entity);

			scene->addDebugCube(mtx.getTranslation(),
				mtx.getXVector() * extents.x,
				mtx.getYVector() * extents.y,
				mtx.getZVector() * extents.z,
				0xffff0000,
				0);
			return true;
		}

		return false;
	}

	WorldEditor& m_editor;
};


struct StudioAppPlugin : public StudioApp::IPlugin
{
	explicit StudioAppPlugin(Lumix::WorldEditor& editor)
		: m_editor(editor)
	{
		m_action = LUMIX_NEW(m_editor.getAllocator(), Action)("Physics", "physics");
		m_action->func.bind<StudioAppPlugin, &StudioAppPlugin::onAction>(this);
		m_is_window_opened = false;
	}


	void onAction() { m_is_window_opened = !m_is_window_opened; }


	void onWindowGUI() override
	{
		auto* scene = static_cast<PhysicsScene*>(m_editor.getScene(crc32("physics")));
		if (ImGui::BeginDock("Physics", &m_is_window_opened))
		{
			if (ImGui::CollapsingHeader("Layers"))
			{
				for (int i = 0; i < scene->getCollisionsLayersCount(); ++i)
				{
					char buf[30];
					copyString(buf, scene->getCollisionLayerName(i));
					char label[10];
					toCString(i, label, lengthOf(label));
					if (ImGui::InputText(label, buf, lengthOf(buf)))
					{
						scene->setCollisionLayerName(i, buf);
					}
				}
				if (ImGui::Button("Add layer"))
				{
					scene->addCollisionLayer();
				}
				if (scene->getCollisionsLayersCount() > 1)
				{
					ImGui::SameLine();
					if (ImGui::Button("Remove layer"))
					{
						scene->removeCollisionLayer();
					}
				}
			}

			if (ImGui::CollapsingHeader("Collision matrix", nullptr, true, true))
			{
				ImGui::Columns(1 + scene->getCollisionsLayersCount());
				ImGui::NextColumn();
				ImGui::PushTextWrapPos(1);
				float basic_offset = 0;
				for (int i = 0, c = scene->getCollisionsLayersCount(); i < c; ++i)
				{
					auto* layer_name = scene->getCollisionLayerName(i);
					basic_offset = Math::maximum(basic_offset, ImGui::CalcTextSize(layer_name).x);
				}
				basic_offset += ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().WindowPadding.x;

				for (int i = 0, c = scene->getCollisionsLayersCount(); i < c; ++i)
				{
					auto* layer_name = scene->getCollisionLayerName(i);
					float offset = basic_offset + i * 35.0f;
					ImGui::SetColumnOffset(-1, offset);
					ImGui::Text("%s", layer_name);
					ImGui::NextColumn();
				}
				ImGui::PopTextWrapPos();

				ImGui::Separator();
				for (int i = 0, c = scene->getCollisionsLayersCount(); i < c; ++i)
				{
					ImGui::Text("%s", scene->getCollisionLayerName(i));
					ImGui::NextColumn();

					for (int j = 0; j <= i; ++j)
					{
						bool b = scene->canLayersCollide(i, j);
						if (ImGui::Checkbox(StaticString<10>("###", i, "-") << j, &b))
						{
							scene->setLayersCanCollide(i, j, b);
						}
						ImGui::NextColumn();
					}
					for (int j = i + 1; j < c; ++j)
					{
						ImGui::NextColumn();
					}
				}
				ImGui::Columns();
			}
		}

		ImGui::EndDock();
	}


	bool m_is_window_opened;
	Lumix::WorldEditor& m_editor;
};


} // anonymous


LUMIX_STUDIO_ENTRY(physics)
{
	StudioAppPlugin* plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), StudioAppPlugin)(*app.getWorldEditor());
	app.addPlugin(*plugin);

	auto& editor = *app.getWorldEditor();
	auto* editor_plugin = LUMIX_NEW(editor.getAllocator(), EditorPlugin)(editor);
	editor.addPlugin(*editor_plugin);
}

