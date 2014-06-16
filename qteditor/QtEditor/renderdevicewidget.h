#pragma once


#include <qwidget.h>
#include <qevent.h>
#include "graphics/irender_device.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "engine/engine.h"


class RenderDeviceWidget : public QWidget
{
	public:
		RenderDeviceWidget(QWidget* parent)
			: QWidget(parent)
		{
			m_render_device = NULL;
			m_is_down = false;
			m_latitude = m_longitude = 0;
		}

		virtual void paintEvent(QPaintEvent*) override
		{
			if(m_render_device)
			{
				m_render_device->beginFrame();
				m_engine->getRenderer().render(*m_render_device);
				m_render_device->endFrame();
			}
		}

		virtual void mousePressEvent(QMouseEvent* event) override
		{
			m_last_x = event->x();
			m_last_y = event->y();
			m_is_down = true;
		}

		virtual void mouseMoveEvent(QMouseEvent* event) override
		{
			if(m_is_down)
			{
				rotateCamera(event->x() - m_last_x, event->y() - m_last_y);
				m_last_x = event->x();
				m_last_y = event->y();
			}
		}

		virtual void mouseReleaseEvent(QMouseEvent*) override
		{
			m_is_down = false;
		}

		virtual void resizeEvent(QResizeEvent* event) override
		{
			if(m_render_device)
			{
				m_render_device->getPipeline().resize(event->size().width(), event->size().height());
			}
		}

		void rotateCamera(int x, int y)
		{
			m_latitude = Lumix::Math::clamp(m_latitude + x * 0.01f, -Lumix::Math::PI, Lumix::Math::PI);
			m_longitude = Lumix::Math::clamp(m_longitude + y * 0.01f, -Lumix::Math::PI, Lumix::Math::PI);
			Lumix::Entity camera = m_render_device->getPipeline().getScene()->getCameraInSlot("editor").entity;
			Lumix::Matrix mtx = Lumix::Matrix::IDENTITY;
			Lumix::Vec3 pos = Lumix::Vec3(cosf(m_longitude) * sinf(m_latitude) * 5, sinf(m_longitude) * 5, cosf(m_latitude) * cosf(m_longitude) * 5);
			Lumix::Vec3 dir = pos;
			dir.normalize();
			Lumix::Vec3 up(-sinf(m_latitude) * sinf(m_longitude), cosf(m_longitude), -cosf(m_latitude) * sinf(m_longitude));
			Lumix::Vec3 right = Lumix::crossProduct(up, dir);
			right.normalize();
			up = Lumix::crossProduct(dir, right);
			up.normalize();
			mtx.setTranslation(pos + Lumix::Vec3(0, 0, -5));
			mtx.setZVector(dir);
			mtx.setXVector(right);
			mtx.setYVector(up);
			camera.setMatrix(mtx);
		}

		Lumix::IRenderDevice* m_render_device;
		Lumix::Engine* m_engine;
		int m_last_x;
		int m_last_y;
		float m_latitude;
		float m_longitude;
		bool m_is_down;
};
