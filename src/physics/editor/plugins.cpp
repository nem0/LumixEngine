#include <PxPhysicsAPI.h>
#include <imgui/imgui.h>

#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/gizmo.h"
#include "editor/property_grid.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/path.h"
#include "engine/reflection.h"
#include "engine/universe.h"
#include "physics/physics_geometry.h"
#include "physics/physics_scene.h"
#include "physics/physics_system.h"
#include "renderer/model.h"
#include "renderer/render_scene.h"


using namespace Lumix;


namespace
{


const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
const ComponentType RAGDOLL_TYPE = reflection::getComponentType("ragdoll");
const ComponentType CONTROLLER_TYPE = reflection::getComponentType("physical_controller");
const ComponentType DISTANCE_JOINT_TYPE = reflection::getComponentType("distance_joint");
const ComponentType HINGE_JOINT_TYPE = reflection::getComponentType("hinge_joint");
const ComponentType SPHERICAL_JOINT_TYPE = reflection::getComponentType("spherical_joint");
const ComponentType D6_JOINT_TYPE = reflection::getComponentType("d6_joint");
const ComponentType RIGID_ACTOR_TYPE = reflection::getComponentType("rigid_actor");
const ComponentType VEHICLE_TYPE = reflection::getComponentType("vehicle");
const ComponentType WHEEL_TYPE = reflection::getComponentType("wheel");
const u32 RENDERER_HASH = crc32("renderer");


Vec3 fromPhysx(const physx::PxVec3& v) { return Vec3(v.x, v.y, v.z); }
Quat fromPhysx(const physx::PxQuat& v) { return Quat(v.x, v.y, v.z, v.w); }
RigidTransform fromPhysx(const physx::PxTransform& v) { return{ DVec3(fromPhysx(v.p)), fromPhysx(v.q) }; }

void showD6JointGizmo(UniverseView& view, const RigidTransform& global_frame, physx::PxD6Joint* joint)
{
	physx::PxRigidActor* actors[2];
	joint->getActors(actors[0], actors[1]);

	const physx::PxTransform local_frame0 = joint->getLocalPose(physx::PxJointActorIndex::eACTOR0);
	const RigidTransform global_frame0 = global_frame * fromPhysx(local_frame0);
	const DVec3 joint_pos = global_frame0.pos;
	const Quat rot0 = global_frame0.rot;

	addLine(view, joint_pos, joint_pos + rot0 * Vec3(1, 0, 0), Color::RED);
	addLine(view, joint_pos, joint_pos + rot0 * Vec3(0, 1, 0), Color::GREEN);
	addLine(view, joint_pos, joint_pos + rot0 * Vec3(0, 0, 1), Color::BLUE);

	RigidTransform global_frame1 = global_frame0;
	if (actors[1]) {
		const physx::PxTransform local_frame1 = joint->getLocalPose(physx::PxJointActorIndex::eACTOR1);
		const RigidTransform global_frame1 = fromPhysx(actors[1]->getGlobalPose() * local_frame1);
		const Quat rot1 = global_frame1.rot;

		addLine(view, joint_pos, joint_pos + rot1 * Vec3(1, 0, 0), Color::RED);
		addLine(view, joint_pos, joint_pos + rot1 * Vec3(0, 1, 0), Color::GREEN);
		addLine(view, joint_pos, joint_pos + rot1 * Vec3(0, 0, 1), Color::BLUE);
	}
	const bool is_swing1_limited = joint->getMotion(physx::PxD6Axis::eSWING1) == physx::PxD6Motion::eLIMITED;
	const bool is_swing2_limited = joint->getMotion(physx::PxD6Axis::eSWING2) == physx::PxD6Motion::eLIMITED;
	const Quat rot1 = global_frame1.rot;
	if (is_swing1_limited && is_swing2_limited)
	{
		const float swing1 = joint->getSwingLimit().yAngle;
		const float swing2 = joint->getSwingLimit().zAngle;
		addCone(view, joint_pos,
			rot1 * Vec3(1, 0, 0),
			rot1 * Vec3(0, 1, 0) * tanf(swing1),
			rot1 * Vec3(0, 0, 1) * tanf(swing2),
			Color(0x55, 0x55, 0x55, 0xff));
	}
	else if (is_swing1_limited)
	{
		const Vec3 x_vec = rot1 * Vec3(1, 0, 0);
		const Vec3 z_vec = rot1 * Vec3(0, 0, 1);
		float swing1 = joint->getSwingLimit().yAngle;
		DVec3 prev_pos = joint_pos + z_vec * sinf(-swing1) + x_vec * cosf(-swing1);
		addLine(view, prev_pos, joint_pos, Color(0x55, 0x55, 0x55, 0xff));
		for (int i = 1; i <= 32; ++i)
		{
			float angle = -swing1 + (2*swing1) * i / 32.0f;
			float s = sinf(angle);
			float c = cosf(angle);
			DVec3 pos = joint_pos + z_vec * s + x_vec * c;
			addLine(view, pos, prev_pos, Color(0x55, 0x55, 0x55, 0xff));
			prev_pos = pos;
		}
		addLine(view, prev_pos, joint_pos, Color(0x55, 0x55, 0x55, 0xff));
	}
	else if (is_swing2_limited)
	{
		Vec3 y_vec = rot1 * Vec3(1, 0, 0);
		Vec3 x_vec = rot1 * Vec3(1, 0, 0);
		float swing2 = joint->getSwingLimit().zAngle;
		DVec3 prev_pos = joint_pos + y_vec * sinf(-swing2) + x_vec * cosf(-swing2);
		addLine(view, prev_pos, joint_pos, Color(0x55, 0x55, 0x55, 0xff));
		for (int i = 1; i <= 32; ++i)
		{
			float angle = -swing2 + (2 * swing2) * i / 32.0f;
			float s = sinf(angle);
			float c = cosf(angle);
			DVec3 pos = joint_pos + y_vec * s + x_vec * c;
			addLine(view, pos, prev_pos, Color(0x55, 0x55, 0x55, 0xff));
			prev_pos = pos;
		}
		addLine(view, prev_pos, joint_pos, Color(0x55, 0x55, 0x55, 0xff));
	}

	bool is_twist_limited = joint->getMotion(physx::PxD6Axis::eTWIST) == physx::PxD6Motion::eLIMITED;
	if (is_twist_limited)
	{
		Vec3 y_vec = rot1 * Vec3(0, 1, 0);
		Vec3 z_vec = rot1 * Vec3(0, 0, 1);
		float lower = joint->getTwistLimit().lower;
		float upper = joint->getTwistLimit().upper;
		DVec3 prev_pos = joint_pos + y_vec * sinf(lower) + z_vec * cosf(lower);
		addLine(view, prev_pos, joint_pos, Color(0x55, 0x55, 0x55, 0xff));
		for (int i = 1; i <= 32; ++i)
		{
			float angle = lower + (upper - lower) * i / 32.0f;
			float s = sinf(angle);
			float c = cosf(angle);
			DVec3 pos = joint_pos + y_vec * s + z_vec * c;
			addLine(view, pos, prev_pos, Color(0x55, 0x55, 0x55, 0xff));
			prev_pos = pos;
		}
		addLine(view, prev_pos, joint_pos, Color(0x55, 0x55, 0x55, 0xff));
	}
}

void showSphericalJointGizmo(UniverseView& view, ComponentUID cmp)
{
	auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
	Universe& universe = phy_scene->getUniverse();

	const EntityRef entity = (EntityRef)cmp.entity;
	EntityPtr other_entity = phy_scene->getJointConnectedBody(entity);
	if (!other_entity.isValid()) return;


	RigidTransform local_frame0 = phy_scene->getJointLocalFrame(entity);
	const RigidTransform global_frame0 = universe.getTransform(entity).getRigidPart() * local_frame0;
	const DVec3 joint_pos = global_frame0.pos;
	const Quat rot0 = global_frame0.rot;

	addLine(view, joint_pos, joint_pos + rot0 * Vec3(1, 0, 0), Color::RED);
	addLine(view, joint_pos, joint_pos + rot0 * Vec3(0, 1, 0), Color::GREEN);
	addLine(view, joint_pos, joint_pos + rot0 * Vec3(0, 0, 1), Color::BLUE);

	RigidTransform local_frame1 = phy_scene->getJointConnectedBodyLocalFrame(entity);
	RigidTransform global_frame1 = universe.getTransform((EntityRef)other_entity).getRigidPart() * local_frame1;
	const Quat rot1 = global_frame1.rot;

	bool use_limit = phy_scene->getSphericalJointUseLimit(entity);
	if (use_limit)
	{
		Vec2 limit = phy_scene->getSphericalJointLimit(entity);
		DVec3 other_pos = universe.getPosition((EntityRef)other_entity);
		addLine(view, joint_pos, other_pos, Color::RED);
		addCone(view, joint_pos,
			rot1 * Vec3(1, 0, 0),
			rot1 * Vec3(0, 1, 0) * tanf(limit.y),
			rot1 * Vec3(0, 0, 1) * tanf(limit.x),
			Color(0xff555555));
	}
	else
	{
		addLine(view, joint_pos, joint_pos + rot1 * Vec3(1, 0, 0), Color::RED);
		addLine(view, joint_pos, joint_pos + rot1 * Vec3(0, 1, 0), Color::GREEN);
		addLine(view, joint_pos, joint_pos + rot1 * Vec3(0, 0, 1), Color::BLUE);
	}
}

void showRigidActorGizmo(UniverseView& view, ComponentUID cmp)
{
	auto* scene = static_cast<PhysicsScene*>(cmp.scene);
	const EntityRef e = (EntityRef)cmp.entity;
	Universe& universe = scene->getUniverse();
	const DVec3 pos = universe.getPosition(e);
	const Quat rot = universe.getRotation(e);
	const i32 box_count = scene->getBoxGeometryCount(e);
	for (i32 i = 0; i < box_count; ++i) {
		const Vec3 half = scene->getBoxGeomHalfExtents(e, i);
		const Vec3 p = scene->getBoxGeomOffsetPosition(e, i);
		const Quat r = rot * scene->getBoxGeomOffsetRotationQuat(e, i);

		addCube(view
			, pos + rot.rotate(p)
			, r.rotate(Vec3(half.x, 0, 0))
			, r.rotate(Vec3(0, half.y, 0))
			, r.rotate(Vec3(0, 0, half.z))
			, Color::BLUE);
	}
	const i32 sphere_count = scene->getSphereGeometryCount(e);
	for (i32 i = 0; i < sphere_count; ++i) {
		const float r = scene->getSphereGeomRadius(e, i);
		const Vec3 p = scene->getSphereGeomOffsetPosition(e, i);
		addSphere(view, pos + rot.rotate(p), r, Color::BLUE);
	}
}

void showWheelGizmo(UniverseView& view, ComponentUID cmp) {
	Universe& universe = cmp.scene->getUniverse();
	const EntityRef e = (EntityRef)cmp.entity;
	PhysicsScene* scene = (PhysicsScene*)cmp.scene;
	const Transform wheel_tr = universe.getTransform(e);
	const float radius = scene->getWheelRadius(e);
	const float width = scene->getWheelWidth(e);

	const Vec3 wheel_axis = wheel_tr.rot.rotate(Vec3(1, 0, 0));
	addCylinder(view, wheel_tr.pos - wheel_axis * width * 0.5f, wheel_axis , radius, width, Color::BLUE);
}

void showVehicleGizmo(UniverseView& view, ComponentUID cmp) {
	const EntityRef e = (EntityRef)cmp.entity;
	PhysicsScene* scene = (PhysicsScene*)cmp.scene;
	Universe& universe = cmp.scene->getUniverse();
	const Transform vehicle_tr = universe.getTransform(e);
	for (EntityPtr ch_ptr = universe.getFirstChild(e); ch_ptr.isValid(); ch_ptr = universe.getNextSibling((EntityRef)ch_ptr)) {
		const EntityRef ch = (EntityRef)ch_ptr;
		if (!universe.hasComponent(ch, WHEEL_TYPE)) continue;
			
		ComponentUID wheel_cmp;
		wheel_cmp.entity = ch;
		wheel_cmp.scene = scene;
		wheel_cmp.type = WHEEL_TYPE;
		showWheelGizmo(view, wheel_cmp);

		const Transform wheel_tr = universe.getTransform(ch);
		addLine(view, vehicle_tr.pos, wheel_tr.pos, Color::BLUE);

		const Vec3 cm = scene->getVehicleCenterOfMass(e);
		addLine(view, vehicle_tr.pos, vehicle_tr.pos + vehicle_tr.rot.rotate(cm), Color::RED);
	}
}

void showDistanceJointGizmo(UniverseView& view, ComponentUID cmp)
{
	static const int SEGMENT_COUNT = 100;
	static const int TWIST_COUNT = 5;

	auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
	Universe& universe = phy_scene->getUniverse();

	const EntityRef entity = (EntityRef)cmp.entity;
	EntityPtr other_entity = phy_scene->getJointConnectedBody(entity);
	if (!other_entity.isValid()) return;
	RigidTransform local_frame = phy_scene->getJointConnectedBodyLocalFrame(entity);

	DVec3 pos = universe.getPosition((EntityRef)other_entity);
	DVec3 other_pos = (universe.getTransform((EntityRef)other_entity).getRigidPart() * local_frame).pos;
	Vec3 dir = (other_pos - pos).toFloat();

	dir = dir * (1.0f / SEGMENT_COUNT);
	float dir_len = dir.length();
	Vec3 right(0, -dir.z, dir.y);
	if (fabsf(right.y) < 0.001f && fabsf(right.z) < 0.001f)
	{
		right.set(dir.z, 0, -dir.x);
	}
	right.normalize();
	Vec3 up = crossProduct(dir, right).normalized();
	right *= minimum(1.0f, 5 * dir_len);
	up *= minimum(1.0f, 5 * dir_len);

	Vec3 force = phy_scene->getDistanceJointLinearForce(entity);

	float t = minimum(force.length() / 10.0f, 1.0f);
	u32 color = 0xff000000 + (u32(t * 0xff) << 16) + u32((1 - t) * 0xff);
	addLine(view, pos + right, pos, color);
	static const float ANGLE_STEP = PI * 2 * float(TWIST_COUNT) / SEGMENT_COUNT;
	float c = cosf(0);
	float s = sinf(0);
	for (int i = 0; i < SEGMENT_COUNT; ++i)
	{
		float angle = ANGLE_STEP * i;
		float c2 = cosf(angle + ANGLE_STEP);
		float s2 = sinf(angle + ANGLE_STEP);
		addLine(view, pos + c * right + s * up, pos + c2 * right + s2 * up + dir, color);
		c = c2;
		s = s2;
		pos += dir;
	}
	addLine(view, pos + right, other_pos, color);
}

void showHingeJointGizmo(UniverseView& view, 
	PhysicsScene& phy_scene,
	const Vec2& limit,
	bool use_limit,
	const RigidTransform& global_frame1)
{
	Universe& universe = phy_scene.getUniverse();
	Vec3 y_vec = global_frame1.rot * Vec3(0, 1, 0);
	Vec3 z_vec = global_frame1.rot * Vec3(0, 0, 1);

	addLine(view, global_frame1.pos, global_frame1.pos + global_frame1.rot * Vec3(1, 0, 0), 0xffff0000);
	addLine(view, global_frame1.pos, global_frame1.pos + global_frame1.rot * Vec3(0, 1, 0), 0xff00ff00);
	addLine(view, global_frame1.pos, global_frame1.pos + global_frame1.rot * Vec3(0, 0, 1), 0xff0000ff);

	if (use_limit)
	{
		addLine(view, global_frame1.pos, global_frame1.pos + y_vec * sinf(limit.x) + z_vec * cosf(limit.x), 0xff555555);
		addLine(view, global_frame1.pos, global_frame1.pos + y_vec * sinf(limit.y) + z_vec * cosf(limit.y), 0xff555555);

		
		DVec3 prev_pos = global_frame1.pos + y_vec * sinf(limit.x) + z_vec * cosf(limit.x);
		for (int i = 1; i <= 32; ++i)
		{
			float angle = limit.x + (limit.y - limit.x) * i / 32.0f;
			float s = sinf(angle);
			float c = cosf(angle);
			const DVec3 pos = global_frame1.pos + y_vec * s + z_vec * c;
			addLine(view, pos, prev_pos, 0xff555555);
			prev_pos = pos;
		}
	}
}

void showHingeJointGizmo(UniverseView& view, ComponentUID cmp)
{
	auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
	const EntityRef entity = (EntityRef)cmp.entity;
	const EntityPtr connected_body = phy_scene->getJointConnectedBody(entity);
	Vec2 limit = phy_scene->getHingeJointLimit(entity);
	bool use_limit = phy_scene->getHingeJointUseLimit(entity);
	if (!connected_body.isValid()) return;
	RigidTransform global_frame1 = phy_scene->getJointConnectedBodyLocalFrame(entity);
	global_frame1 = phy_scene->getUniverse().getTransform((EntityRef)connected_body).getRigidPart() * global_frame1;
	showHingeJointGizmo(view, *phy_scene, limit, use_limit, global_frame1);
}

struct PhysicsUIPlugin final : StudioApp::GUIPlugin
{
	explicit PhysicsUIPlugin(StudioApp& app)
		: m_editor(app.getWorldEditor())
		, m_selected_bone(-1)
		, m_is_window_open(false)
		, m_app(app)
	{
		m_toggle_ui.init("Physics", "Toggle physics UI", "physics", "", false);
		m_toggle_ui.func.bind<&PhysicsUIPlugin::onAction>(this);
		m_toggle_ui.is_selected.bind<&PhysicsUIPlugin::isOpen>(this);
		app.addWindowAction(&m_toggle_ui);
	}

