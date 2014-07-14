#pragma once


#include <QDockWidget>

namespace Lumix
{
	class EditorClient;
	class EditorServer;
	class PipelineInstance;
}

class QDoubleSpinBox;

class SceneView : public QDockWidget
{
	Q_OBJECT
public:
	explicit SceneView(QWidget* parent = NULL);
	void setEditorClient(Lumix::EditorClient& client);
	void setServer(Lumix::EditorServer* server) { m_server = server; }
	void setPipeline(Lumix::PipelineInstance& pipeline) { m_pipeline = &pipeline; }
	QWidget* getViewWidget() { return m_view; }
	float getNavivationSpeed() const;

private:
	virtual void resizeEvent(QResizeEvent*) override;
	virtual void dragEnterEvent(QDragEnterEvent *event) override;
	virtual void dropEvent(QDropEvent *event) override;

private:	
	Lumix::EditorClient* m_client;
	Lumix::EditorServer* m_server;
	Lumix::PipelineInstance* m_pipeline;
	QWidget* m_view;
	QDoubleSpinBox* m_speed_input;
	int m_last_x;
	int m_last_y;

signals:

public slots:

};

