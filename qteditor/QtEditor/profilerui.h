#pragma once

#include <QDockWidget>

class QAbstractItemModel;

namespace Ui 
{
class ProfilerUI;
}

class ProfilerUI : public QDockWidget
{
	Q_OBJECT

public:
	explicit ProfilerUI(QWidget* parent = NULL);
	~ProfilerUI();

private slots:
	void on_recordCheckBox_stateChanged(int arg1);
	void on_dataChanged();
	void on_frameSet();
	void on_profileTreeView_clicked(const QModelIndex &index);

	private:
	Ui::ProfilerUI* m_ui;
	QAbstractItemModel* m_model;
};


