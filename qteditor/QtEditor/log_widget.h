#pragma once

#include <QDockWidget>

namespace Ui {
class LogWidget;
}

class LogWidget : public QDockWidget
{
	Q_OBJECT

public:
	explicit LogWidget(QWidget *parent = NULL);
	~LogWidget();

private slots:
	void on_clearButton_clicked();

private:
	void onInfo(const char* system, const char* message);

private:
	Ui::LogWidget *m_ui;
};

