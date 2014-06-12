#pragma once

#include <QDockWidget>
#include <QStyledItemDelegate>

class QAbstractItemModel;

namespace Ui 
{
class ProfilerUI;
}

class HistoryDelegate : public QStyledItemDelegate
{
	Q_OBJECT

	public:
		HistoryDelegate(QWidget* parent = NULL);

		void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;	
};

class ProfilerUI : public QDockWidget
{
	Q_OBJECT

public:
	explicit ProfilerUI(QWidget* parent = NULL);
	~ProfilerUI();

private slots:
	void on_recordCheckBox_stateChanged(int arg1);
	void on_frameSlider_valueChanged(int value);

private:
	Ui::ProfilerUI* m_ui;
	QAbstractItemModel* m_model;
};