	~PhysicsUIPlugin() {
		m_app.removeAction(&m_toggle_ui);
	}

	bool packData(const char* dest_dir) override
	{
		char exe_path[LUMIX_MAX_PATH];
		os::getExecutablePath(Span(exe_path));
		char exe_dir[LUMIX_MAX_PATH];

		const char* physx_dlls[] = {
			"PhysX_64.dll",
			"PhysXFoundation_64.dll",
			"PhysXCommon_64.dll",
			"PhysXCooking_64.dll",
		};
		for (const char* dll : physx_dlls)
		{
			copyString(Span(exe_dir), Path::getDir(exe_path));
			StaticString<LUMIX_MAX_PATH> tmp(exe_dir, dll);
			if (!os::fileExists(tmp)) return false;
			StaticString<LUMIX_MAX_PATH> dest(dest_dir, dll);
			if (!os::copyFile(tmp, dest))
			{
				logError("Failed to copy ", tmp, " to ", dest);
				return false;
			}
		}
		return true; 
	}



	const char* getName() const override { return "physics"; }
	bool isOpen() const { return m_is_window_open; }
	void onAction() { m_is_window_open = !m_is_window_open; }


	void onLayersGUI()
	{
		PhysicsSystem* system = static_cast<PhysicsSystem*>(m_editor.getEngine().getPluginManager().getPlugin("physics"));
		if (ImGui::CollapsingHeader("Layers"))
		{
			for (int i = 0; i < system->getCollisionsLayersCount(); ++i)
			{
				char buf[30];
				copyString(buf, system->getCollisionLayerName(i));
				char label[10];
				toCString(i, Span(label));
				if (ImGui::InputText(label, buf, sizeof(buf)))
				{
					system->setCollisionLayerName(i, buf);
				}
			}
			if (ImGui::Button("Add layer"))
			{
				system->addCollisionLayer();
			}
			if (system->getCollisionsLayersCount() > 1)
			{
				ImGui::SameLine();
				if (ImGui::Button("Remove layer"))
				{
					system->removeCollisionLayer();
				}
			}
		}
	}


