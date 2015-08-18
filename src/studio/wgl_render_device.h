#pragma once


namespace Lumix
{
	class Engine;
	class PipelineInstance;
	class WorldEditor;
}


class WGLRenderDevice
{
	public:
		WGLRenderDevice(Lumix::WorldEditor& editor,
						Lumix::Engine& engine,
						const char* pipeline_path);
		virtual ~WGLRenderDevice();

		void shutdown();
		Lumix::PipelineInstance& getPipeline();
		int getWidth() const;
		int getHeight() const;
		void setWidget(class QWidget& widget);

	private:
		void onUniverseCreated();
		void onUniverseDestroyed();

	private:
		Lumix::PipelineInstance* m_pipeline;
		Lumix::Engine& m_engine;
		Lumix::WorldEditor& m_editor;
};
