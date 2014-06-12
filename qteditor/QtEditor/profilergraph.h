#pragma once

#include <QWidget>
#include "core/profiler.h"

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
		void setBlock(Lux::Profiler::Block* block) { m_block = block; }

	private:
		void getBlockPath(Lux::Profiler::Block* block, QPainterPath& path, float max);

	signals:
		void frameSet();

	private:
		Ui::ProfilerGraph* m_ui;
		int m_frame;
		Lux::Profiler::Block* m_block;
};

