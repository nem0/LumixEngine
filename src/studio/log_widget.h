#pragma once

#include <QDockWidget>
#include <qelapsedtimer.h>


namespace Ui
{
class LogWidget;
}

class LogWidget : public QDockWidget
{
	Q_OBJECT

public:
	enum Type
	{
		ERROR,
		WARNING,
		INFO,
		TYPE_COUNT
	};

public:
	explicit LogWidget(QWidget* parent = nullptr);
	~LogWidget();

signals:
	void logReceived(Type type, const QString& system, const QString& message);

private slots:
	void on_clearButton_clicked();
	void onLogReceived(Type type, const QString& system, const QString& message);
	void onTabChanged(int index);

private:
	void updateCountersUI(); 
	void onInfo(const char* system, const char* message);
	void onWarning(const char* system, const char* message);
	void onError(const char* system, const char* message);

private:
	QElapsedTimer m_timer;
	Ui::LogWidget* m_ui;
	int m_all_logs_count[TYPE_COUNT];
	int m_new_logs_count[TYPE_COUNT];
};
