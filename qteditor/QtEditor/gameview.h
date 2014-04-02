#pragma once


#include <QDockWidget>


namespace Lux
{
	class PipelineInstance;
}

namespace Ui {
class GameView;
}

class GameView : public QDockWidget
{
	Q_OBJECT

public:
	explicit GameView(QWidget *parent = 0);
	virtual ~GameView();

	QWidget* getContentWidget() const;
	void setPipeline(Lux::PipelineInstance& pipeline) { m_pipeline = &pipeline; }

private:
	virtual void resizeEvent(QResizeEvent *) override;

private:
	Ui::GameView *m_ui;
	Lux::PipelineInstance* m_pipeline;
};

