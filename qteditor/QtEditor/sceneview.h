#pragma once


#include <QDockWidget>

namespace Lumix
{
	class WorldEditor;
	class PipelineInstance;
}

class QDoubleSpinBox;

class SceneView : public QDockWidget
{
	Q_OBJECT
public:
	explicit SceneView(QWidget* parent = NULL);
	void setWorldEditor(Lumix::WorldEditor* editor);
	void setPipeline(Lumix::PipelineInstance& pipeline) { m_pipeline = &pipeline; }
	QWidget* getViewWidget() { return m_view; }
	float getNavivationSpeed() const;

private:
	virtual void resizeEvent(QResizeEvent*) override;
	virtual void dragEnterEvent(QDragEnterEvent *event) override;
	virtual void dropEvent(QDropEvent *event) override;

private:	
	Lumix::WorldEditor* m_world_editor;
	Lumix::PipelineInstance* m_pipeline;
	QWidget* m_view;
	QDoubleSpinBox* m_speed_input;
	int m_last_x;
	int m_last_y;

signals:

public slots:

};