	void onCollisionMatrixGUI()
	{
		PhysicsSystem* system = static_cast<PhysicsSystem*>(m_editor.getEngine().getPluginManager().getPlugin("physics"));
		if (ImGui::CollapsingHeader("Collision matrix"))
		{
			ImGui::Columns(1 + system->getCollisionsLayersCount(), "collision_matrix_col");
			ImGui::NextColumn();
			ImGui::PushTextWrapPos(1);
			float basic_offset = 0;
			for (int i = 0, c = system->getCollisionsLayersCount(); i < c; ++i)
			{
				auto* layer_name = system->getCollisionLayerName(i);
				basic_offset = maximum(basic_offset, ImGui::CalcTextSize(layer_name).x);
			}
			basic_offset += ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().WindowPadding.x;

			for (int i = 0, c = system->getCollisionsLayersCount(); i < c; ++i)
			{
				auto* layer_name = system->getCollisionLayerName(i);
				float offset = basic_offset + i * 35.0f;
				ImGui::SetColumnOffset(-1, offset);
				ImGui::Text("%s", layer_name);
				ImGui::NextColumn();
			}
			ImGui::PopTextWrapPos();

			ImGui::Separator();
			for (int i = 0, c = system->getCollisionsLayersCount(); i < c; ++i)
			{
				ImGui::Text("%s", system->getCollisionLayerName(i));
				ImGui::NextColumn();

				for (int j = 0; j <= i; ++j)
				{
					bool b = system->canLayersCollide(i, j);
					if (ImGui::Checkbox(StaticString<10>("###", i, "-") << j, &b))
					{
						system->setLayersCanCollide(i, j, b);
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


	void onJointGUI()
	{
		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(RIGID_ACTOR_TYPE));
		auto* render_scene = static_cast<RenderScene*>(m_editor.getUniverse()->getScene(MODEL_INSTANCE_TYPE));
		if (!render_scene) return;

		int count = scene->getJointCount();
		if (count > 0 && ImGui::CollapsingHeader("Joints"))
		{
			ImGui::Columns(2);
			ImGui::Text("From"); ImGui::NextColumn();
			ImGui::Text("To"); ImGui::NextColumn();
			ImGui::PushID("joints");
			ImGui::Separator();
			for (int i = 0; i < count; ++i)
			{
				ComponentUID cmp;
				const EntityRef entity = scene->getJointEntity(i);
				cmp.entity = entity;
				cmp.scene = scene;
				physx::PxJoint* joint = scene->getJoint(entity);
				switch ((physx::PxJointConcreteType::Enum)scene->getJoint(entity)->getConcreteType())
				{
					case physx::PxJointConcreteType::eDISTANCE:
						cmp.type = DISTANCE_JOINT_TYPE;
						//showDistanceJointGizmo(cmp);
						break;
					case physx::PxJointConcreteType::eREVOLUTE:
						cmp.type = HINGE_JOINT_TYPE;
						//showHingeJointGizmo(cmp);
						break;
					case physx::PxJointConcreteType::eSPHERICAL:
						cmp.type = SPHERICAL_JOINT_TYPE;
						//showSphericalJointGizmo(view, cmp); // TODO
						break;
					case physx::PxJointConcreteType::eD6:
						cmp.type = D6_JOINT_TYPE;
						/*showD6JointGizmo(m_editor.getUniverse()->getTransform(entity).getRigidPart(),
							*render_scene,
							static_cast<physx::PxD6Joint*>(joint));*/
						// TODO
						break;
					default: ASSERT(false); break;
				}

				ImGui::PushID(i);
				char tmp[256];
				getEntityListDisplayName(m_app, m_app.getWorldEditor(), Span(tmp), cmp.entity);
				bool b = false;
				if (ImGui::Selectable(tmp, &b)) m_editor.selectEntities(Span(&entity, 1), false);
				ImGui::NextColumn();

				EntityPtr other_entity = scene->getJointConnectedBody(entity);
				getEntityListDisplayName(m_app, m_app.getWorldEditor(), Span(tmp), other_entity);
				if (other_entity.isValid() && ImGui::Selectable(tmp, &b)) {
					const EntityRef e = (EntityRef)other_entity;
					m_editor.selectEntities(Span(&e, 1), false);
				}
				ImGui::NextColumn();
				ImGui::PopID();
			}
			ImGui::Columns();
			ImGui::PopID();
		}
	}


	void onVisualizationGUI()
	{
		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));
		DVec3 camera_pos = m_editor.getView().getViewport().pos;
		const Vec3 extents(20, 20, 20);
		scene->setVisualizationCullingBox(camera_pos - extents, camera_pos + extents);

		if (!ImGui::CollapsingHeader("Visualization")) return;

		u32 viz_flags = scene->getDebugVisualizationFlags();
		auto flag_gui = [&viz_flags](const char* label, int flag) {
			bool b = (viz_flags & (1 << flag)) != 0;
			if (ImGui::Checkbox(label, &b))
			{
				if (b) viz_flags |= 1 << flag;
				else  viz_flags &= ~(1 << flag);
			}
		};

		flag_gui("Body axes", physx::PxVisualizationParameter::eBODY_AXES);
		flag_gui("Mass axes", physx::PxVisualizationParameter::eBODY_MASS_AXES);
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

		if (m_editor.getSelectedEntities().empty()) {
			ImGui::Text("No entities selected.");
			return;
		}

		const EntityRef e = m_editor.getSelectedEntities()[0];

		auto* scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));

		if(!scene->getUniverse().hasComponent(e, RIGID_ACTOR_TYPE)) {
			ImGui::Text("Entity does not have rigid actor component.");
			return;
		};
		
		char tmp[255];
		getEntityListDisplayName(m_app, m_app.getWorldEditor(), Span(tmp), e);

		ImGui::Text("%s", tmp);
		ImGui::SameLine();
		bool is_debug_viz = scene->isActorDebugEnabled(e);
		if (ImGui::Checkbox("Debug visualization", &is_debug_viz)) {
			scene->enableActorDebug(e, is_debug_viz);
		}
	}


