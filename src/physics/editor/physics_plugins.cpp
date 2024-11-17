#include <extensions/PxD6Joint.h>
#include <foundation/PxQuat.h>
#include <foundation/PxTransform.h>
#include <foundation/PxVec3.h>
#include <imgui/imgui.h>
#include <PxVisualizationParameter.h>
#include <PxMaterial.h>

#include "core/geometry.h"
#include "core/log.h"
#include "core/math.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/tokenizer.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/gizmo.h"
#include "editor/property_grid.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/component_uid.h"
#include "engine/engine.h"
#include "engine/world.h"
#include "physics/physics_module.h"
#include "physics/physics_resources.h"
#include "physics/physics_system.h"
#include "renderer/model.h"
#include "renderer/render_module.h"


using namespace Lumix;


namespace
{


const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
const ComponentType BONE_ATTACHMENT_TYPE = reflection::getComponentType("bone_attachment");
const ComponentType CONTROLLER_TYPE = reflection::getComponentType("physical_controller");
const ComponentType DISTANCE_JOINT_TYPE = reflection::getComponentType("distance_joint");
const ComponentType HINGE_JOINT_TYPE = reflection::getComponentType("hinge_joint");
const ComponentType SPHERICAL_JOINT_TYPE = reflection::getComponentType("spherical_joint");
const ComponentType D6_JOINT_TYPE = reflection::getComponentType("d6_joint");
const ComponentType RIGID_ACTOR_TYPE = reflection::getComponentType("rigid_actor");
const ComponentType VEHICLE_TYPE = reflection::getComponentType("vehicle");
const ComponentType WHEEL_TYPE = reflection::getComponentType("wheel");


Vec3 fromPhysx(const physx::PxVec3& v) { return Vec3(v.x, v.y, v.z); }
Quat fromPhysx(const physx::PxQuat& v) { return Quat(v.x, v.y, v.z, v.w); }
RigidTransform fromPhysx(const physx::PxTransform& v) { return{ DVec3(fromPhysx(v.p)), fromPhysx(v.q) }; }

void showD6JointGizmo(WorldView& view, const RigidTransform& global_frame, physx::PxD6Joint* joint)
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
		const RigidTransform f = fromPhysx(actors[1]->getGlobalPose() * local_frame1);
		const Quat rot1 = f.rot;

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

void showSphericalJointGizmo(WorldView& view, ComponentUID cmp)
{
	auto* phy_module = static_cast<PhysicsModule*>(cmp.module);
	World& world = phy_module->getWorld();

	const EntityRef entity = (EntityRef)cmp.entity;
	EntityPtr other_entity = phy_module->getJointConnectedBody(entity);
	if (!other_entity.isValid()) return;


	RigidTransform local_frame0 = phy_module->getJointLocalFrame(entity);
	const RigidTransform global_frame0 = world.getTransform(entity).getRigidPart() * local_frame0;
	const DVec3 joint_pos = global_frame0.pos;
	const Quat rot0 = global_frame0.rot;

	addLine(view, joint_pos, joint_pos + rot0 * Vec3(1, 0, 0), Color::RED);
	addLine(view, joint_pos, joint_pos + rot0 * Vec3(0, 1, 0), Color::GREEN);
	addLine(view, joint_pos, joint_pos + rot0 * Vec3(0, 0, 1), Color::BLUE);

	RigidTransform local_frame1 = phy_module->getJointConnectedBodyLocalFrame(entity);
	RigidTransform global_frame1 = world.getTransform((EntityRef)other_entity).getRigidPart() * local_frame1;
	const Quat rot1 = global_frame1.rot;

	bool use_limit = phy_module->getSphericalJointUseLimit(entity);
	if (use_limit)
	{
		Vec2 limit = phy_module->getSphericalJointLimit(entity);
		DVec3 other_pos = world.getPosition((EntityRef)other_entity);
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

void showRigidActorGizmo(WorldView& view, ComponentUID cmp)
{
	auto* module = static_cast<PhysicsModule*>(cmp.module);
	const EntityRef e = (EntityRef)cmp.entity;
	World& world = module->getWorld();
	const DVec3 pos = world.getPosition(e);
	const Quat rot = world.getRotation(e);
	const i32 box_count = module->getBoxGeometryCount(e);
	for (i32 i = 0; i < box_count; ++i) {
		const Vec3 half = module->getBoxGeomHalfExtents(e, i);
		const Vec3 p = module->getBoxGeomOffsetPosition(e, i);
		const Quat r = rot * module->getBoxGeomOffsetRotationQuat(e, i);

		addCube(view
			, pos + rot.rotate(p)
			, r.rotate(Vec3(half.x, 0, 0))
			, r.rotate(Vec3(0, half.y, 0))
			, r.rotate(Vec3(0, 0, half.z))
			, Color::BLUE);
	}
	const i32 sphere_count = module->getSphereGeometryCount(e);
	for (i32 i = 0; i < sphere_count; ++i) {
		const float r = module->getSphereGeomRadius(e, i);
		const Vec3 p = module->getSphereGeomOffsetPosition(e, i);
		addSphere(view, pos + rot.rotate(p), r, Color::BLUE);
	}
}

void showWheelGizmo(WorldView& view, ComponentUID cmp) {
	World& world = cmp.module->getWorld();
	const EntityRef e = (EntityRef)cmp.entity;
	PhysicsModule* module = (PhysicsModule*)cmp.module;
	const Transform wheel_tr = world.getTransform(e);
	const float radius = module->getWheelRadius(e);
	const float width = module->getWheelWidth(e);

	const Vec3 wheel_axis = wheel_tr.rot.rotate(Vec3(1, 0, 0));
	addCylinder(view, wheel_tr.pos - wheel_axis * width * 0.5f, wheel_axis , radius, width, Color::BLUE);
}

void showVehicleGizmo(WorldView& view, ComponentUID cmp) {
	const EntityRef e = (EntityRef)cmp.entity;
	PhysicsModule* module = (PhysicsModule*)cmp.module;
	World& world = cmp.module->getWorld();
	const Transform vehicle_tr = world.getTransform(e);
	for (EntityRef ch : world.childrenOf(e)) {
		if (!world.hasComponent(ch, WHEEL_TYPE)) continue;
			
		ComponentUID wheel_cmp;
		wheel_cmp.entity = ch;
		wheel_cmp.module = module;
		wheel_cmp.type = WHEEL_TYPE;
		showWheelGizmo(view, wheel_cmp);

		const Transform wheel_tr = world.getTransform(ch);
		addLine(view, vehicle_tr.pos, wheel_tr.pos, Color::BLUE);

		const Vec3 cm = module->getVehicleCenterOfMass(e);
		addLine(view, vehicle_tr.pos, vehicle_tr.pos + vehicle_tr.rot.rotate(cm), Color::RED);
	}
}

void showDistanceJointGizmo(WorldView& view, ComponentUID cmp)
{
	static const int SEGMENT_COUNT = 100;
	static const int TWIST_COUNT = 5;

	auto* phy_module = static_cast<PhysicsModule*>(cmp.module);
	World& world = phy_module->getWorld();

	const EntityRef entity = (EntityRef)cmp.entity;
	EntityPtr other_entity = phy_module->getJointConnectedBody(entity);
	if (!other_entity.isValid()) return;
	RigidTransform local_frame = phy_module->getJointConnectedBodyLocalFrame(entity);

	DVec3 pos = world.getPosition((EntityRef)other_entity);
	DVec3 other_pos = (world.getTransform((EntityRef)other_entity).getRigidPart() * local_frame).pos;
	Vec3 dir = Vec3(other_pos - pos);

	dir = dir * (1.0f / SEGMENT_COUNT);
	float dir_len = length(dir);
	Vec3 right(0, -dir.z, dir.y);
	if (fabsf(right.y) < 0.001f && fabsf(right.z) < 0.001f)
	{
		right = Vec3(dir.z, 0, -dir.x);
	}
	right = normalize(right);
	Vec3 up = normalize(cross(dir, right));
	right *= minimum(1.0f, 5 * dir_len);
	up *= minimum(1.0f, 5 * dir_len);

	Vec3 force = phy_module->getDistanceJointLinearForce(entity);

	float t = minimum(length(force) / 10.0f, 1.0f);
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

void showHingeJointGizmo(WorldView& view, 
	PhysicsModule& phy_module,
	const Vec2& limit,
	bool use_limit,
	const RigidTransform& global_frame1)
{
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

void showHingeJointGizmo(WorldView& view, ComponentUID cmp)
{
	auto* phy_module = static_cast<PhysicsModule*>(cmp.module);
	const EntityRef entity = (EntityRef)cmp.entity;
	const EntityPtr connected_body = phy_module->getJointConnectedBody(entity);
	Vec2 limit = phy_module->getHingeJointLimit(entity);
	bool use_limit = phy_module->getHingeJointUseLimit(entity);
	if (!connected_body.isValid()) return;
	RigidTransform global_frame1 = phy_module->getJointConnectedBodyLocalFrame(entity);
	global_frame1 = phy_module->getWorld().getTransform((EntityRef)connected_body).getRigidPart() * global_frame1;
	showHingeJointGizmo(view, *phy_module, limit, use_limit, global_frame1);
}

struct PhysicsUIPlugin final : StudioApp::GUIPlugin
{
	explicit PhysicsUIPlugin(StudioApp& app)
		: m_app(app)
		, m_simulated_entities(app.getAllocator())
		, m_reset_dynamic_entities(app.getAllocator())
	{
		app.getWorldEditor().worldDestroyed().bind<&PhysicsUIPlugin::onWorldDestroyed>(this);
		app.getSettings().registerOption("physics_ui_open", &m_is_window_open);
	}

	~PhysicsUIPlugin() {
		m_app.getWorldEditor().worldDestroyed().unbind<&PhysicsUIPlugin::onWorldDestroyed>(this);
	}

	void onWorldDestroyed() {
		m_is_simulating_selected = false;
		m_simulated_entities.clear();
		m_reset_dynamic_entities.clear();
	}

	bool isSimulatingSelected() { return m_is_simulating_selected; }

	void resetSimulation() {
		WorldEditor& editor = m_app.getWorldEditor();
		World* world = editor.getWorld();
		PhysicsModule* module = (PhysicsModule*)world->getModule(RIGID_ACTOR_TYPE);

		if (!m_is_simulating_selected) return;
		for (EntityRef e : m_reset_dynamic_entities) {
			module->setDynamicType(e, PhysicsModule::DynamicType::DYNAMIC);
		}

		m_reset_dynamic_entities.clear();
		m_simulated_entities.clear();
		m_is_simulating_selected = false;
	}

	void toggleSimulateSelected() {
		WorldEditor& editor = m_app.getWorldEditor();
		if (editor.isGameMode()) return;

		World* world = editor.getWorld();
		PhysicsModule* module = (PhysicsModule*)world->getModule(RIGID_ACTOR_TYPE);
		if (m_is_simulating_selected) {
			editor.beginCommandGroup("phys_sim_end");
			for (SimulatedEntity e : m_simulated_entities) {
				const Transform tr = world->getTransform(e.entity);
				world->setTransform(e.entity, e.start_transform);
				editor.setEntitiesPositionsAndRotations(&e.entity, &tr.pos, &tr.rot, 1);
			}
			editor.endCommandGroup();
			editor.lockGroupCommand();
			for (EntityRef e : m_reset_dynamic_entities) {
				module->setDynamicType(e, PhysicsModule::DynamicType::DYNAMIC);
			}

			m_reset_dynamic_entities.clear();
			m_simulated_entities.clear();
		}
		else {
			ASSERT(m_simulated_entities.empty());
			const Array<EntityRef>& selected =  editor.getSelectedEntities();
			for (EntityRef e : selected) {
				if (!world->hasComponent(e, RIGID_ACTOR_TYPE)) continue;
				SimulatedEntity& se = m_simulated_entities.emplace();
				se.entity = e;
				se.start_transform = world->getTransform(e);
			}
			for (EntityRef e : module->getDynamicActors()) {
				if (selected.indexOf(e) < 0) {
					module->setDynamicType(e, PhysicsModule::DynamicType::STATIC);
					m_reset_dynamic_entities.push(e);
				}
			}
			if (m_simulated_entities.empty()) return;
		}
		m_is_simulating_selected = !m_is_simulating_selected;
	}

	void update(float time_delta) override {
		if (!m_is_simulating_selected) return;
		if (m_app.getWorldEditor().isGameMode()) {
			resetSimulation();
			return;
		}

		PhysicsModule* module = (PhysicsModule*)m_app.getWorldEditor().getWorld()->getModule(RIGID_ACTOR_TYPE);
		module->forceUpdateDynamicActors(time_delta);
	}

	bool exportData(const char* dest_dir) override
	{
		#ifndef LUMIX_STATIC_PHYSX
			char exe_path[MAX_PATH];
			os::getExecutablePath(Span(exe_path));
			char exe_dir[MAX_PATH];

			const char* physx_dlls[] = {
				"PhysX_64.dll",
				"PhysXFoundation_64.dll",
				"PhysXCommon_64.dll",
				"PhysXCooking_64.dll",
			};
			for (const char* dll : physx_dlls)
			{
				copyString(Span(exe_dir), Path::getDir(exe_path));
				StaticString<MAX_PATH> tmp(exe_dir, dll);
				if (!os::fileExists(tmp)) return false;
				StaticString<MAX_PATH> dest(dest_dir, dll);
				if (!os::copyFile(tmp, dest))
				{
					logError("Failed to copy ", tmp, " to ", dest);
					return false;
				}
			}
		#endif
		return true; 
	}



	const char* getName() const override { return "physics"; }
	bool isOpen() const { return m_is_window_open; }
	void toggleUI() { m_is_window_open = !m_is_window_open; }


	void onLayersGUI()
	{
		PhysicsSystem* system = static_cast<PhysicsSystem*>(m_app.getEngine().getSystemManager().getSystem("physics"));
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
		PhysicsSystem* system = static_cast<PhysicsSystem*>(m_app.getEngine().getSystemManager().getSystem("physics"));
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
				const char* layer_name = system->getCollisionLayerName(i);
				float offset = basic_offset + i * 35.0f;
				ImGui::SetColumnOffset(-1, offset);
				ImGui::TextUnformatted(layer_name);
				ImGui::NextColumn();
			}
			ImGui::PopTextWrapPos();

			ImGui::Separator();
			for (int i = 0, c = system->getCollisionsLayersCount(); i < c; ++i)
			{
				ImGui::TextUnformatted(system->getCollisionLayerName(i));
				ImGui::NextColumn();

				for (int j = 0; j <= i; ++j)
				{
					bool b = system->canLayersCollide(i, j);
					if (ImGui::Checkbox(StaticString<10>("###", i, "-", j), &b))
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


	void onJointGUI(WorldEditor& editor) {
		World& world = *editor.getWorld();
		auto* phy_module = static_cast<PhysicsModule*>(world.getModule(RIGID_ACTOR_TYPE));
		auto* render_module = static_cast<RenderModule*>(world.getModule(MODEL_INSTANCE_TYPE));
		if (!render_module) return;

		int count = phy_module->getJointCount();
		if (count > 0 && ImGui::CollapsingHeader("Joints")) {
			ImGui::Columns(2);
			ImGui::Text("From");
			ImGui::NextColumn();
			ImGui::Text("To");
			ImGui::NextColumn();
			ImGui::PushID("joints");
			ImGui::Separator();
			for (int i = 0; i < count; ++i) {
				ComponentUID cmp;
				const EntityRef entity = phy_module->getJointEntity(i);
				cmp.entity = entity;
				cmp.module = phy_module;
				//physx::PxJoint* joint = phy_module->getJoint(entity);
				switch ((physx::PxJointConcreteType::Enum)phy_module->getJoint(entity)->getConcreteType()) {
					case physx::PxJointConcreteType::eDISTANCE:
						cmp.type = DISTANCE_JOINT_TYPE;
						// showDistanceJointGizmo(cmp);
						break;
					case physx::PxJointConcreteType::eREVOLUTE:
						cmp.type = HINGE_JOINT_TYPE;
						// showHingeJointGizmo(cmp);
						break;
					case physx::PxJointConcreteType::eSPHERICAL:
						cmp.type = SPHERICAL_JOINT_TYPE;
						// showSphericalJointGizmo(view, cmp); // TODO
						break;
					case physx::PxJointConcreteType::eD6:
						cmp.type = D6_JOINT_TYPE;
						/*showD6JointGizmo(world.getTransform(entity).getRigidPart(),
							*render_module,
							static_cast<physx::PxD6Joint*>(joint));*/
						// TODO
						break;
					default: ASSERT(false); break;
				}

				ImGui::PushID(i);
				char tmp[256];
				getEntityListDisplayName(m_app, world, Span(tmp), cmp.entity);
				bool b = false;
				if (ImGui::Selectable(tmp, &b)) editor.selectEntities(Span(&entity, 1), false);
				ImGui::NextColumn();

				EntityPtr other_entity = phy_module->getJointConnectedBody(entity);
				getEntityListDisplayName(m_app, world, Span(tmp), other_entity);
				if (other_entity.isValid() && ImGui::Selectable(tmp, &b)) {
					const EntityRef e = (EntityRef)other_entity;
					editor.selectEntities(Span(&e, 1), false);
				}
				ImGui::NextColumn();
				ImGui::PopID();
			}
			ImGui::Columns();
			ImGui::PopID();
		}
	}


	void onVisualizationGUI(WorldEditor& editor)
	{
		auto* module = static_cast<PhysicsModule*>(editor.getWorld()->getModule("physics"));
		DVec3 camera_pos = editor.getView().getViewport().pos;
		const Vec3 extents(20, 20, 20);
		module->setVisualizationCullingBox(camera_pos - extents, camera_pos + extents);

		if (!ImGui::CollapsingHeader("Visualization")) return;

		u32 viz_flags = module->getDebugVisualizationFlags();
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
		module->setDebugVisualizationFlags(viz_flags);
	}


	void onActorGUI(WorldEditor& editor)
	{
		if (!ImGui::CollapsingHeader("Actors")) return;

		if (editor.getSelectedEntities().empty()) {
			ImGui::Text("No entities selected.");
			return;
		}

		const EntityRef e = editor.getSelectedEntities()[0];

		World& world = *editor.getWorld();
		auto* module = static_cast<PhysicsModule*>(world.getModule("physics"));

		if(!module->getWorld().hasComponent(e, RIGID_ACTOR_TYPE)) {
			ImGui::Text("Entity does not have rigid actor component.");
			return;
		};
		
		char tmp[255];
		getEntityListDisplayName(m_app, world, Span(tmp), e);

		ImGui::TextUnformatted(tmp);
		ImGui::SameLine();
		bool is_debug_viz = module->isActorDebugEnabled(e);
		if (ImGui::Checkbox("Debug visualization", &is_debug_viz)) {
			module->enableActorDebug(e, is_debug_viz);
		}
	}

	void onDebugGUI(WorldEditor& editor) {
		if (!ImGui::CollapsingHeader("Debug")) return;

		ImGui::Indent();
		onVisualizationGUI(editor);
		onJointGUI(editor);
		onActorGUI(editor);
		ImGui::Unindent();
	}

	void autogeneratePhySkeleton(EntityRef entity, WorldEditor& editor) {
		editor.beginCommandGroup("ragdoll");
		World& world = *editor.getWorld();
		auto* phy_module = static_cast<PhysicsModule*>(world.getModule(RIGID_ACTOR_TYPE));
		auto* render_module = static_cast<RenderModule*>(world.getModule(MODEL_INSTANCE_TYPE));
		const Transform root_tr = world.getTransform(entity);
		ASSERT(render_module);
		Model* model = render_module->getModelInstanceModel(entity);
		ASSERT(model && model->isReady());
		Array<EntityRef> entities(m_app.getAllocator());
		for (int i = 0; i < model->getBoneCount(); ++i) {
			const Model::Bone& bone = model->getBone(i);

			const Transform tr = root_tr.compose(bone.transform);

			if (bone.parent_idx >= 0) {
				const Model::Bone& parent_bone = model->getBone(bone.parent_idx);
				const Vec3 parent_pos = parent_bone.transform.pos;
				const DVec3 pos = (root_tr.compose(parent_bone.transform)).pos;

				Quat rot = Quat::IDENTITY;
				if (squaredLength(parent_pos - bone.transform.pos) > 0.01f) {
					rot = Quat::vec3ToVec3(Vec3(0, 1, 0), normalize(parent_pos - bone.transform.pos));
				}
				const EntityRef bone_e = editor.addEntityAt(pos);
				editor.setEntitiesRotations(&bone_e, &rot, 1);
				entities.push(bone_e);
				Vec3 size(length(bone.transform.pos - parent_pos) * 0.5f);
				size.x *= 0.2f;
				size.z *= 0.2f;

				if (size.y > 0) {
					editor.addComponent(Span(&bone_e, 1), RIGID_ACTOR_TYPE);
					ComponentUID cmp;
					cmp.entity = bone_e;
					cmp.module = phy_module;
					cmp.type = RIGID_ACTOR_TYPE;
					editor.addArrayPropertyItem(cmp, "Box geometry");
					editor.addComponent(Span(&bone_e, 1), BONE_ATTACHMENT_TYPE);
					editor.setProperty(BONE_ATTACHMENT_TYPE, "", 0, "Parent", Span(&bone_e, 1), entity);
					editor.setProperty(BONE_ATTACHMENT_TYPE, "", 0, "Bone", Span(&bone_e, 1), bone.parent_idx);
					editor.setProperty(RIGID_ACTOR_TYPE, "Box geometry", 0, "Size", Span(&bone_e, 1), size);
					editor.setProperty(RIGID_ACTOR_TYPE, "Box geometry", 0, "Position offset", Span(&bone_e, 1), Vec3(0, -size.y, 0));
					editor.setProperty(RIGID_ACTOR_TYPE, "", 0, "Dynamic", Span(&bone_e, 1), (i32)PhysicsModule::DynamicType::KINEMATIC);

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
		}
		editor.endCommandGroup();
	}


	void onRagdollGUI(WorldEditor& editor)
	{
		if (!ImGui::CollapsingHeader("Ragdoll")) return;

		if (editor.getSelectedEntities().size() != 1) {
			ImGui::TextUnformatted("Please select single entity.");
			return;
		}

		EntityRef entity = editor.getSelectedEntities()[0];
		if (ImGui::Button("Autogenerate")) autogeneratePhySkeleton(entity, editor);
	}

	void onGUI() override
	{
		if (m_app.checkShortcut(m_simulate_selected, true)) toggleSimulateSelected();
		if (m_app.checkShortcut(m_toggle_ui, true)) m_is_window_open = !m_is_window_open;

		if (!m_is_window_open) return;
		if (ImGui::Begin("Physics", &m_is_window_open))
		{
			WorldEditor& editor = m_app.getWorldEditor();
			onLayersGUI();
			onCollisionMatrixGUI();
			onRagdollGUI(editor);
			onDebugGUI(editor);
		}

		ImGui::End();
	}

	struct SimulatedEntity {
		EntityRef entity;
		Transform start_transform;
	};

	StudioApp& m_app;
	bool m_is_window_open = false;
	Action m_toggle_ui{"Physics", "Physics - toggle UI", "physics_toggle_ui", "", Action::WINDOW};
	Action m_simulate_selected{"Simulate physics", "Physics - simulate physics for selected object", "simulate_physics_selected_obj", "", Action::TOOL};
	bool m_is_simulating_selected = false;
	Array<SimulatedEntity> m_simulated_entities;
	Array<EntityRef> m_reset_dynamic_entities;
};


struct PhysicsGeometryPlugin final : AssetBrowser::IPlugin
{
	explicit PhysicsGeometryPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("phy", PhysicsGeometry::TYPE);
	}

	const char* getLabel() const override { return "Physics geometry"; }

	StudioApp& m_app;
};

struct PhysicsMaterialPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	struct EditorWindow : AssetEditorWindow {
		EditorWindow(const Path& path, StudioApp& app)
			: AssetEditorWindow(app)
			, m_app(app)
		{
			m_resource = app.getEngine().getResourceManager().load<PhysicsMaterial>(path);
		}

		~EditorWindow() { m_resource->decRefCount(); }

		void save() {
			OutputMemoryStream blob(m_app.getAllocator());
			blob << "static_friction = " << m_resource->material->getStaticFriction() << "\n";
			blob << "dynamic_friction = " << m_resource->material->getDynamicFriction() << "\n";
			blob << "restitution = " << m_resource->material->getRestitution() << "\n";

			m_app.getAssetBrowser().saveResource(*m_resource, blob);
			m_dirty = false;
		}
		
		void windowGUI() override {
			CommonActions& actions = m_app.getCommonActions();
		
			if (ImGui::BeginMenuBar()) {
				if (actions.save.iconButton(m_dirty, &m_app)) save();
				if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_resource);
				if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(*m_resource);
				ImGui::EndMenuBar();
			}

			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
				return;
			}

			if (m_resource->isReady() && m_resource->material) {
				float static_friction = m_resource->material->getStaticFriction();
				float dynamic_friction = m_resource->material->getDynamicFriction();
				float restitution = m_resource->material->getRestitution();
				ImGuiEx::Label("Static friction");
				if (ImGui::DragFloat("##s", &static_friction)) {
					m_resource->material->setStaticFriction(static_friction);
					m_dirty = true;
				}
				ImGuiEx::Label("Dynamic friction");
				if (ImGui::DragFloat("##d", &dynamic_friction)) {
					m_resource->material->setDynamicFriction(dynamic_friction);
					m_dirty = true;
				}
				ImGuiEx::Label("Restitution");
				if (ImGui::DragFloat("##r", &restitution)) {
					m_resource->material->setRestitution(restitution);
					m_dirty = true;
				}
			}
		}
	
		const Path& getPath() override { return m_resource->getPath(); }
		const char* getName() const override { return "physics material editor"; }

		StudioApp& m_app;
		PhysicsMaterial* m_resource;
	};

	explicit PhysicsMaterialPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("pma", PhysicsMaterial::TYPE);
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "pma"; }
	void createResource(OutputMemoryStream& blob) override {}

	bool compile(const Path& src) override {
		// load
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, src_data)) return false;

		// parse
		float sf = 0.5f;
		float df = 0.5f;
		float rest = 0.1f;
		const ParseItemDesc descs[] = {
			{"static_friction", &sf},
			{"dynamic_friction", &df},
			{"restitution", &rest},
		};
		StringView sv((const char*)src_data.data(), (u32)src_data.size());
		if (!parse(sv, src.c_str(), descs)) return false;

		char tmp[64];
		OutputMemoryStream compiled(tmp, sizeof(tmp));
		PhysicsMaterial::Header header;
		compiled.write(header);
		compiled.write(sf);
		compiled.write(df);
		compiled.write(rest);

		return m_app.getAssetCompiler().writeCompiledResource(src, compiled);
	}

	const char* getLabel() const override { return "Physics material"; }

	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	StudioApp& m_app;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_geometry_plugin(app)
		, m_material_plugin(app)
		, m_ui_plugin(app)
	{
	}

	void init() override
	{
		m_app.addPlugin(m_ui_plugin);
		const char* geom_exts[] = { "phy" };
		m_app.getAssetBrowser().addPlugin(m_geometry_plugin, Span(geom_exts));
		const char* mat_exts[] = { "pma" };
		m_app.getAssetBrowser().addPlugin(m_material_plugin, Span(mat_exts));
		m_app.getAssetCompiler().addPlugin(m_material_plugin, Span(mat_exts));
	}


	bool showGizmo(WorldView& view, ComponentUID cmp) override
	{
		auto* phy_module = static_cast<PhysicsModule*>(cmp.module);
		World& world = phy_module->getWorld();
		
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
			float height = phy_module->getControllerHeight(entity);
			float radius = phy_module->getControllerRadius(entity);

			const DVec3 pos = world.getPosition(entity);
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
			physx::PxD6Joint* joint = static_cast<physx::PxD6Joint*>(phy_module->getJoint(entity));
			showD6JointGizmo(view, world.getTransform(entity).getRigidPart(), joint);
			return true;
		}

		return false;
	}


	~StudioAppPlugin() {
		m_app.removePlugin(m_ui_plugin);
		m_app.getAssetCompiler().removePlugin(m_material_plugin);
		m_app.getAssetBrowser().removePlugin(m_material_plugin);
		m_app.getAssetBrowser().removePlugin(m_geometry_plugin);
	}

	const char* getName() const override { return "physics"; }


	StudioApp& m_app;
	PhysicsUIPlugin m_ui_plugin;
	PhysicsMaterialPlugin m_material_plugin;
	PhysicsGeometryPlugin m_geometry_plugin;
};


} // anonymous


LUMIX_STUDIO_ENTRY(physics) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}

