#ifndef SCENEVIEW_H
#define SCENEVIEW_H

#include <QDockWidget>

namespace Lux
{
	class EditorClient;
	class EditorServer;
	class Pipeline;
}

class SceneView : public QDockWidget
{
    Q_OBJECT
public:
    explicit SceneView(QWidget* parent = 0);
	void setClient(Lux::EditorClient* client) { m_client = client; }
	void setServer(Lux::EditorServer* server) { m_server = server; }
	void setPipeline(Lux::Pipeline& pipeline) { m_pipeline = &pipeline; }

private:
	virtual void mousePressEvent(QMouseEvent* event) override;
	virtual void mouseMoveEvent(QMouseEvent* event) override;
	virtual void mouseReleaseEvent(QMouseEvent* event) override;
	virtual void resizeEvent(QResizeEvent *) override;

private:	
	Lux::EditorClient* m_client;
	Lux::EditorServer* m_server;
	Lux::Pipeline* m_pipeline;
	int m_last_x;
	int m_last_y;

signals:

public slots:

};

#endif // SCENEVIEW_H
