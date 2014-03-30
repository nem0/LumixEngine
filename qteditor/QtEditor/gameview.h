#ifndef GAMEVIEW_H
#define GAMEVIEW_H

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
    ~GameView();

	QWidget* getContentWidget() const;
	void setPipeline(Lux::PipelineInstance& pipeline) { m_pipeline = &pipeline; }

private:
	virtual void resizeEvent(QResizeEvent *) override;

private:
    Ui::GameView *ui;
	Lux::PipelineInstance* m_pipeline;
};

#endif // GAMEVIEW_H
