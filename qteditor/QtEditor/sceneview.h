#pragma once


#include <QDockWidget>

namespace Lumix
{
	class EditorClient;
	class EditorServer;
	class PipelineInstance;
}

class SceneView : public QDockWidget
{
	Q_OBJECT
public:
	explicit SceneView(QWidget* parent = NULL);
	void setEditorClient(Lumix::EditorClient& client) { m_client = &client; }
	void setServer(Lumix::EditorServer* server) { m_server = server; }
	void setPipeline(Lumix::PipelineInstance& pipeline) { m_pipeline = &pipeline; }

private:
	virtual void mousePressEvent(QMouseEvent* event) override;
	virtual void mouseMoveEvent(QMouseEvent* event) override;
	virtual void mouseReleaseEvent(QMouseEvent* event) override;
	virtual void resizeEvent(QResizeEvent*) override;
	virtual void dragEnterEvent(QDragEnterEvent *event) override;
	virtual void dropEvent(QDropEvent *event) override;

private:	
	Lumix::EditorClient* m_client;
	Lumix::EditorServer* m_server;
	Lumix::PipelineInstance* m_pipeline;
	int m_last_x;
	int m_last_y;

signals:

public slots:

};

