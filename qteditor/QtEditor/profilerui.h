#pragma once

#include <QAbstractItemModel>
#include <QDockWidget>
#include <QSortFilterProxyModel>
#include "core/profiler.h"

namespace Ui 
{
class ProfilerUI;
}

class ProfileModel;


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
	ProfileModel* m_model;
	QSortFilterProxyModel* m_sortable_model;
};


class ProfileModel : public QAbstractItemModel
{
	public:
		enum class Values
		{
			NAME,
			LENGTH,
			LENGTH_EXCLUSIVE,
			HIT_COUNT,
			FUNCTION,
			COUNT
		};

		struct Block
		{
			Block();
			~Block() {}

			const char* m_name;
			const char* m_function;
			Block* m_parent;
			Block* m_first_child;
			Block* m_next;
			QList<float> m_frames;
			QList<int> m_hit_counts;
		};

	public:
		ProfileModel(QWidget* parent);
		Block* getRoot() { return m_root; }
		void setFrame(int frame) { m_frame = frame; } 

	private:
		void cloneBlock(Block* my_block, Lumix::Profiler::Block* remote_block);
		void onFrame();

		virtual QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
		virtual QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
		virtual QModelIndex parent(const QModelIndex& index) const override;
		virtual int rowCount(const QModelIndex& parent_index) const override;
		virtual int columnCount(const QModelIndex&) const override;
		virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

	private:
		Block* m_root;
		int m_frame;
};

