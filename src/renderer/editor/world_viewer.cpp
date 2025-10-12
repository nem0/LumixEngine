#include <imgui/imgui.h>

#include "animation/animation_module.h"
#include "engine/plugin.h"
#include "engine/resource_manager.h"
#include "editor/studio_app.h"
#include "editor/settings.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "world_viewer.h"

namespace Lumix {

static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType ENVIRONMENT_PROBE_TYPE = reflection::getComponentType("environment_probe");
static const ComponentType ENVIRONMENT_TYPE = reflection::getComponentType("environment");

WorldViewer::WorldViewer(StudioApp& app)
	: m_app(app)
{
	Engine& engine = m_app.getEngine();
	auto* renderer = static_cast<Renderer*>(engine.getSystemManager().getSystem("renderer"));
	m_viewport.is_ortho = false;
	m_viewport.fov = m_app.getFOV();
	m_viewport.near = 0.1f;
	m_viewport.far = 1000.f;
	m_viewport.pos = DVec3(0, 0, 0);
	m_viewport.rot = Quat::IDENTITY;

	m_world = &engine.createWorld();
	m_pipeline = Pipeline::create(*renderer, PipelineType::PREVIEW);

	const EntityRef mesh_entity = m_world->createEntity({0, 0, 0}, {0, 0, 0, 1});
	auto* render_module = static_cast<RenderModule*>(m_world->getModule(MODEL_INSTANCE_TYPE));
	m_mesh = mesh_entity;
	m_world->createComponent(MODEL_INSTANCE_TYPE, mesh_entity);

	const EntityRef env_probe = m_world->createEntity({0, 0, 0}, Quat::IDENTITY);
	m_world->createComponent(ENVIRONMENT_PROBE_TYPE, env_probe);
	render_module->getEnvironmentProbe(env_probe).inner_range = Vec3(1e3);
	render_module->getEnvironmentProbe(env_probe).outer_range = Vec3(1e3);

	Matrix light_mtx;
	light_mtx.lookAt({10, 10, 10}, Vec3::ZERO, {0, 1, 0});
	const EntityRef light_entity = m_world->createEntity({0, 0, 0}, light_mtx.getRotation());
	m_world->createComponent(ENVIRONMENT_TYPE, light_entity);
	render_module->getEnvironment(light_entity).direct_intensity = 3;
	render_module->getEnvironment(light_entity).indirect_intensity = 1;
	
	const EntityRef e = m_world->createEntity(DVec3(0, 0, 0), Quat::IDENTITY);
	m_world->createComponent(MODEL_INSTANCE_TYPE, e);
	m_world->setScale(e, Vec3(100));
	render_module->setModelInstancePath(e, Path("engine/models/plane.fbx"));

	m_pipeline->setWorld(m_world);
}

void WorldViewer::setModelPath(const Path& path) {
	RenderModule* module = (RenderModule*)m_world->getModule("renderer");
	module->setModelInstancePath(*m_mesh, path);
}

void WorldViewer::setAnimatorPath(const Path& path) {
	AnimationModule* module = (AnimationModule*)m_world->getModule("animation");
	ComponentType ANIMATOR_TYPE = reflection::getComponentType("animator");
	if (!m_world->hasComponent(*m_mesh, ANIMATOR_TYPE)) {
		m_world->createComponent(ANIMATOR_TYPE, *m_mesh);
	}
	module->setAnimatorSource(*m_mesh, path);
}

void WorldViewer::drawMeshTransform() {
	auto* render_module = (RenderModule*)m_world->getModule("renderer");
	Transform tr = m_world->getTransform(*m_mesh);
	render_module->addDebugLine(tr.pos, tr.pos + tr.rot.rotate(Vec3(1, 0, 0)), Color::RED);
	render_module->addDebugLine(tr.pos, tr.pos + tr.rot.rotate(Vec3(0, 1, 0)), Color::GREEN);
	render_module->addDebugLine(tr.pos, tr.pos + tr.rot.rotate(Vec3(0, 0, 1)), Color::BLUE);
}

void WorldViewer::drawSkeleton(i32 selected_bone) {
	auto* render_module = (RenderModule*)m_world->getModule("renderer");
	Pose* pose = render_module->lockPose(*m_mesh);
	Model* model = render_module->getModelInstanceModel(*m_mesh);
	if (pose && model->isReady()) {
		Transform tr = m_world->getTransform(*m_mesh);
		ASSERT(pose->is_absolute);
		for (u32 i = 0, c = model->getBoneCount(); i < c; ++i) {
			const Model::Bone& bone = model->getBone(i);
			const i32 parent_idx = bone.parent_idx;
			if (parent_idx < 0) continue;

			Color color = Color::BLUE;
			if (selected_bone == i) {
				color = Color::RED;
			}

			Vec3 bone_dir = pose->positions[i] - pose->positions[parent_idx];
			const float bone_len = length(bone_dir);
			const Quat r = pose->rotations[parent_idx];

			Vec3 up = r.rotate(Vec3(0, 0, 0.06f * bone_len));
			Vec3 right = r.rotate(Vec3(0.12f * bone_len, 0, 0));

			DVec3 a = tr.transform(pose->positions[parent_idx]);
			render_module->addDebugBone(a, tr.rot.rotate(bone_dir), tr.rot.rotate(up), tr.rot.rotate(right), color);
		}
		render_module->unlockPose(*m_mesh, false);
	}
}


WorldViewer::~WorldViewer() {
	Engine& engine = m_app.getEngine();
	engine.destroyWorld(*m_world);
	m_pipeline.reset();
}

void WorldViewer::resetCamera() {
	RenderModule* module = (RenderModule*)m_world->getModule("renderer");
	Model* model = module->getModelInstanceModel(*m_mesh);
	if (model && model->isReady()) resetCamera(*model);
}

void WorldViewer::resetCamera(const Model& model) {
	if (model.getMeshCount() == 0) return;
	const AABB aabb = model.getAABB();
	const Vec3 center = (aabb.max + aabb.min) * 0.5f;
	m_viewport.pos = DVec3(0) + center + Vec3(1, 1, 1) * length(aabb.max - aabb.min);
	Matrix mtx;
	ASSERT(model.getCenterBoundingRadius() > 0);
	Vec3 eye = center + Vec3(model.getCenterBoundingRadius() * 2);
	mtx.lookAt(eye, center, normalize(Vec3(1, -1, 1)));
	mtx = mtx.inverted();
	m_viewport.rot = mtx.getRotation();
	m_camera_speed = 1;
}

void WorldViewer::gui() {
	auto* render_module = static_cast<RenderModule*>(m_world->getModule(MODEL_INSTANCE_TYPE));
	ASSERT(render_module);

	ImVec2 image_size = ImGui::GetContentRegionAvail();
	image_size.y = maximum(200.f, image_size.y);

	m_viewport.fov = m_app.getFOV();
	m_viewport.w = (int)image_size.x;
	m_viewport.h = (int)image_size.y;
	Viewport vp = m_viewport;
	if (m_follow_mesh) {
		vp.pos += m_world->getPosition(*m_mesh);
	}
	m_pipeline->setViewport(vp);
	m_pipeline->render(false);
	gpu::TextureHandle preview = m_pipeline->getOutput();
	const ImVec2 view_pos = ImGui::GetCursorScreenPos();
	if (gpu::isOriginBottomLeft()) {
		ImGui::Image(preview, image_size);
	}
	else {
		ImGui::Image(preview, image_size, ImVec2(0, 1), ImVec2(1, 0));
	}
	
	if (m_is_mouse_captured) {
		os::Rect rect;
		rect.left = (i32)view_pos.x;
		rect.top = (i32)view_pos.y;
		rect.width = (i32)image_size.x;
		rect.height = (i32)image_size.y;
		m_app.setMouseClipRect(ImGui::GetWindowViewport()->PlatformHandle, rect);
	}

	const bool mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	if (m_is_mouse_captured && (!mouse_down || !m_app.isMouseCursorClipped())) {
		m_is_mouse_captured = false;
		m_app.unclipMouseCursor();
		os::showCursor(true);
		os::setMouseScreenPos(m_captured_mouse_pos.x, m_captured_mouse_pos.y);
	}

	if ((m_is_mouse_captured || ImGui::IsItemHovered()) && mouse_down) {
		Vec2 delta(0, 0);
		for (const os::Event e : m_app.getEvents()) {
			if (e.type == os::Event::Type::MOUSE_MOVE) {
				delta += Vec2((float)e.mouse_move.xrel, (float)e.mouse_move.yrel);
			}
		}

		if (!m_is_mouse_captured) {
			m_is_mouse_captured = true;
			m_app.clipMouseCursor();
			os::showCursor(false);
			m_captured_mouse_pos = os::getMouseScreenPos();
		}

		m_camera_speed = maximum(0.01f, m_camera_speed + ImGui::GetIO().MouseWheel / 20.0f);

		auto moveCamera = [&](const Vec3& dir){
			float speed = m_camera_speed;
			if (os::isKeyDown(os::Keycode::SHIFT)) speed *= 10;
			const Vec3 d = m_viewport.rot.rotate(dir);
			m_viewport.pos -= d * m_app.getEngine().getLastTimeDelta() * speed;
		};

		const CommonActions& actions = m_app.getCommonActions();
		const bool is_orbit = actions.cam_orbit.isActive();
		if (actions.cam_forward.isActive()) moveCamera(Vec3(0, 0, 1));
		else if (actions.cam_backward.isActive())  moveCamera(Vec3(0, 0, -1));

		if (!is_orbit) {
			if (actions.cam_left.isActive())  moveCamera(Vec3(1, 0, 0));
			else if (actions.cam_right.isActive())  moveCamera(Vec3(-1, 0, 0));
			if (actions.cam_up.isActive())  moveCamera(Vec3(0, 1, 0));
			else if (actions.cam_down.isActive())  moveCamera(Vec3(0, -1, 0));
		}

		if (delta.x != 0 || delta.y != 0) {
			Quat rot = m_viewport.rot;

			float yaw = m_app.getSettings().m_mouse_sensitivity_x.eval(delta.x);
			Quat yaw_rot(Vec3(0, 1, 0), yaw);
			rot = normalize(yaw_rot * rot);

			Vec3 pitch_axis = rot.rotate(Vec3(1, 0, 0));
			float pitch = m_app.getSettings().m_mouse_sensitivity_y.eval(delta.y);
			Quat pitch_rot(pitch_axis, pitch);
			rot = normalize(pitch_rot * rot);

			if (is_orbit) {
				Vec3 dir = rot.rotate(Vec3(0, 0, 1));
				Vec3 origin = Vec3::ZERO;
				Model* model = render_module->getModelInstanceModel(*m_mesh);
				if (model && model->isReady()) {
					const AABB& aabb = model->getAABB();
					origin = (aabb.min + aabb.max) * 0.5f;
				}
				const float dist = float(length(origin - Vec3(m_viewport.pos)));
				m_viewport.pos = DVec3(origin + dir * dist);
			}

			m_viewport.rot = rot;
		}
	}
}

}