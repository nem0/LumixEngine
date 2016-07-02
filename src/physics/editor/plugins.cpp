#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "editor/asset_browser.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/math_utils.h"
#include "engine/property_register.h"
#include "engine/universe/universe.h"
#include "physics/physics_geometry_manager.h"
#include "physics/physics_scene.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"


using namespace Lumix;


namespace
{


static const ComponentType RAGDOLL_TYPE = PropertyRegister::getComponentType("ragdoll");
static const ComponentType BOX_ACTOR_TYPE = PropertyRegister::getComponentType("box_rigid_actor");
static const ComponentType SPHERE_ACTOR_TYPE = PropertyRegister::getComponentType("sphere_rigid_actor");
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


		Transform local_frame0 = phy_scene->getSphericalJointLocalFrame(cmp.handle);
		Transform global_frame0 = universe.getTransform(cmp.entity) * local_frame0;
		Vec3 joint_pos = global_frame0.pos;
		Matrix mtx0 = global_frame0.toMatrix();

		render_scene->addDebugLine(joint_pos, joint_pos + mtx0.getXVector(), 0xffff0000, 0);
		render_scene->addDebugLine(joint_pos, joint_pos + mtx0.getYVector(), 0xff00ff00, 0);
		render_scene->addDebugLine(joint_pos, joint_pos + mtx0.getZVector(), 0xff0000ff, 0);

		Transform local_frame1 = phy_scene->getSphericalJointConnectedBodyLocalFrame(cmp.handle);
		Transform global_frame1 = universe.getTransform(other_entity) * local_frame1;
		Matrix mtx1 = global_frame1.toMatrix();

