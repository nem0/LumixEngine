#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "engine/crc32.h"
#include "engine/math_utils.h"
#include "engine/property_register.h"
#include "editor/asset_browser.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/universe/universe.h"
#include "physics/physics_geometry_manager.h"
#include "physics/physics_scene.h"
#include "renderer/render_scene.h"


using namespace Lumix;


namespace
{


static const ComponentType BOX_ACTOR_TYPE = PropertyRegister::getComponentType("box_rigid_actor");
static const ComponentType CAPSULE_ACTOR_TYPE = PropertyRegister::getComponentType("capsule_rigid_actor");
static const ComponentType CONTROLLER_TYPE = PropertyRegister::getComponentType("physical_controller");
static const ComponentType DISTANCE_JOINT_TYPE = PropertyRegister::getComponentType("distance_joint");
static const ComponentType HINGE_JOINT_TYPE = PropertyRegister::getComponentType("hinge_joint");
static const ComponentType SPHERICAL_JOINT_TYPE = PropertyRegister::getComponentType("spherical_joint");
static const uint32 RENDERER_HASH = crc32("renderer");
static const uint32 PHYSICS_HASH = crc32("PHYSICS");


struct EditorPlugin : public WorldEditor::Plugin
{
	explicit EditorPlugin(WorldEditor& editor)
		: m_editor(editor)
	{
	}


	static void showSphericalJointGizmo(ComponentUID cmp)
	{
		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		Universe& universe = phy_scene->getUniverse();
		auto* render_scene = static_cast<RenderScene*>(universe.getScene(RENDERER_HASH));
		if (!render_scene) return;

		Entity other_entity = phy_scene->getSphericalJointConnectedBody(cmp.handle);
		if (!isValid(other_entity)) return;

		Matrix mtx = universe.getMatrix(cmp.entity);
		Vec3 axis_local_pos = phy_scene->getSphericalJointAxisPosition(cmp.handle);
		Vec3 axis_local_dir = phy_scene->getSphericalJointAxisDirection(cmp.handle).normalized();

		Vec3 axis_dir = (mtx * Vec4(axis_local_dir, 0)).xyz();
		Vec3 axis_pos = mtx.multiplyPosition(axis_local_pos);

		Vec3 pos = mtx.getTranslation();
		Vec3 other_pos = universe.getPosition(other_entity);

		Matrix local_mtx;
		Quat::vec3ToVec3(Vec3(1, 0, 0), axis_local_dir).toMatrix(local_mtx);
		mtx = mtx * local_mtx;
		render_scene->addDebugLine(axis_pos, axis_pos + mtx.getXVector(), 0xffff0000, 0);
		render_scene->addDebugLine(axis_pos, axis_pos + mtx.getYVector(), 0xff00ff00, 0);
		render_scene->addDebugLine(axis_pos, axis_pos + mtx.getZVector(), 0xff0000ff, 0);

		Matrix local_frame1 = phy_scene->getSphericalJointConnectedBodyLocalFrame(cmp.handle);
		Matrix global_frame1 = universe.getMatrix(other_entity) * local_frame1;
		Vec3 frame1_pos = global_frame1.getTranslation();

		Vec2 limit = phy_scene->getSphericalJointLimit(cmp.handle);
		bool use_limit = phy_scene->getSphericalJointUseLimit(cmp.handle);
		if (use_limit)
		{
			render_scene->addDebugLine(frame1_pos, other_pos, 0xffff0000, 0);
			render_scene->addDebugCone(frame1_pos,
				global_frame1.getXVector(),
				global_frame1.getYVector() * tanf(limit.y),
				global_frame1.getZVector() * tanf(limit.x),
				0xff555555,
				0);
		}
		else
		{
			render_scene->addDebugLine(frame1_pos, frame1_pos + global_frame1.getXVector(), 0xffff0000, 0);
			render_scene->addDebugLine(frame1_pos, frame1_pos + global_frame1.getYVector(), 0xff00ff00, 0);
			render_scene->addDebugLine(frame1_pos, frame1_pos + global_frame1.getZVector(), 0xff0000ff, 0);
		}
	}