	void onDebugGUI()
	{
		if (!ImGui::CollapsingHeader("Debug")) return;

		ImGui::Indent();

		onVisualizationGUI();
		onJointGUI();
		onActorGUI();
		ImGui::Unindent();
	}


	void showBoneListItem(RenderScene& render_scene, const Transform& tr, Model& model, int bone_index, bool visualize)
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
				u32 color = m_selected_bone == i ? 0xffff0000 : 0xff0000ff;
				render_scene.addDebugLine(tr.transform(bone.transform.pos), tr.transform(child_bone.transform.pos), color);
			}
			showBoneListItem(render_scene, tr, model, i, visualize);
		}
		ImGui::Unindent();
	}


	void renderBone(RenderScene& render_scene, PhysicsScene& phy_scene, RagdollBone* bone, RagdollBone* selected_bone)
	{
		/*if (!bone) return;
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
			RigidTransform tr;
			tr.rot = Quat(pose.q.x, pose.q.y, pose.q.z, pose.q.w);
			tr.pos = DVec3(pose.p.x, pose.p.y, pose.p.z);
			if(joint->is<physx::PxRevoluteJoint>())	{
				GizmoPlugin::showHingeJointGizmo(phy_scene, Vec2(0, 0), false, tr);
			}
			if (joint->is<physx::PxD6Joint>())
			{
				GizmoPlugin::showD6JointGizmo(
					fromPhysx(a0->getGlobalPose()), render_scene, static_cast<physx::PxD6Joint*>(joint));
			}
		}*/
		// TODO
	}


	void setSubtreeDynamic(EntityRef entity) {
		WorldEditor& editor = m_app.getWorldEditor();
		Universe& universe = *editor.getUniverse();
		auto* phy_scene = static_cast<PhysicsScene*>(universe.getScene(RIGID_ACTOR_TYPE));
		auto* render_scene = static_cast<RenderScene*>(universe.getScene(MODEL_INSTANCE_TYPE));
		
		phy_scene->setDynamicType(entity, PhysicsScene::DynamicType::DYNAMIC);
		//render_scene->setBoneAttachmentInverse(entity, true);

		for (EntityPtr e = universe.getFirstChild(entity); e.isValid(); e = universe.getNextSibling((EntityRef)e)) {
			setSubtreeDynamic((EntityRef)e);
		}

	}

	void autogeneratePhySkeleton(EntityRef entity) {
		WorldEditor& editor = m_app.getWorldEditor();
		editor.beginCommandGroup(crc32("ragdoll"));
		Universe& universe = *editor.getUniverse();
		auto* phy_scene = static_cast<PhysicsScene*>(universe.getScene(RIGID_ACTOR_TYPE));
		auto* render_scene = static_cast<RenderScene*>(universe.getScene(MODEL_INSTANCE_TYPE));
		const Transform root_tr = universe.getTransform(entity);
		ASSERT(render_scene);
		Model* model = render_scene->getModelInstanceModel(entity);
		ASSERT(model && model->isReady());
		Array<EntityRef> entities(m_app.getAllocator());
		for (int i = 0; i < model->getBoneCount(); ++i) {
			const Model::Bone& bone = model->getBone(i);

			const Transform tr = root_tr * bone.transform;

			if (bone.parent_idx >= 0) {
				const Model::Bone& parent_bone = model->getBone(bone.parent_idx);
				const Vec3 parent_pos = parent_bone.transform.pos;
				const DVec3 pos = (root_tr * parent_bone.transform).pos;

				Quat rot = Quat::IDENTITY;
				if ((parent_pos - bone.transform.pos).squaredLength() > 0.01f) {
					rot = Quat::vec3ToVec3(Vec3(0, 1, 0), (parent_pos - bone.transform.pos).normalized());
				}
				const EntityRef bone_e = editor.addEntityAt(pos);
				editor.setEntitiesRotations(&bone_e, &rot, 1);
				entities.push(bone_e);
				Vec3 size((bone.transform.pos - parent_pos).length() * 0.5f);
				size.x *= 0.2f;
				size.z *= 0.2f;

				if (size.y > 0) {
					editor.addComponent(Span(&bone_e, 1), RIGID_ACTOR_TYPE);
					ComponentUID cmp;
					cmp.entity = bone_e;
					cmp.scene = phy_scene;
					cmp.type = RIGID_ACTOR_TYPE;
					editor.addArrayPropertyItem(cmp, "Box geometry");
					editor.addComponent(Span(&bone_e, 1), BONE_ATTACHMENT_TYPE);
					editor.setProperty(BONE_ATTACHMENT_TYPE, "", 0, "Parent", Span(&bone_e, 1), entity);
					editor.setProperty(BONE_ATTACHMENT_TYPE, "", 0, "Bone", Span(&bone_e, 1), bone.parent_idx);
					editor.setProperty(RIGID_ACTOR_TYPE, "Box geometry", 0, "Size", Span(&bone_e, 1), size);
					editor.setProperty(RIGID_ACTOR_TYPE, "Box geometry", 0, "Position offset", Span(&bone_e, 1), Vec3(0, -size.y, 0));
					editor.setProperty(RIGID_ACTOR_TYPE, "", 0, "Dynamic", Span(&bone_e, 1), (i32)PhysicsScene::DynamicType::KINEMATIC);

					editor.addComponent(Span(&bone_e, 1), SPHERICAL_JOINT_TYPE);
					editor.setProperty(SPHERICAL_JOINT_TYPE, "", 0, "Connected body", Span(&bone_e, 1), entities[bone.parent_idx]);
					editor.setProperty(SPHERICAL_JOINT_TYPE, "", 0, "Axis direction", Span(&bone_e, 1), Vec3(0, -1, 0));
					editor.setProperty(SPHERICAL_JOINT_TYPE, "", 0, "Use limit", Span(&bone_e, 1), true);
					editor.setProperty(SPHERICAL_JOINT_TYPE, "", 0, "Limit", Span(&bone_e, 1), Vec2(degreesToRadians(45.f)));
				}
				
				editor.makeParent(entities[bone.parent_idx], bone_e);
			}
			else {
				const EntityRef bone_e = editor.addEntityAt(tr.pos);
				editor.setEntitiesRotations(&bone_e, &tr.rot, 1);
				entities.push(bone_e);
				editor.makeParent(entity, bone_e);
			}
/*
			auto iter = model->getBoneIndex(bone_name_hash);
			ASSERT(iter.isValid());

			auto* new_bone = LUMIX_NEW(m_allocator, RagdollBone);
			new_bone->child = new_bone->next = new_bone->prev = new_bone->parent = nullptr;
			new_bone->parent_joint = nullptr;
			new_bone->is_kinematic = false;
			new_bone->pose_bone_idx = iter.value();

			float bone_height;
			RigidTransform transform = getNewBoneTransform(model, iter.value(), bone_height);

			new_bone->bind_transform = transform.inverted() * model->getBone(iter.value()).transform;
			new_bone->inv_bind_transform = new_bone->bind_transform.inverted();
			transform = m_universe.getTransform(entity).getRigidPart() * transform;

			PxCapsuleGeometry geom;
			geom.halfHeight = bone_height * 0.3f;
			if (geom.halfHeight < 0.001f) geom.halfHeight = 1.0f;
			geom.radius = geom.halfHeight * 0.5f;

			PxTransform px_transform = toPhysx(transform);
			new_bone->actor = PxCreateDynamic(m_scene->getPhysics(), px_transform, geom, *m_default_material, 1.0f);
			new_bone->actor->is<PxRigidDynamic>()->setMass(0.0001f);
			new_bone->actor->userData = (void*)(intptr_t)entity.index;
			new_bone->actor->setActorFlag(PxActorFlag::eVISUALIZATION, true);
			new_bone->actor->is<PxRigidDynamic>()->setSolverIterationCounts(8, 8);
			m_scene->addActor(*new_bone->actor);
			updateFilterData(new_bone->actor, 0);

			auto& ragdoll = m_ragdolls[entity];
			new_bone->next = ragdoll.root;
			if (new_bone->next) new_bone->next->prev = new_bone;
			setRagdollRoot(ragdoll, new_bone);
			auto* parent = getPhyParent(entity, model, iter.value());
			if (parent) connect(ragdoll, new_bone, parent);

			findCloserChildren(ragdoll, entity, model, new_bone);*/
		}
		editor.endCommandGroup();
	}


	void onRagdollGUI()
	{
		if (!ImGui::CollapsingHeader("Ragdoll")) return;

		if (m_editor.getSelectedEntities().size() != 1) {
			ImGui::Text("%s", "Please select single entity.");
			return;
		}

		EntityRef entity = m_editor.getSelectedEntities()[0];
		if (ImGui::Button("Autogenerate")) autogeneratePhySkeleton(entity);
		if (ImGui::Button("Set subtree dynamic")) setSubtreeDynamic(entity);

/*		
		auto* render_scene = static_cast<RenderScene*>(m_editor.getUniverse()->getScene(RENDERER_HASH));
		if (!render_scene) return;

		bool has_model_instance = render_scene->getUniverse().hasComponent(entity, MODEL_INSTANCE_TYPE);
		auto* phy_scene = static_cast<PhysicsScene*>(m_editor.getUniverse()->getScene(crc32("physics")));

		bool has_ragdoll = render_scene->getUniverse().hasComponent(entity, RAGDOLL_TYPE);
		if (!has_ragdoll || !has_model_instance)
		{
			ImGui::Text("%s", "Please select an entity with ragdoll and mesh components.");
			return;
		}

		const Transform tr = m_editor.getUniverse()->getTransform(entity);
		Model* model = render_scene->getModelInstanceModel(entity);
		if (!model || !model->isReady()) return;

		static bool visualize = true;
		ImGui::Checkbox("Visualize physics", &visualize);
		ImGui::SameLine();
		static bool visualize_bones = false;
		ImGui::Checkbox("Visualize bones", &visualize_bones);
		RagdollBone* selected_bone = nullptr;
		if (m_selected_bone >= 0 && m_selected_bone < model->getBoneCount())
		{
			u32 hash = crc32(model->getBone(m_selected_bone).name.c_str());
			selected_bone = phy_scene->getRagdollBoneByName(entity, hash);
		}
		if (visualize) renderBone(*render_scene, *phy_scene, phy_scene->getRagdollRootBone(entity), selected_bone);
		ImGui::SameLine();
		if (ImGui::Button("Autogenerate")) autogeneratePhySkeleton(*phy_scene, entity, model);
		ImGui::SameLine();
		auto* root = phy_scene->getRagdollRootBone(entity);
		if (ImGui::Button("All kinematic")) phy_scene->setRagdollBoneKinematicRecursive(root, true);
		PhysicsScene::BoneOrientation new_bone_orientation = phy_scene->getNewBoneOrientation();
		if (ImGui::Combo("New bone orientation", (int*)&new_bone_orientation, "X\0Y\0"))
		{
			phy_scene->setNewBoneOrientation(new_bone_orientation);
		}

		if (ImGui::BeginChild("bones", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0)))
		{
			for (int i = 0; i < model->getBoneCount(); ++i)
			{
				auto& bone = model->getBone(i);
				if (bone.parent_idx >= 0) continue;

				showBoneListItem(*render_scene, tr, *model, i, visualize_bones);
			}
		}
		ImGui::EndChild();
		ImGui::SameLine();
		if (ImGui::BeginChild("properties", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
		{
			if (m_selected_bone < 0 || m_selected_bone >= model->getBoneCount())
			{
				ImGui::Text("No bone selected");
			}
			else
			{
				auto& bone = model->getBone(m_selected_bone);
				onBonePropertiesGUI(*phy_scene, entity, crc32(bone.name.c_str()));
			}
		}
		ImGui::EndChild();*/
	}


	void onBonePropertiesGUI(PhysicsScene& scene, EntityRef entity, u32 bone_name_hash)
	{
		/*auto* bone_handle = scene.getRagdollBoneByName(entity, bone_name_hash);
		if (!bone_handle)
		{
			if (ImGui::Button("Add"))
			{
				scene.createRagdollBone(entity, bone_name_hash);
			}
			return;
		}

		if (ImGui::Button("Remove"))
		{
			scene.destroyRagdollBone(entity, bone_handle);
			return;
		}

		bool is_kinematic = scene.isRagdollBoneKinematic(bone_handle);
		if (ImGui::Checkbox("Kinematic", &is_kinematic)) scene.setRagdollBoneKinematic(bone_handle, is_kinematic);

		float height = scene.getRagdollBoneHeight(bone_handle);
		float radius = scene.getRagdollBoneRadius(bone_handle);
		if (ImGui::DragFloat("Height", &height)) scene.setRagdollBoneHeight(bone_handle, height);
		if (ImGui::DragFloat("Radius", &radius)) scene.setRagdollBoneRadius(bone_handle, radius);

		Transform transform = scene.getRagdollBoneTransform(bone_handle).toScaled(1);
		bool changed_by_gizmo = m_editor.getGizmo().immediate(transform);
		if (ImGui::DragFloat3("Position", &transform.pos.x) || changed_by_gizmo)
		{
			scene.setRagdollBoneTransform(bone_handle, transform.getRigidPart());
		}
		Vec3 euler_angles = radiansToDegrees(transform.rot.toEuler());
		if (ImGui::DragFloat3("Rotation", &euler_angles.x))
		{
			transform.rot.fromEuler(degreesToRadians(euler_angles));
			scene.setRagdollBoneTransform(bone_handle, transform.getRigidPart());
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
				if (changed) hinge->setLimit(limit);
				joint_type = 0;
				break;
			}
			case physx::PxJointType::eD6:
			{
				auto* d6 = joint->is<physx::PxD6Joint>();
				auto linear_limit = d6->getLinearLimit();
				if (ImGui::DragFloat("Linear limit", &linear_limit.value)) d6->setLinearLimit(linear_limit);

				auto swing_limit = d6->getSwingLimit();
				Vec2 tmp = {radiansToDegrees(swing_limit.yAngle), radiansToDegrees(swing_limit.zAngle)};
				if (ImGui::DragFloat2("Swing limit", &tmp.x))
				{
					swing_limit.yAngle = degreesToRadians(tmp.x);
					swing_limit.zAngle = degreesToRadians(tmp.y);
					d6->setSwingLimit(swing_limit);
				}

				auto twist_limit = d6->getTwistLimit();
				tmp = {radiansToDegrees(twist_limit.lower), radiansToDegrees(twist_limit.upper)};
				if (ImGui::DragFloat2("Twist limit", &tmp.x))
				{
					twist_limit.lower = degreesToRadians(tmp.x);
					twist_limit.upper = degreesToRadians(tmp.y);
					d6->setTwistLimit(twist_limit);
				}

				for (int i = 0; i < 6; ++i)
				{
					const char* labels[] = {"X motion", "Y motion", "Z motion", "Twist", "Swing 1", "Swing 2"};
					int motion = d6->getMotion(physx::PxD6Axis::Enum(i));
					if (ImGui::Combo(labels[i], &motion, "Locked\0Limited\0Free\0"))
					{
						d6->setMotion(physx::PxD6Axis::Enum(i), physx::PxD6Motion::Enum(motion));
					}
				}

				joint_type = 3;
				break;
			}
			default: ASSERT(false); break;
		}
		if (ImGui::Combo("Joint type", &joint_type, "Hinge\0Fixed\0Spherical\0D6\0"))
		{
			int px_type = physx::PxJointConcreteType::eFIXED;
			switch (joint_type)
			{
				case 0: px_type = physx::PxJointConcreteType::eREVOLUTE; break;
				case 1: px_type = physx::PxJointConcreteType::eFIXED; break;
				case 2: px_type = physx::PxJointConcreteType::eSPHERICAL; break;
				case 3: px_type = physx::PxJointConcreteType::eD6; break;
				default: ASSERT(false); break;
			}
			scene.changeRagdollBoneJoint(bone_handle, px_type);
			joint = scene.getRagdollBoneJoint(bone_handle);
			if (!joint) return;
		}

		auto local_pose0 = joint->getLocalPose(physx::PxJointActorIndex::eACTOR0);
		auto original_pose = local_pose0;
		if (ImGui::DragFloat3("Joint position", &local_pose0.p.x))
		{
			auto local_pose1 = joint->getLocalPose(physx::PxJointActorIndex::eACTOR1);
			local_pose1 = original_pose.getInverse() * local_pose0 * local_pose1;
			joint->setLocalPose(physx::PxJointActorIndex::eACTOR1, local_pose1);
			joint->setLocalPose(physx::PxJointActorIndex::eACTOR0, local_pose0);
		}*/
	}

	void onSettingsLoaded() override {
		m_is_window_open = m_app.getSettings().getValue("is_physics_ui_open", false);
	}

	void onBeforeSettingsSaved() override {
		m_app.getSettings().setValue("is_physics_ui_open", m_is_window_open);
	}

	void onWindowGUI() override
	{
		if (!m_is_window_open) return;
		if (ImGui::Begin("Physics", &m_is_window_open))
		{
			onLayersGUI();
			onCollisionMatrixGUI();
			onRagdollGUI();
			onDebugGUI();
		}

		ImGui::End();
	}


	StudioApp& m_app;
	WorldEditor& m_editor;
	bool m_is_window_open = false;
	i32 m_selected_bone = -1;
	Action m_toggle_ui;
};



