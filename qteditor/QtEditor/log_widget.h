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

signals:
	void infoReceived(const QString& system, const QString& message);

private slots:
	void on_clearButton_clicked();
	void onInfoReceived(const QString& system, const QString& message);

private:
	void onInfo(const char* system, const char* message);

private:
	Ui::LogWidget* m_ui;
};