	static void showDistanceJointGizmo(ComponentUID cmp)
	{
		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		Universe& universe = phy_scene->getUniverse();
		auto* render_scene = static_cast<RenderScene*>(universe.getScene(RENDERER_HASH));
		if (!render_scene) return;

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


	static void showHingeJointGizmo(ComponentUID cmp)
	{
		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		Universe& universe = phy_scene->getUniverse();
		auto* render_scene = static_cast<RenderScene*>(universe.getScene(RENDERER_HASH));
		if (!render_scene) return;

		Entity other_entity = phy_scene->getHingeJointConnectedBody(cmp.handle);
		if (!isValid(other_entity)) return;

		Matrix mtx = universe.getMatrix(cmp.entity);
		Vec3 axis_local_pos = phy_scene->getHingeJointAxisPosition(cmp.handle);
		Vec3 axis_local_dir = phy_scene->getHingeJointAxisDirection(cmp.handle).normalized();

		Vec3 axis_dir = (mtx * Vec4(axis_local_dir, 0)).xyz();
		Vec3 axis_pos = mtx.multiplyPosition(axis_local_pos);
		
		Vec3 pos = mtx.getTranslation();
		Vec3 other_pos = universe.getPosition(other_entity);

		render_scene->addDebugLine(axis_pos - axis_dir, axis_pos + axis_dir, 0xff0000ff, 0);
		render_scene->addDebugLine(pos, axis_pos, 0xff0000ff, 0);
		render_scene->addDebugLine(other_pos, axis_pos, 0xff0000ff, 0);

		Vec2 limit = phy_scene->getHingeJointLimit(cmp.handle);
		bool use_limit = phy_scene->getHingeJointUseLimit(cmp.handle);
		if (use_limit)
		{
			Vec3 init_pos = phy_scene->getHingeJointConnectedBodyInitialPosition(cmp.handle);

			Vec3 null_dir = (init_pos - axis_pos).normalized();
			Vec3 normal = crossProduct(axis_dir, null_dir).normalized();
			Vec3 x = cosf(limit.x) * null_dir + sinf(limit.x) * normal;
			render_scene->addDebugLine(axis_pos, axis_pos + 2 * x, 0xffff0000, 0);
			Vec3 y = cosf(limit.y) * null_dir + sinf(limit.y) * normal;
			render_scene->addDebugLine(axis_pos, axis_pos + 2 * y, 0xffff0000, 0);
		}
	}


	static void showBoxActorGizmo(ComponentUID cmp, RenderScene& render_scene)
	{
		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		Universe& universe = phy_scene->getUniverse();

		Vec3 extents = phy_scene->getHalfExtents(cmp.handle);
		Matrix mtx = universe.getPositionAndRotation(cmp.entity);

		render_scene.addDebugCube(mtx.getTranslation(),
			mtx.getXVector() * extents.x,
			mtx.getYVector() * extents.y,
			mtx.getZVector() * extents.z,
			0xffff0000,
			0);
	}


	static void showCapsuleActorGizmo(ComponentUID cmp, RenderScene& render_scene)
	{
		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		Universe& universe = phy_scene->getUniverse();

		float radius = phy_scene->getCapsuleRadius(cmp.handle);
		float height = phy_scene->getCapsuleHeight(cmp.handle);
		Matrix mtx = universe.getPositionAndRotation(cmp.entity);
		Vec3 physx_capsule_up = mtx.getXVector();
		mtx.setXVector(mtx.getYVector());
		mtx.setYVector(physx_capsule_up);
		Vec3 physx_capsule_center = mtx.getTranslation() - (height * 0.5f + radius) * physx_capsule_up;
		mtx.setTranslation(physx_capsule_center);
		render_scene.addDebugCapsule(mtx, height, radius, 0xffff0000, 0);
	}


	bool showGizmo(ComponentUID cmp) override
	{
		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		Universe& universe = phy_scene->getUniverse();
		auto* render_scene = static_cast<RenderScene*>(universe.getScene(RENDERER_HASH));
		if (!render_scene) return false;
		
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

		if (cmp.type == HINGE_JOINT_TYPE)
		{
			showHingeJointGizmo(cmp);
			return true;
		}

		if (cmp.type == SPHERICAL_JOINT_TYPE)
		{
			showSphericalJointGizmo(cmp);
			return true;
		}

		if (cmp.type == BOX_ACTOR_TYPE)
		{
			showBoxActorGizmo(cmp, *render_scene);
			return true;
		}

		if (cmp.type == CAPSULE_ACTOR_TYPE)
		{
			showCapsuleActorGizmo(cmp, *render_scene);
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


	void onLayersGUI()
	{
		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));
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
	}


	void onCollisionMatrixGUI()
	{
		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));
		if (ImGui::CollapsingHeader("Collision matrix"))
		{
			ImGui::Columns(1 + scene->getCollisionsLayersCount(), "collision_matrix_col");
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


	void onDistanceJointGUI()
	{
		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));
		int count = scene->getDistanceJointCount();
		if (count > 0 && ImGui::CollapsingHeader("Distance joints"))
		{
			ImGui::Columns(2);
			ImGui::Text("From"); ImGui::NextColumn();
			ImGui::Text("To"); ImGui::NextColumn();
			ImGui::PushID("distance_joints");
			ImGui::Separator();
			for (int i = 0; i < count; ++i)
			{
				ComponentUID cmp;
				cmp.handle = scene->getDistanceJointComponent(i);
				cmp.type = DISTANCE_JOINT_TYPE;
				cmp.scene = scene;
				cmp.entity = scene->getDistanceJointEntity(cmp.handle);
				EditorPlugin::showDistanceJointGizmo(cmp);

				ImGui::PushID(i);
				char tmp[256];
				getEntityListDisplayName(m_editor, tmp, Lumix::lengthOf(tmp), cmp.entity);
				bool b = false;
				if (ImGui::Selectable(tmp, &b)) m_editor.selectEntities(&cmp.entity, 1);
				ImGui::NextColumn();

				Entity other_entity = scene->getDistanceJointConnectedBody(cmp.handle);
				getEntityListDisplayName(m_editor, tmp, Lumix::lengthOf(tmp), other_entity);
				if (isValid(other_entity) && ImGui::Selectable(tmp, &b)) m_editor.selectEntities(&other_entity, 1);
				ImGui::NextColumn();
				ImGui::PopID();
			}
			ImGui::Columns();
			ImGui::PopID();
		}
	}

