#pragma once


#include <QDockWidget>


namespace Lumix
{
	class EditorServer;
	class PipelineInstance;
}

namespace Ui
{
	class GameView;
}

class GameView : public QDockWidget
{
	Q_OBJECT

public:
	explicit GameView(QWidget* parent = NULL);
	virtual ~GameView();

	QWidget* getContentWidget() const;
	void setPipeline(Lumix::PipelineInstance& pipeline) { m_pipeline = &pipeline; }

private slots:
	void on_playButton_clicked();

private:
	virtual void resizeEvent(QResizeEvent*) override;

private:
	Ui::GameView* m_ui;
	Lumix::PipelineInstance* m_pipeline;
	Lumix::EditorServer* m_server;
};

