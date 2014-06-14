#pragma once

#include <QWidget>
#include "core/profiler.h"
#include "profilerui.h"

namespace Ui 
{
	class ProfilerGraph;
}

class ProfilerGraph : public QWidget
{
		Q_OBJECT

	public:
		explicit ProfilerGraph(QWidget* parent = NULL);
		~ProfilerGraph();

		virtual void paintEvent(QPaintEvent*) override;
		virtual void mousePressEvent(QMouseEvent*) override;
		virtual void mouseMoveEvent(QMouseEvent*) override;
		int getFrame() const { return m_frame; }
		void setBlock(ProfileModel::Block* block) { m_block = block; }
		void setModel(ProfileModel* model) { m_model = model; }

	private:
		void getBlockPath(ProfileModel::Block* block, QPainterPath& path, float max);

	signals:
		void frameSet();

	private:
		Ui::ProfilerGraph* m_ui;
		int m_frame;
		ProfileModel* m_model;
		ProfileModel::Block* m_block;
};