	void onSphericalJointGUI()
	{
		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));
		int count = scene->getSphericalJointCount();
		if (count > 0 && ImGui::CollapsingHeader("Spherical joints"))
		{
			ImGui::Columns(2);
			ImGui::Text("From"); ImGui::NextColumn();
			ImGui::Text("To"); ImGui::NextColumn();
			ImGui::PushID("spherical_joints");
			ImGui::Separator();
			for (int i = 0; i < count; ++i)
			{
				ComponentUID cmp;
				cmp.handle = scene->getSphericalJointComponent(i);
				cmp.type = SPHERICAL_JOINT_TYPE;
				cmp.scene = scene;
				cmp.entity = scene->getSphericalJointEntity(cmp.handle);
				EditorPlugin::showSphericalJointGizmo(cmp);

				ImGui::PushID(i);
				char tmp[256];
				getEntityListDisplayName(m_editor, tmp, Lumix::lengthOf(tmp), cmp.entity);
				bool b = false;
				if (ImGui::Selectable(tmp, &b)) m_editor.selectEntities(&cmp.entity, 1);
				ImGui::NextColumn();

				Entity other_entity = scene->getSphericalJointConnectedBody(cmp.handle);
				getEntityListDisplayName(m_editor, tmp, Lumix::lengthOf(tmp), other_entity);
				if (isValid(other_entity) && ImGui::Selectable(tmp, &b)) m_editor.selectEntities(&other_entity, 1);
				ImGui::NextColumn();
				ImGui::PopID();
			}
			ImGui::Columns();
			ImGui::PopID();
		}
	}


	void onHingeJointGUI()
	{
		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));
		int count = scene->getHingeJointCount();
		if (count > 0 && ImGui::CollapsingHeader("Hinge joints"))
		{
			ImGui::Columns(2);
			ImGui::Text("From"); ImGui::NextColumn();
			ImGui::Text("To"); ImGui::NextColumn();
			ImGui::Separator();
			for (int i = 0; i < count; ++i)
			{
				ComponentUID cmp;
				cmp.handle = scene->getHingeJointComponent(i);
				cmp.type = HINGE_JOINT_TYPE;
				cmp.scene = scene;
				cmp.entity = scene->getHingeJointEntity(cmp.handle);
				EditorPlugin::showHingeJointGizmo(cmp);

				ImGui::PushID(i);
				char tmp[256];
				getEntityListDisplayName(m_editor, tmp, Lumix::lengthOf(tmp), cmp.entity);
				bool b = false;
				if (ImGui::Selectable(tmp, &b)) m_editor.selectEntities(&cmp.entity, 1);
				ImGui::NextColumn();

				Entity other_entity = scene->getHingeJointConnectedBody(cmp.handle);
				getEntityListDisplayName(m_editor, tmp, Lumix::lengthOf(tmp), other_entity);
				if (ImGui::Selectable(tmp, &b)) m_editor.selectEntities(&other_entity, 1);
				ImGui::NextColumn();
				ImGui::PopID();

			}
			ImGui::Columns();
		}
	}


	void onVisualizationGUI()
	{
		if (!ImGui::CollapsingHeader("Visualization")) return;
		
		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));

		uint32 viz_flags = scene->getDebugVisualizationFlags();
		auto flag_gui = [&viz_flags](const char* label, int flag) {
			bool b = (viz_flags & (1 << flag)) != 0;
			if (ImGui::Checkbox(label, &b))
			{
				if (b) viz_flags |= 1 << flag;
				else  viz_flags &= ~(1 << flag);
			}
		};

		flag_gui("Body axes", physx::PxVisualizationParameter::eBODY_AXES);
		flag_gui("Body linear velocity", physx::PxVisualizationParameter::eBODY_LIN_VELOCITY);
		flag_gui("Body angular velocity", physx::PxVisualizationParameter::eBODY_ANG_VELOCITY);
		flag_gui("Contact normal", physx::PxVisualizationParameter::eCONTACT_NORMAL);
		flag_gui("Contact error", physx::PxVisualizationParameter::eCONTACT_ERROR);
		flag_gui("Contact force", physx::PxVisualizationParameter::eCONTACT_FORCE);
		flag_gui("Collision axes", physx::PxVisualizationParameter::eCOLLISION_AXES);
		flag_gui("Joint local frames", physx::PxVisualizationParameter::eJOINT_LOCAL_FRAMES);
		flag_gui("Joint limits", physx::PxVisualizationParameter::eJOINT_LIMITS);
		flag_gui("Collision shapes", physx::PxVisualizationParameter::eCOLLISION_SHAPES);
		flag_gui("Actor axes", physx::PxVisualizationParameter::eACTOR_AXES);
		flag_gui("Collision AABBs", physx::PxVisualizationParameter::eCOLLISION_AABBS);
		flag_gui("World axes", physx::PxVisualizationParameter::eWORLD_AXES);
		flag_gui("Contact points", physx::PxVisualizationParameter::eCONTACT_POINT);
		scene->setDebugVisualizationFlags(viz_flags);
	}


	void onActorGUI()
	{
		if (!ImGui::CollapsingHeader("Actors")) return;

		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));
		int count = scene->getActorCount();
		if (!count) return;
		auto* render_scene = static_cast<RenderScene*>(m_editor.getUniverse()->getScene(RENDERER_HASH));

		ImGui::Columns(3);
		ImGui::Text("Entity"); ImGui::NextColumn();
		ImGui::Text("Type"); ImGui::NextColumn();
		ImGui::Text("Debug visualization"); ImGui::NextColumn();
		ImGui::Separator();
		for (int i = 0; i < count; ++i)
		{
			ComponentUID cmp;
			cmp.entity = scene->getActorEntity(i);
			if (!isValid(cmp.entity)) continue;
			ImGui::PushID(i);
			char tmp[255];
			getEntityListDisplayName(m_editor, tmp, lengthOf(tmp), cmp.entity);
			bool selected = false;
			if (ImGui::Selectable(tmp, &selected)) m_editor.selectEntities(&cmp.entity, 1);
			ImGui::NextColumn();
			auto type = scene->getActorType(i);
			cmp.handle = scene->getActorComponentHandle(i);
			cmp.scene = scene;
			switch (type)
			{
				case PhysicsScene::ActorType::BOX:
					ImGui::Text("%s", "box");
					cmp.type = BOX_ACTOR_TYPE;
					EditorPlugin::showBoxActorGizmo(cmp, *render_scene);
					break;
				case PhysicsScene::ActorType::MESH: ImGui::Text("%s", "mesh"); break;
				case PhysicsScene::ActorType::CAPSULE: 
					ImGui::Text("%s", "capsule"); 
					cmp.type = BOX_ACTOR_TYPE;
					EditorPlugin::showCapsuleActorGizmo(cmp, *render_scene);
					break;
				default: ImGui::Text("%s", "unknown"); break;
			}
			ImGui::NextColumn();
			bool is_debug_viz = scene->isActorDebugEnabled(i);
			if (ImGui::Checkbox("", &is_debug_viz))
			{
				scene->enableActorDebug(i, is_debug_viz);
			}
			ImGui::NextColumn();
			ImGui::PopID();
		}
		ImGui::Columns();
	}


	void onDebugGUI()
	{
		if (!ImGui::CollapsingHeader("Debug")) return;

		ImGui::Indent();

		onVisualizationGUI();
		onHingeJointGUI();
		onDistanceJointGUI();
		onSphericalJointGUI();
		onActorGUI();
		ImGui::Unindent();
	}


	void onWindowGUI() override
	{
		if (ImGui::BeginDock("Physics", &m_is_window_opened))
		{
			onLayersGUI();
			onCollisionMatrixGUI();
			onDebugGUI();
		}

		ImGui::EndDock();
	}


	bool m_is_window_opened;
	Lumix::WorldEditor& m_editor;
};



