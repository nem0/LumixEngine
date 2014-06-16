#pragma once


#include <QDockWidget>


namespace Lumix
{
	class EditorClient;
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
	void setEditorClient(Lumix::EditorClient& client) { m_client = &client; }

private slots:
	void on_playButton_clicked();

private:
	virtual void resizeEvent(QResizeEvent*) override;

private:
	Ui::GameView* m_ui;
	Lumix::PipelineInstance* m_pipeline;
	Lumix::EditorClient* m_client;
};

