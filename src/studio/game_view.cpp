#include "game_view.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "engine/engine.h"
#include "ocornut-imgui/imgui.h"
#include "renderer/frame_buffer.h"
#include "renderer/pipeline.h"
#include "renderer/texture.h"


GameView::GameView()
	: m_is_opened(true)
	, m_pipeline(nullptr)
	, m_pipeline_source(nullptr)
{
}


GameView::~GameView()
{
}


void GameView::init(Lumix::WorldEditor& editor)
{
	auto& engine = editor.getEngine();
	auto* pipeline_manager = engine.getResourceManager().get(Lumix::ResourceManager::PIPELINE);
	auto* resource = pipeline_manager->load(Lumix::Path("pipelines/game_view.lua"));

	m_pipeline_source = static_cast<Lumix::Pipeline*>(resource);
	m_pipeline = Lumix::PipelineInstance::create(*m_pipeline_source, engine.getAllocator());
}


void GameView::shutdown()
{
	Lumix::PipelineInstance::destroy(m_pipeline);
	auto& rm = m_pipeline_source->getResourceManager();
	rm.get(Lumix::ResourceManager::PIPELINE)->unload(*m_pipeline_source);

	m_pipeline = nullptr;
	m_pipeline_source = nullptr;
}


void GameView::setScene(Lumix::RenderScene* scene)
{
	m_pipeline->setScene(scene);
}


void GameView::onGui()
{
	PROFILE_FUNCTION();
	if (!m_is_opened) return;
	if (!m_pipeline_source->isReady()) return;

	if (ImGui::Begin("Game view", &m_is_opened))
	{
		//m_is_gameview_hovered = ImGui::IsWindowHovered();
		auto size = ImGui::GetContentRegionAvail();
		if (size.x > 0 && size.y > 0)
		{
			auto pos = ImGui::GetWindowPos();
			auto cp = ImGui::GetCursorPos();
			int gameview_x = int(pos.x + cp.x);
			int gameview_y = int(pos.y + cp.y);
			m_pipeline->setViewport(0, 0, int(size.x), int(size.y));

			auto* fb = m_pipeline->getFramebuffer("default");
			m_texture_handle = fb->getRenderbufferHandle(0);
			ImGui::Image(&m_texture_handle, size);
			m_pipeline->render();
		}
	}
	ImGui::End();
}