struct PhysicsGeometryPlugin final : AssetBrowser::IPlugin
{
	explicit PhysicsGeometryPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("phy", PhysicsGeometry::TYPE);
	}


	void onGUI(Span<Resource*> resources) override {}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Physics geometry"; }
	ResourceType getResourceType() const override { return PhysicsGeometry::TYPE; }


	StudioApp& m_app;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_geom_plugin(app)
		, m_ui_plugin(app)
	{
	}

	void init() override
	{
		m_app.addPlugin(m_ui_plugin);
		m_app.getAssetBrowser().addPlugin(m_geom_plugin);
	}


	bool showGizmo(UniverseView& view, ComponentUID cmp) override
	{
		auto* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		Universe& universe = phy_scene->getUniverse();
		
		const EntityRef entity = (EntityRef)cmp.entity;
		
		if (cmp.type == RIGID_ACTOR_TYPE) {
			showRigidActorGizmo(view, cmp);
			return true;
		}
		
		if (cmp.type == VEHICLE_TYPE) {
			showVehicleGizmo(view, cmp);
			return true;
		}
		
		if (cmp.type == WHEEL_TYPE) {
			showWheelGizmo(view, cmp);
			return true;
		}

		if (cmp.type == CONTROLLER_TYPE)
		{
			float height = phy_scene->getControllerHeight(entity);
			float radius = phy_scene->getControllerRadius(entity);

			const DVec3 pos = universe.getPosition(entity);
			addCapsule(view, pos, height, radius, Color::BLUE);
			return true;
		}

		if (cmp.type == DISTANCE_JOINT_TYPE)
		{
			showDistanceJointGizmo(view, cmp);
			return true;
		}

		if (cmp.type == HINGE_JOINT_TYPE)
		{
			showHingeJointGizmo(view, cmp);
			return true;
		}

		if (cmp.type == SPHERICAL_JOINT_TYPE)
		{
			showSphericalJointGizmo(view, cmp);
			return true;
		}

		if (cmp.type == D6_JOINT_TYPE)
		{
			physx::PxD6Joint* joint = static_cast<physx::PxD6Joint*>(phy_scene->getJoint(entity));
			showD6JointGizmo(view, universe.getTransform(entity).getRigidPart(), joint);
			return true;
		}

		return false;
	}


	~StudioAppPlugin()
	{
		m_app.removePlugin(m_ui_plugin);
		m_app.getAssetBrowser().removePlugin(m_geom_plugin);
	}

	const char* getName() const override { return "physics"; }


	StudioApp& m_app;
	PhysicsUIPlugin m_ui_plugin;
	PhysicsGeometryPlugin m_geom_plugin;
};


} // anonymous


LUMIX_STUDIO_ENTRY(physics)
{
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}

