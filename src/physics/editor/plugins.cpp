#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "engine/crc32.h"
#include "engine/math_utils.h"
#include "engine/property_register.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/universe/universe.h"
#include "physics/physics_scene.h"
#include "renderer/render_scene.h"


using namespace Lumix;


namespace
{


static const ComponentType BOX_ACTOR_TYPE = PropertyRegister::getComponentType("box_rigid_actor");
static const ComponentType CONTROLLER_TYPE = PropertyRegister::getComponentType("physical_controller");
static const ComponentType DISTANCE_JOINT_TYPE = PropertyRegister::getComponentType("distance_joint");


struct EditorPlugin : public WorldEditor::Plugin
{
	explicit EditorPlugin(WorldEditor& editor)
		: m_editor(editor)
	{
	}


	void showDistanceJointGizmo(ComponentUID cmp)
	{
		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		auto* render_scene = static_cast<RenderScene*>(m_editor.getUniverse()->getScene(crc32("renderer")));
		if (!render_scene) return;
		Universe& universe = render_scene->getUniverse();

		Entity other_entity = phy_scene->getDistanceJointConnectedBody(cmp.handle);
		if (!isValid(other_entity)) return;
		Vec3 pos = universe.getPosition(cmp.entity);
		Vec3 other_pos = universe.getPosition(other_entity);

		Vec3 dir = other_pos - pos;
		static const int SEGMENT_COUNT = 100;
		static const int TWIST_COUNT = 5;

		dir = dir * (1.0f / SEGMENT_COUNT);
		float dir_len = dir.length();
		Vec3 right = crossProduct(Vec3(1, 0, 0), dir);
		right.normalize();
		Vec3 up = crossProduct(dir, right).normalized();
		right *= Math::minimum(1.0f, 5 * dir_len);
		up *= Math::minimum(1.0f, 5 * dir_len);

		Vec3 force = phy_scene->getDistanceJointLinearForce(cmp.handle);

		float t = Math::minimum(force.length() / 10.0f, 1.0f);
		uint32 color = 0xff000000 + (uint32(t * 0xff) << 16) + uint32((1 - t) * 0xff);
		render_scene->addDebugLine(pos + right, pos, color, 0);
		static const float ANGLE_STEP = Math::PI * 2 * float(TWIST_COUNT) / SEGMENT_COUNT;
		float c = cosf(0);
		float s = sinf(0);
		for (int i = 0; i < SEGMENT_COUNT; ++i)
		{
			float angle = ANGLE_STEP * i;
			float c2 = cosf(angle + ANGLE_STEP);
			float s2 = sinf(angle + ANGLE_STEP);
			render_scene->addDebugLine(pos + c * right + s * up, pos + c2 * right + s2 * up + dir, color, 0);
			c = c2;
			s = s2;
			pos += dir;
		}
		render_scene->addDebugLine(pos + right, other_pos, color, 0);
	}

	bool showGizmo(ComponentUID cmp) override
	{
		auto* render_scene = static_cast<RenderScene*>(m_editor.getUniverse()->getScene(crc32("renderer")));
		if (!render_scene) return false;

		Universe& universe = render_scene->getUniverse();
		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		
		if (cmp.type == CONTROLLER_TYPE)
		{
			float height = phy_scene->getControllerHeight(cmp.handle);
			float radius = phy_scene->getControllerRadius(cmp.handle);

			Vec3 pos = universe.getPosition(cmp.entity);
			render_scene->addDebugCapsule(pos, height, radius, 0xff0000ff, 0);
			return true;
		}

		if (cmp.type == DISTANCE_JOINT_TYPE)
		{
			showDistanceJointGizmo(cmp);
			return true;
		}

		if (cmp.type == BOX_ACTOR_TYPE)
		{
			Vec3 extents = phy_scene->getHalfExtents(cmp.handle);
			Matrix mtx = universe.getPositionAndRotation(cmp.entity);

			render_scene->addDebugCube(mtx.getTranslation(),
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
		m_action->is_selected.bind<StudioAppPlugin, &StudioAppPlugin::isOpened>(this);
		m_is_window_opened = false;
	}


	bool isOpened() const { return m_is_window_opened; }


	void onAction() { m_is_window_opened = !m_is_window_opened; }


	void onWindowGUI() override
	{
		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));
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

			if (ImGui::CollapsingHeader(
					"Collision matrix", nullptr, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
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
	app.registerComponent("distance_joint", "Distance Joint");
	app.registerComponent("box_rigid_actor", "Physics Box");
	app.registerComponent("physical_controller", "Physics Controller");
	app.registerComponent("mesh_rigid_actor", "Physics Mesh");
	app.registerComponent("physical_heightfield", "Physics Heightfield");

	auto& editor = *app.getWorldEditor();
	auto& allocator = editor.getAllocator();

	app.addPlugin(*LUMIX_NEW(allocator, StudioAppPlugin)(editor));
	editor.addPlugin(*LUMIX_NEW(allocator, EditorPlugin)(editor));
}

