#pragma once


#include <QDockWidget>

namespace Lux
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
	void setEditorClient(Lux::EditorClient& client) { m_client = &client; }
	void setServer(Lux::EditorServer* server) { m_server = server; }
	void setPipeline(Lux::PipelineInstance& pipeline) { m_pipeline = &pipeline; }

private:
	virtual void mousePressEvent(QMouseEvent* event) override;
	virtual void mouseMoveEvent(QMouseEvent* event) override;
	virtual void mouseReleaseEvent(QMouseEvent* event) override;
	virtual void resizeEvent(QResizeEvent*) override;
	virtual void dragEnterEvent(QDragEnterEvent *event) override;
	virtual void dropEvent(QDropEvent *event) override;

private:	
	Lux::EditorClient* m_client;
	Lux::EditorServer* m_server;
	Lux::PipelineInstance* m_pipeline;
	int m_last_x;
	int m_last_y;

signals:

public slots:

};