struct PhysicsGeometryPlugin : public AssetBrowser::IPlugin
{
	explicit PhysicsGeometryPlugin(StudioApp& app)
		: m_app(app)
	{
	}


	bool acceptExtension(const char* ext, Lumix::uint32 type) const override { return false; }


	bool onGUI(Resource* resource, uint32 type) override
	{
		if (type != PHYSICS_HASH) return false;

		auto* geom = static_cast<PhysicsGeometry*>(resource);
		if (geom->isFailure())
		{
			ImGui::Text("Failed to load.");
			return true;
		}
		return true;
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Physics geometry"; }
	bool hasResourceManager(uint32 type) const override { return type == PHYSICS_HASH; }


	uint32 getResourceType(const char* ext) override
	{
		if (equalStrings(ext, "phy")) return PHYSICS_HASH;
		return 0;
	}

	StudioApp& m_app;
};


} // anonymous


LUMIX_STUDIO_ENTRY(physics)
{
	app.registerComponent("distance_joint", "Distance Joint");
	app.registerComponent("hinge_joint", "Hinge Joint");
	app.registerComponent("spherical_joint", "Spherical Joint");
	app.registerComponent("box_rigid_actor", "Physics Box");
	app.registerComponent("capsule_rigid_actor", "Physics Capsule");
	app.registerComponent("physical_controller", "Physics Controller");
	app.registerComponent("mesh_rigid_actor", "Physics Mesh");
	app.registerComponent("physical_heightfield", "Physics Heightfield");

	auto& editor = *app.getWorldEditor();
	auto& allocator = editor.getAllocator();

	app.addPlugin(*LUMIX_NEW(allocator, StudioAppPlugin)(editor));
	editor.addPlugin(*LUMIX_NEW(allocator, EditorPlugin)(editor));
	app.getAssetBrowser()->addPlugin(*LUMIX_NEW(allocator, PhysicsGeometryPlugin)(app));
}

