#ifndef LOG_WIDGET_H
#define LOG_WIDGET_H

#include <QDockWidget>

namespace Ui {
class LogWidget;
}

class LogWidget : public QDockWidget
{
	Q_OBJECT

public:
	explicit LogWidget(QWidget *parent = 0);
	~LogWidget();

private slots:
	void on_clearButton_clicked();

private:
	void onInfo(const char* system, const char* message);

private:
	Ui::LogWidget *ui;
};

#endif // LOG_WIDGET_H
