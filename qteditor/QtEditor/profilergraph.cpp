#include "profilergraph.h"
#include <QMouseEvent>
#include <qpainter.h>
#include <qpixmap.h>
#include "ui_profilergraph.h"


ProfilerGraph::ProfilerGraph(QWidget *parent)
	: QWidget(parent)
	, m_ui(new Ui::ProfilerGraph)
{
	m_ui->setupUi(this);
	m_frame = 0;
	m_block = NULL;
}

ProfilerGraph::~ProfilerGraph()
{
	delete m_ui;
}


void ProfilerGraph::mousePressEvent(QMouseEvent* event)
{
	m_frame = Lux::Profiler::Block::MAX_FRAMES * event->x() / width();
	update();
	emit frameSet();
}


void ProfilerGraph::mouseMoveEvent(QMouseEvent* event)
{
	if(event->buttons() & Qt::LeftButton)
	{
		m_frame = Lux::Profiler::Block::MAX_FRAMES * event->x() / width();
		update();
		emit frameSet();
	}
}


void ProfilerGraph::getBlockPath(Lux::Profiler::Block* block, QPainterPath& path, float max)
{
	int w = width();
	int h = height();
	const Lux::Profiler::Block::Frame* frame = block->m_frames;

	path.moveTo(0, height());
	for(int i = 0; i < Lux::Profiler::Block::MAX_FRAMES - 1; ++i)
	{
		int idx = (block->m_frame_index + 1 + i) % Lux::Profiler::Block::MAX_FRAMES;
		float l = i * w / (float)Lux::Profiler::Block::MAX_FRAMES;
		float t = (h - 1) * (1.0f - frame[idx].m_length / max);
		path.lineTo(l, t);
	}
	path.lineTo(width(), height());
	path.closeSubpath();

}


void ProfilerGraph::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::RenderHint::Antialiasing);
	Lux::Profiler::Block* root = Lux::g_profiler.getRootBlock();
	if(!root)
	{
		return;
	}
	painter.setPen(QColor(255, 255, 255));
	QLinearGradient gradient(0, 0, 0, 100);
	gradient.setColorAt(0.0, QColor(0, 255, 0, 128));
	gradient.setColorAt(1.0, QColor(0, 64, 0, 128));
	gradient.setSpread(QGradient::Spread::ReflectSpread);

	const Lux::Profiler::Block::Frame* frame = root->m_frames;
	float max = 0;
	for(int i = 0; i < Lux::Profiler::Block::MAX_FRAMES; ++i)
	{
		max = max < frame[i].m_length ? frame[i].m_length : max;
	}

	QPainterPath path;
	getBlockPath(root, path, max);
	painter.fillPath(path, gradient);
	if(m_block && m_block != root)
	{
		/*ASSERT(m_block->m_frames[0].m_index == root->m_frames[0].m_index);
		for(int i = 0; i < Lux::Profiler::Block::MAX_FRAMES; ++i)
		{
			ASSERT(m_block->m_frames[i].m_length < root->m_frames[i].m_length);
		}*/
		QPainterPath detail_path;
		getBlockPath(m_block, detail_path, max);
		gradient.setColorAt(0.0, QColor(0, 0, 255, 192));
		gradient.setColorAt(1.0, QColor(0, 0, 64, 192));
		painter.fillPath(detail_path, gradient);
	}

	painter.setPen(QColor(255, 0, 0));
	float l = m_frame * width() / (float)Lux::Profiler::Block::MAX_FRAMES;
	painter.drawLine(QPointF(l, 0), QPointF(l, height()));
}