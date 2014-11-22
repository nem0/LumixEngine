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
	m_model = NULL;
}

ProfilerGraph::~ProfilerGraph()
{
	delete m_ui;
}


void ProfilerGraph::mousePressEvent(QMouseEvent* event)
{
	m_frame = m_model->getRoot() ? m_model->getRoot()->m_frames.size() * event->x() / width() : 0;
	update();
	emit frameSet();
}


void ProfilerGraph::mouseMoveEvent(QMouseEvent* event)
{
	if(event->buttons() & Qt::LeftButton)
	{
		m_frame = m_model->getRoot() ? m_model->getRoot()->m_frames.size() * event->x() / width() : 0;
		update();
		emit frameSet();
	}
}

void ProfilerGraph::getRootPath(QPainterPath& path, float max)
{
	ProfileModel::Block* root = m_model->getRoot();

	int w = width();
	int h = height();

	path.moveTo(0, height());

	if (max > 0)
	{
		for (int i = 0; i < root->m_frames.size(); ++i)
		{
			float l = i * w / (float)m_model->getRoot()->m_frames.size();
			float time = 0;
			ProfileModel::Block* block = root;
			while (block)
			{
				time += block->m_frames[i];
				block = block->m_next;
			}
			float t = (h - 1) * (1.0f - time / max);
			path.lineTo(l, t);
		}
	}
	path.lineTo(width(), height());
	path.closeSubpath();

}


void ProfilerGraph::getBlockPath(ProfileModel::Block* block, QPainterPath& path, float max)
{
	int w = width();
	int h = height();

	path.moveTo(0, height());
	
	if(max > 0)
	{
		for (int i = 0; i < block->m_frames.size(); ++i)
		{
			float l = i * w / (float)m_model->getRoot()->m_frames.size();
			float time = block->m_frames[i];
			float t = (h - 1) * (1.0f - time / max);
			path.lineTo(l, t);
		}
	}
	path.lineTo(width(), height());
	path.closeSubpath();

}


void ProfilerGraph::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::RenderHint::Antialiasing);
	ProfileModel::Block* root = m_model->getRoot();
	if(!root)
	{
		return;
	}
	painter.setPen(QColor(255, 255, 255));
	QLinearGradient gradient(0, 0, 0, 100);
	gradient.setColorAt(0.0, QColor(0, 255, 0, 128));
	gradient.setColorAt(1.0, QColor(0, 64, 0, 128));
	gradient.setSpread(QGradient::Spread::ReflectSpread);

	float max = 0;
	for (int i = 0; i < root->m_frames.size(); ++i)
	{
		ProfileModel::Block* block = root;
		float time = 0;
		while (block)
		{
			time += block->m_frames[i];
			block = block->m_next;
		}
		max = max < time ? time : max;
	}

	QPainterPath path;
	getRootPath(path, max);
	painter.fillPath(path, gradient);
	if(m_block)
	{
		QPainterPath detail_path;
		getBlockPath(m_block, detail_path, max);
		gradient.setColorAt(0.0, QColor(0, 0, 255, 192));
		gradient.setColorAt(1.0, QColor(0, 0, 64, 192));
		painter.fillPath(detail_path, gradient);
	}

	painter.setPen(QColor(255, 0, 0));
	float l = m_frame * width() / m_model->getRoot()->m_frames.size();
	painter.drawLine(QPointF(l, 0), QPointF(l, height()));
}