		bool use_limit = phy_scene->getSphericalJointUseLimit(cmp.handle);
		if (use_limit)
		{
			Vec2 limit = phy_scene->getSphericalJointLimit(cmp.handle);
			Vec3 other_pos = universe.getPosition(other_entity);
			render_scene->addDebugLine(joint_pos, other_pos, 0xffff0000, 0);
			render_scene->addDebugCone(joint_pos,
				mtx1.getXVector(),
				mtx1.getYVector() * tanf(limit.y),
				mtx1.getZVector() * tanf(limit.x),
				0xff555555,
				0);
		}
		else
		{
			render_scene->addDebugLine(joint_pos, joint_pos + mtx1.getXVector(), 0xffff0000, 0);
			render_scene->addDebugLine(joint_pos, joint_pos + mtx1.getYVector(), 0xff00ff00, 0);
			render_scene->addDebugLine(joint_pos, joint_pos + mtx1.getZVector(), 0xff0000ff, 0);
		}
	}


	static void showDistanceJointGizmo(ComponentUID cmp)
	{
		static const int SEGMENT_COUNT = 100;
		static const int TWIST_COUNT = 5;

		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		Universe& universe = phy_scene->getUniverse();
		auto* render_scene = static_cast<RenderScene*>(universe.getScene(RENDERER_HASH));
		if (!render_scene) return;

		Entity other_entity = phy_scene->getDistanceJointConnectedBody(cmp.handle);
		if (!isValid(other_entity)) return;
		
		Vec3 pos = universe.getPosition(cmp.entity);
		Vec3 other_pos = universe.getPosition(other_entity);
		Vec3 dir = other_pos - pos;

		dir = dir * (1.0f / SEGMENT_COUNT);
		float dir_len = dir.length();
		Vec3 right(0, -dir.z, dir.y);
		if (Math::abs(right.y) < 0.001f && Math::abs(right.z) < 0.001f)
		{
			right.set(dir.z, 0, -dir.x);
		}
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
		Entity connected_body = phy_scene->getHingeJointConnectedBody(cmp.handle);
		Vec2 limit = phy_scene->getHingeJointLimit(cmp.handle);
		bool use_limit = phy_scene->getHingeJointUseLimit(cmp.handle);
		if (!isValid(connected_body)) return;
		Transform global_frame1 = phy_scene->getHingeJointConnectedBodyLocalFrame(cmp.handle);
		global_frame1 = phy_scene->getUniverse().getTransform(connected_body) * global_frame1;
		showHingeJointGizmo(*phy_scene, limit, use_limit, global_frame1.toMatrix());
	}


	static void showHingeJointGizmo(PhysicsScene& phy_scene,
		const Vec2& limit,
		bool use_limit,
		const Matrix& global_frame1)
	{
		Universe& universe = phy_scene.getUniverse();
		auto* render_scene = static_cast<RenderScene*>(universe.getScene(RENDERER_HASH));
		if (!render_scene) return;
		Vec3 global_frame1_pos = global_frame1.getTranslation();
		Vec3 y_vec = global_frame1.getYVector();
		Vec3 z_vec = global_frame1.getZVector();

		render_scene->addDebugLine(global_frame1_pos, global_frame1_pos + global_frame1.getXVector(), 0xffff0000, 0);
		render_scene->addDebugLine(global_frame1_pos, global_frame1_pos + global_frame1.getYVector(), 0xff00ff00, 0);
		render_scene->addDebugLine(global_frame1_pos, global_frame1_pos + global_frame1.getZVector(), 0xff0000ff, 0);

		if (use_limit)
		{
			render_scene->addDebugLine(
				global_frame1_pos, global_frame1_pos + y_vec * sinf(limit.x) + z_vec * cosf(limit.x), 0xff555555, 0);
			render_scene->addDebugLine(
				global_frame1_pos, global_frame1_pos + y_vec * sinf(limit.y) + z_vec * cosf(limit.y), 0xff555555, 0);

			
			Vec3 prev_pos = global_frame1_pos + y_vec * sinf(limit.x) + z_vec * cosf(limit.x);
			for (int i = 1; i <= 32; ++i)
			{
				float angle = limit.x + (limit.y - limit.x) * i / 32.0f;
				float s = sinf(angle);
				float c = cosf(angle);
				Vec3 pos = global_frame1_pos + y_vec * s + z_vec * c;
				render_scene->addDebugLine(pos, prev_pos, 0xff555555, 0);
				prev_pos = pos;
			}
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


	static void showSphereActorGizmo(ComponentUID cmp, RenderScene& render_scene)
	{
		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		Universe& universe = phy_scene->getUniverse();

		float radius = phy_scene->getSphereRadius(cmp.handle);
		Vec3 pos = universe.getPosition(cmp.entity);

		render_scene.addDebugSphere(pos, radius, 0xffff0000, 0);
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

		if (cmp.type == SPHERE_ACTOR_TYPE)
		{
			showSphereActorGizmo(cmp, *render_scene);
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
		, m_selected_bone(-1)
		, m_is_window_opened(false)
	{
		m_action = LUMIX_NEW(m_editor.getAllocator(), Action)("Physics", "physics");
		m_action->func.bind<StudioAppPlugin, &StudioAppPlugin::onAction>(this);
		m_action->is_selected.bind<StudioAppPlugin, &StudioAppPlugin::isOpened>(this);
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
				case PhysicsScene::ActorType::SPHERE:
					ImGui::Text("%s", "sphere");
					cmp.type = SPHERE_ACTOR_TYPE;
					EditorPlugin::showSphereActorGizmo(cmp, *render_scene);
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


	void showBoneListItem(RenderScene& render_scene, const Matrix& mtx, Model& model, int bone_index, bool visualize)
	{
		auto& bone = model.getBone(bone_index);
		if (ImGui::Selectable(bone.name.c_str(), m_selected_bone == bone_index)) m_selected_bone = bone_index;

		ImGui::Indent();
		for (int i = bone_index + 1; i < model.getBoneCount(); ++i)
		{
			auto& child_bone = model.getBone(i);
			if (child_bone.parent_idx != bone_index) continue;

			if (visualize)
			{
				uint32 color = m_selected_bone == i ? 0xffff0000 : 0xff0000ff;
				render_scene.addDebugLine(
					mtx.transform(bone.transform.pos), mtx.transform(child_bone.transform.pos), color, 0);
			}
			showBoneListItem(render_scene, mtx, model, i, visualize);
		}
		ImGui::Unindent();
	}


	void renderBone(RenderScene& render_scene,
		PhysicsScene& phy_scene,
		PhysicsScene::RagdollBoneHandle bone,
		PhysicsScene::RagdollBoneHandle selected_bone)
	{
		if (!bone) return;
		bool is_selected = bone == selected_bone;
		Matrix mtx = phy_scene.getRagdollBoneTransform(bone).toMatrix();
		float height = phy_scene.getRagdollBoneHeight(bone);
		float radius = phy_scene.getRagdollBoneRadius(bone);
		auto tmp = mtx.getXVector();
		mtx.setXVector(-mtx.getYVector());
		mtx.setYVector(tmp);
		mtx.translate(-(radius + height * 0.5f) * mtx.getYVector());

		render_scene.addDebugCapsule(mtx, height, radius, is_selected ? 0xffff0000 : 0xff00ff00, 0);
		renderBone(render_scene, phy_scene, phy_scene.getRagdollBoneChild(bone), selected_bone);
		renderBone(render_scene, phy_scene, phy_scene.getRagdollBoneSibling(bone), selected_bone);

		physx::PxJoint* joint = phy_scene.getRagdollBoneJoint(bone);
		if (joint && is_selected)
		{
			physx::PxRigidActor* a0, *a1;
			joint->getActors(a0, a1);
			physx::PxTransform pose = a1->getGlobalPose() * joint->getLocalPose(physx::PxJointActorIndex::eACTOR1);
			Matrix mtx = Quat(pose.q.x, pose.q.y, pose.q.z, pose.q.w).toMatrix();
			mtx.setTranslation(Vec3(pose.p.x, pose.p.y, pose.p.z));
			if(joint->is<physx::PxRevoluteJoint>())	EditorPlugin::showHingeJointGizmo(phy_scene, Vec2(0, 0), false, mtx);
		}
	}


	void autogeneratePhySkeleton(PhysicsScene& scene, ComponentHandle cmp, Model* model)
	{
		while (scene.getRagdollRootBone(cmp))
		{
			scene.destroyRagdollBone(cmp, scene.getRagdollRootBone(cmp));
		}

		for (int i = 0; i < model->getBoneCount(); ++i)
		{
			auto& bone = model->getBone(i);
			scene.createRagdollBone(cmp, crc32(bone.name.c_str()));
		}
	}


	void onRagdollGUI()
	{
		if (!ImGui::CollapsingHeader("Ragdoll")) return;

		if (m_editor.getSelectedEntities().size() != 1)
		{
			ImGui::Text("%s", "Please select an entity.");
			return;
		}
		
		auto* render_scene = static_cast<RenderScene*>(m_editor.getUniverse()->getScene(RENDERER_HASH));
		if (!render_scene) return;

		Entity entity = m_editor.getSelectedEntities()[0];
		ComponentHandle renderable = render_scene->getRenderableComponent(entity);
		auto* phy_scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));

		ComponentHandle cmp = phy_scene->getComponent(entity, RAGDOLL_TYPE);
		if (!isValid(cmp) || !isValid(renderable))
		{
			ImGui::Text("%s", "Please select an entity with ragdoll and mesh components.");
			return;
		}

		Matrix mtx = m_editor.getUniverse()->getMatrix(entity);
		Model* model = render_scene->getRenderableModel(renderable);
		if (!model || !model->isReady()) return;

		static bool visualize = true;
		ImGui::Checkbox("Visualize physics", &visualize);
		ImGui::SameLine();
		static bool visualize_bones = true;
		ImGui::Checkbox("Visualize bones", &visualize_bones);
		PhysicsScene::RagdollBoneHandle selected_bone = nullptr;
		if (m_selected_bone >= 0 && m_selected_bone < model->getBoneCount())
		{
			uint32 hash = crc32(model->getBone(m_selected_bone).name.c_str());
			selected_bone = phy_scene->getRagdollBoneByName(cmp, hash);
		}
		if (visualize) renderBone(*render_scene, *phy_scene, phy_scene->getRagdollRootBone(cmp), selected_bone);
		ImGui::SameLine();
		if (ImGui::Button("Autogenerate")) autogeneratePhySkeleton(*phy_scene, cmp, model);

		if (ImGui::BeginChild("bones", ImVec2(ImGui::GetContentRegionAvailWidth() * 0.5f, 0)))
		{
			for (int i = 0; i < model->getBoneCount(); ++i)
			{
				auto& bone = model->getBone(i);
				if (bone.parent_idx >= 0) break;

				showBoneListItem(*render_scene, mtx, *model, i, visualize_bones);
			}
		}
		ImGui::EndChild();
		ImGui::SameLine();
		if (ImGui::BeginChild("properties", ImVec2(ImGui::GetContentRegionAvailWidth(), 0)))
		{
			if (m_selected_bone < 0 || m_selected_bone >= model->getBoneCount())
			{
				ImGui::Text("No bone selected");
			}
			else
			{
				auto& bone = model->getBone(m_selected_bone);
				onBonePropertiesGUI(*phy_scene, cmp, crc32(bone.name.c_str()));
			}
		}
		ImGui::EndChild();
	}


	void onBonePropertiesGUI(PhysicsScene& scene, ComponentHandle cmp, uint32 bone_name_hash)
	{
		auto* bone_handle = scene.getRagdollBoneByName(cmp, bone_name_hash);
		if (!bone_handle)
		{
			if (ImGui::Button("Add"))
			{
				scene.createRagdollBone(cmp, bone_name_hash);
			}
			return;
		}

		if (ImGui::Button("Remove"))
		{
			scene.destroyRagdollBone(cmp, bone_handle);
			return;
		}

		float height = scene.getRagdollBoneHeight(bone_handle);
		float radius = scene.getRagdollBoneRadius(bone_handle);
		if (ImGui::DragFloat("Height", &height)) scene.setRagdollBoneHeight(bone_handle, height);
		if (ImGui::DragFloat("Radius", &radius)) scene.setRagdollBoneRadius(bone_handle, radius);

		Transform transform = scene.getRagdollBoneTransform(bone_handle);
		if (ImGui::DragFloat3("Position", &transform.pos.x))
		{
			scene.setRagdollBoneTransform(bone_handle, transform);
		}

		physx::PxJoint* joint = scene.getRagdollBoneJoint(bone_handle);
		if (!joint) return;
		int joint_type = 0;
		switch (joint->getConcreteType())
		{
			case physx::PxJointType::eSPHERICAL:
			{
				auto* spherical = joint->is<physx::PxSphericalJoint>();
				physx::PxJointLimitCone limit = spherical->getLimitCone();
				bool changed = ImGui::DragFloat("Y angle", &limit.yAngle);
				changed = ImGui::DragFloat("Z angle", &limit.zAngle) || changed;
				changed = ImGui::DragFloat("Stiffness", &limit.stiffness) || changed;
				changed = ImGui::DragFloat("Restitution", &limit.restitution) || changed;
				changed = ImGui::DragFloat("Damping", &limit.damping) || changed;
				changed = ImGui::DragFloat("Bounce threshold", &limit.bounceThreshold) || changed;
				changed = ImGui::DragFloat("Contact distance", &limit.contactDistance) || changed;
				if (changed) spherical->setLimitCone(limit);
				joint_type = 2;
				break;
			}

			case physx::PxJointType::eFIXED: joint_type = 1; break;
			case physx::PxJointType::eREVOLUTE: 
			{
				auto* hinge = joint->is<physx::PxRevoluteJoint>();
				physx::PxJointAngularLimitPair limit = hinge->getLimit();
				bool changed = ImGui::DragFloat("Lower limit", &limit.lower);
				changed = ImGui::DragFloat("Upper limit", &limit.upper) || changed;
				changed = ImGui::DragFloat("Stiffness", &limit.stiffness) || changed;
				changed = ImGui::DragFloat("Damping", &limit.damping) || changed;
				changed = ImGui::DragFloat("Bounce threshold", &limit.bounceThreshold) || changed;
				changed = ImGui::DragFloat("Contact distance", &limit.contactDistance) || changed;
				changed = ImGui::DragFloat("Restitution", &limit.restitution) || changed;
				if(changed) hinge->setLimit(limit);
				joint_type = 0;
				break;
			}
			default: ASSERT(false); break;
		}
		if (ImGui::Combo("Joint type", &joint_type, "Hinge\0Fixed\0Spherical\0"))
		{
			int px_type = physx::PxJointConcreteType::eFIXED;
			switch (joint_type)
			{
				case 0: px_type = physx::PxJointConcreteType::eREVOLUTE; break;
				case 1: px_type = physx::PxJointConcreteType::eFIXED; break;
				case 2: px_type = physx::PxJointConcreteType::eSPHERICAL; break;
				default: ASSERT(false); break;
			}
			scene.changeRagdollBoneJoint(bone_handle, px_type);
		}

		auto local_pose0 = joint->getLocalPose(physx::PxJointActorIndex::eACTOR0);
		auto original_pose = local_pose0;
		if (ImGui::DragFloat3("Joint position", &local_pose0.p.x))
		{
			auto local_pose1 = joint->getLocalPose(physx::PxJointActorIndex::eACTOR1);
			local_pose1 = original_pose.getInverse() * local_pose0 * local_pose1;
			joint->setLocalPose(physx::PxJointActorIndex::eACTOR1, local_pose1);
			joint->setLocalPose(physx::PxJointActorIndex::eACTOR0, local_pose0);
		}
	}


	void onWindowGUI() override
	{
		if (ImGui::BeginDock("Physics", &m_is_window_opened))
		{
			onLayersGUI();
			onCollisionMatrixGUI();
			onRagdollGUI();
			onDebugGUI();
		}

		ImGui::EndDock();
	}


	bool m_is_window_opened;
	int m_selected_bone;
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
	app.registerComponent("sphere_rigid_actor", "Physics Sphere");
	app.registerComponent("capsule_rigid_actor", "Physics Capsule");
	app.registerComponent("physical_controller", "Physics Controller");
	app.registerComponent("mesh_rigid_actor", "Physics Mesh");
	app.registerComponent("physical_heightfield", "Physics Heightfield");
	app.registerComponent("ragdoll", "Ragdoll");

	auto& editor = *app.getWorldEditor();
	auto& allocator = editor.getAllocator();

	app.addPlugin(*LUMIX_NEW(allocator, StudioAppPlugin)(editor));
	editor.addPlugin(*LUMIX_NEW(allocator, EditorPlugin)(editor));
	app.getAssetBrowser()->addPlugin(*LUMIX_NEW(allocator, PhysicsGeometryPlugin)(app));
}

