#pragma once


#include <QDockWidget>

namespace Lumix
{
	class WorldEditor;
	class PipelineInstance;
}

class QDoubleSpinBox;
class QLabel;

class SceneView : public QDockWidget
{
	Q_OBJECT
public:
	explicit SceneView(QWidget* parent = nullptr);
	~SceneView();

	void shutdown();
	void setWorldEditor(Lumix::WorldEditor& editor);
	Lumix::PipelineInstance* getPipeline() const;
	QWidget* getViewWidget() { return m_view; }
	float getNavigationSpeed() const;
	void changeNavigationSpeed(float value);
	float getTimeDeltaMultiplier() const;
	bool isFrameDebuggerActive() const { return m_is_frame_debugger_active; }
	bool isFrameRequested() const { return m_is_frame_requested; }
	void frameServed() { m_is_frame_requested = false; }
	void setWireframe(bool wireframe);
	void render();

private:
	void onDistanceMeasured(float distance);
	virtual void resizeEvent(QResizeEvent*) override;
	virtual void dragEnterEvent(QDragEnterEvent *event) override;
	virtual void dropEvent(QDropEvent *event) override;

private:	
	Lumix::WorldEditor* m_world_editor;
	QWidget* m_view;
	QDoubleSpinBox* m_speed_input;
	QDoubleSpinBox* m_time_delta_multiplier_input;
	QLabel* m_measure_tool_label;
	int m_last_x;
	int m_last_y;
	bool m_is_frame_requested;
	bool m_is_frame_debugger_active;
	class WGLRenderDevice* m_render_device;
};

