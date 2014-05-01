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

		virtual void mouseReleaseEvent(QMouseEvent* event) override
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
			m_latitude = Lux::Math::clamp(m_latitude + x * 0.01f, -Lux::Math::PI, Lux::Math::PI);
			//m_latitude = Lux::Math::PI * 0.5f;
			m_longitude = Lux::Math::clamp(m_longitude + y * 0.01f, -Lux::Math::PI, Lux::Math::PI);
			//m_longitude = Lux::Math::PI * 0.25f;
			Lux::Entity camera = m_render_device->getPipeline().getScene()->getCameraInSlot("editor").entity;
			Lux::Matrix mtx = Lux::Matrix::IDENTITY;
			float clat = cosf(m_latitude);
			float slat = sinf(m_latitude);
			float clon = cosf(m_longitude);
			float slon = sinf(m_longitude);
			Lux::Vec3 pos = Lux::Vec3(cosf(m_longitude) * sinf(m_latitude) * 5, sinf(m_longitude) * 5, cosf(m_latitude) * cosf(m_longitude) * 5);
			Lux::Vec3 dir = pos;
			dir.normalize();
			Lux::Vec3 up(-sinf(m_latitude) * sinf(m_longitude), cosf(m_longitude), -cosf(m_latitude) * sinf(m_longitude));
			float t = Lux::dotProduct(dir, up);
			Lux::Vec3 right = Lux::crossProduct(up, dir);
			right.normalize();
			up = Lux::crossProduct(dir, right);
			up.normalize();
			mtx.setTranslation(pos + Lux::Vec3(0, 0, -5));
			mtx.setZVector(dir);
			mtx.setXVector(right);
			mtx.setYVector(up);
			camera.setMatrix(mtx);
/*
			Lux::Quat yaw_rot(Lux::Vec3(0, 1, 0), -x / 200.0f);
			rot = rot * yaw_rot;
			rot.normalize();
	
			Lux::Vec3 axis = rot * Lux::Vec3(1, 0, 0);
			Lux::Quat pitch_rot(axis, -y / 200.0f);
			rot = rot * pitch_rot;
			rot.normalize();

			Lux::Matrix camera_mtx;
			rot.toMatrix(camera_mtx);

			camera_mtx.setTranslation(pos);
			camera.setMatrix(camera_mtx);*/
		}

		Lux::IRenderDevice* m_render_device;
		Lux::Engine* m_engine;
		int m_last_x;
		int m_last_y;
		float m_latitude;
		float m_longitude;
		bool m_is_down;
};
