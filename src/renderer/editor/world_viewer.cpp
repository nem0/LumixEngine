#include <imgui/imgui.h>

#include "engine/plugin.h"
#include "engine/resource_manager.h"
#include "editor/studio_app.h"
#include "editor/settings.h"
#include "editor/utils.h"
#include "renderer/model.h"
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

	m_world = &engine.createWorld(false);
	PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
	m_pipeline = Pipeline::create(*renderer, pres, "PREVIEW");

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
	render_module->setModelInstancePath(e, Path("models/shapes/plane.fbx"));

	m_pipeline->setWorld(m_world);
}

WorldViewer::~WorldViewer() {
	Engine& engine = m_app.getEngine();
	engine.destroyWorld(*m_world);
	m_pipeline.reset();
}

void WorldViewer::resetCamera(const Model& model) {
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
	m_pipeline->setViewport(m_viewport);
	m_pipeline->render(false);
	gpu::TextureHandle preview = m_pipeline->getOutput();
	if (gpu::isOriginBottomLeft()) {
		ImGui::Image(preview, image_size);
	}
	else {
		ImGui::Image(preview, image_size, ImVec2(0, 1), ImVec2(1, 0));
	}
		
	const bool mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	if (m_is_mouse_captured && !mouse_down) {
		m_is_mouse_captured = false;
		m_app.setCursorCaptured(false);
		os::showCursor(true);
		os::grabMouse(os::INVALID_WINDOW);
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
			m_app.setCursorCaptured(true);
			os::showCursor(false);
			os::grabMouse(ImGui::GetWindowViewport()->PlatformHandle);
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
			const Vec2 mouse_sensitivity = m_app.getSettings().m_mouse_sensitivity;
			Quat rot = m_viewport.rot;

			float yaw = signum(delta.x) * (powf(fabsf(delta.x / 10000 * mouse_sensitivity.x), 1.2f));
			Quat yaw_rot(Vec3(0, 1, 0), yaw);
			rot = normalize(yaw_rot * rot);

			Vec3 pitch_axis = rot.rotate(Vec3(1, 0, 0));
			float pitch = signum(delta.y) * (powf(fabsf(delta.y / 10000 * mouse_sensitivity.y), 1.2f));
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