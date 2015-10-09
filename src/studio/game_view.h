#pragma once


#include "editor/world_editor.h"
#include <bgfx/bgfx.h>


namespace Lumix
{
	class Pipeline;
	class PipelineInstance;
	class RenderScene;
}


class GameView
{
public:
	GameView();
	~GameView();

	void init(Lumix::WorldEditor& editor);
	void shutdown();
	void onGui();
	void setScene(Lumix::RenderScene* scene);

public:
	bool m_is_opened;
	Lumix::Pipeline* m_pipeline_source;
	Lumix::PipelineInstance* m_pipeline;
	bgfx::TextureHandle m_texture_handle;
};