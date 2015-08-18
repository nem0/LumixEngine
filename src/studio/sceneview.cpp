#include "sceneview.h"
#include "editor/world_editor.h"
#include "core/crc32.h"
#include "editor/ieditor_command.h"
#include "editor/measure_tool.h"
#include "engine.h"
#include "iplugin.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "insert_mesh_command.h"
#include "wgl_render_device.h"
#include <qapplication.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <qpushbutton.h>
#include <QVBoxLayout>


class ViewWidget : public QWidget
{
public:
	ViewWidget(SceneView& view, QWidget* parent)
		: QWidget(parent)
		, m_view(view)
		, m_world_editor(nullptr)
	{
		setAttribute(Qt::WA_PaintOnScreen);
		setMouseTracking(true);
	}

	virtual void mousePressEvent(QMouseEvent* event) override
	{
		m_world_editor->onMouseDown(event->x(),
									event->y(),
									event->button() == Qt::RightButton
										? Lumix::MouseButton::RIGHT
										: Lumix::MouseButton::LEFT);
		m_last_x = event->x();
		m_last_y = event->y();
		setFocus();
	}


	virtual QPaintEngine* paintEngine() const override { return nullptr; }

	virtual void wheelEvent(QWheelEvent* event) override
	{
		m_view.changeNavigationSpeed(event->delta() * 0.001f);
	}

	virtual void mouseMoveEvent(QMouseEvent* event) override
	{
		int flags = 0;
		flags |= Qt::ControlModifier & QApplication::keyboardModifiers()
					 ? (int)Lumix::WorldEditor::MouseFlags::CONTROL
					 : 0;
		flags |= Qt::AltModifier & QApplication::keyboardModifiers()
					 ? (int)Lumix::WorldEditor::MouseFlags::ALT
					 : 0;
		m_world_editor->onMouseMove(event->x(),
									event->y(),
									event->x() - m_last_x,
									event->y() - m_last_y,
									flags);
		m_last_x = event->x();
		m_last_y = event->y();
	}

	virtual void mouseReleaseEvent(QMouseEvent* event) override
	{
		m_world_editor->onMouseUp(event->x(),
								  event->y(),
								  event->button() == Qt::RightButton
									  ? Lumix::MouseButton::RIGHT
									  : Lumix::MouseButton::LEFT);
	}

	Lumix::WorldEditor* m_world_editor;
	int m_last_x;
	int m_last_y;
	SceneView& m_view;
};

SceneView::SceneView(QWidget* parent)
	: QDockWidget(parent)
	, m_render_device(nullptr)
{
	m_measure_tool_label = new QLabel("");
	QWidget* root = new QWidget();
	QVBoxLayout* vertical_layout = new QVBoxLayout(root);
	QHBoxLayout* horizontal_layout = new QHBoxLayout(root);
	m_view = new ViewWidget(*this, root);
	m_speed_input = new QDoubleSpinBox(root);
	m_speed_input->setSingleStep(0.1f);
	m_speed_input->setValue(0.1f);


	horizontal_layout->addWidget(m_measure_tool_label);
	horizontal_layout->addStretch();
	horizontal_layout->addWidget(m_speed_input);
	horizontal_layout->setContentsMargins(0, 0, 0, 0);
	vertical_layout->addWidget(m_view);
	vertical_layout->addLayout(horizontal_layout);
	vertical_layout->setContentsMargins(0, 0, 0, 0);
	setWidget(root);
	setWindowTitle("Scene");
	setObjectName("sceneView");
	setAcceptDrops(true);
}


SceneView::~SceneView()
{
	delete m_render_device;
}


void SceneView::shutdown()
{
	m_render_device->shutdown();
}


void SceneView::render()
{
	if (!visibleRegion().isEmpty())
	{
		getPipeline()->render();
	}
}


void SceneView::setWorldEditor(Lumix::WorldEditor& world_editor)
{
	static_cast<ViewWidget*>(m_view)->m_world_editor = &world_editor;
	m_world_editor = &world_editor;
	m_render_device =
		new WGLRenderDevice(*m_world_editor, m_world_editor->getEngine(), "pipelines/main.lua");
	world_editor.getMeasureTool()
		->distanceMeasured()
		.bind<SceneView, &SceneView::onDistanceMeasured>(this);
}


Lumix::PipelineInstance* SceneView::getPipeline() const
{
	return m_render_device ? &m_render_device->getPipeline() : nullptr;
}


void SceneView::setWireframe(bool wireframe)
{
	getPipeline()->setWireframe(wireframe);
}


void SceneView::onDistanceMeasured(float distance)
{
	m_measure_tool_label->setText(
		QString("Measured distance: %1").arg(distance));
}


void SceneView::changeNavigationSpeed(float value)
{
	m_speed_input->setValue(
		Lumix::Math::maxValue(0.1f, (float)m_speed_input->value() + value));
}


float SceneView::getNavigationSpeed() const
{
	return m_speed_input->value();
}

void SceneView::dragEnterEvent(QDragEnterEvent* event)
{
	if (event->mimeData()->hasUrls())
	{
		event->acceptProposedAction();
	}
}


void SceneView::dropEvent(QDropEvent* event)
{
	const QList<QUrl>& list = event->mimeData()->urls();
	if (!list.empty())
	{
		QString file = list[0].toLocalFile();
		if (file.endsWith(".msh"))
		{
			Lumix::Vec3 position;
			Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(
				m_world_editor->getEditCamera().scene);

			Lumix::Vec3 origin;
			Lumix::Vec3 dir;
			scene->getRay(m_world_editor->getEditCamera().index,
						  event->pos().x(),
						  event->pos().y(),
						  origin,
						  dir);
			Lumix::RayCastModelHit hit =
				scene->castRay(origin, dir, Lumix::INVALID_COMPONENT);
			if (hit.m_is_hit)
			{
				position = hit.m_origin + hit.m_dir * hit.m_t;
			}
			else
			{
				position.set(0, 0, 0);
			}
			char rel_path[Lumix::MAX_PATH_LENGTH];
			m_world_editor->getRelativePath(rel_path, sizeof(rel_path), file.toLatin1().data());
			InsertMeshCommand* command =
				m_world_editor->getAllocator().newObject<InsertMeshCommand>(
					*static_cast<ViewWidget&>(*m_view).m_world_editor,
					position,
					Lumix::Path(rel_path));
			m_world_editor->executeCommand(command);
			Lumix::Entity entity = command->getEntity();
			m_world_editor->selectEntities(&entity, 1);
		}
	}
}


void SceneView::resizeEvent(QResizeEvent*)
{
	if (getPipeline())
	{
		getPipeline()->resize(m_view->width(), m_view->height());
	}
}