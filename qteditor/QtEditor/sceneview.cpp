#include "sceneview.h"
#include "editor/world_editor.h"
#include "core/crc32.h"
#include "editor/ieditor_command.h"
#include "engine/engine.h"
#include "engine/iplugin.h"
#include "graphics/pipeline.h"
#include "graphics/render_scene.h"
#include <qapplication.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QVBoxLayout>


static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const uint32_t CAMERA_HASH = crc32("camera");
static const uint32_t LIGHT_HASH = crc32("light");
static const uint32_t SCRIPT_HASH = crc32("script");
static const uint32_t ANIMABLE_HASH = crc32("animable");
static const uint32_t TERRAIN_HASH = crc32("terrain");


class ViewWidget : public QWidget
{
	public:
		class InsertMeshCommand : public Lumix::IEditorCommand
		{
			public:
				InsertMeshCommand(ViewWidget& widget, const Lumix::Vec3& position, const Lumix::Path& mesh_path)
					: m_mesh_path(mesh_path)
					, m_position(position)
					, m_widget(widget)
				{
					
				}


				virtual void execute() override
				{
					Lumix::Engine& engine = m_widget.m_world_editor->getEngine();
					m_entity = engine.getUniverse()->createEntity();
					m_entity.setPosition(m_position);
					const Lumix::Array<Lumix::IScene*>& scenes = engine.getScenes();
					Lumix::Component cmp;
					Lumix::IScene* scene = NULL;
					for (int i = 0; i < scenes.size(); ++i)
					{
						cmp = scenes[i]->createComponent(RENDERABLE_HASH, m_entity);
						if (cmp.isValid())
						{
							scene = scenes[i];
							break;
						}
					}
					if (cmp.isValid())
					{
						char rel_path[LUMIX_MAX_PATH];
						m_widget.m_world_editor->getRelativePath(rel_path, LUMIX_MAX_PATH, m_mesh_path.c_str());
						static_cast<Lumix::RenderScene*>(scene)->setRenderablePath(cmp, Lumix::string(rel_path));
					}
				}


				virtual void undo() override
				{
					const Lumix::Entity::ComponentList& cmps = m_entity.getComponents();
					for (int i = 0; i < cmps.size(); ++i)
					{
						cmps[i].scene->destroyComponent(cmps[i]);
					}
					m_widget.m_world_editor->getEngine().getUniverse()->destroyEntity(m_entity);
					m_entity = Lumix::Entity::INVALID;
				}


				virtual uint32_t getType() override
				{
					static const uint32_t type = crc32("insert_mesh");
					return type;
				}


				virtual bool merge(IEditorCommand&)
				{
					return false;
				}


				const Lumix::Entity& getEntity() const { return m_entity; }


			private:
				Lumix::Vec3 m_position;
				Lumix::Path m_mesh_path;
				Lumix::Entity m_entity;
				ViewWidget& m_widget;
		};

	public:
		ViewWidget(QWidget* parent)
			: QWidget(parent)
		{
			setMouseTracking(true);
		}

		virtual void mousePressEvent(QMouseEvent* event) override
		{
			m_world_editor->onMouseDown(event->x(), event->y(), event->button() == Qt::RightButton ? Lumix::MouseButton::RIGHT : Lumix::MouseButton::LEFT);
			m_last_x = event->x();
			m_last_y = event->y();
			setFocus();
		}

		virtual void mouseMoveEvent(QMouseEvent* event) override
		{
			int flags = 0;
			flags |= Qt::ControlModifier & QApplication::keyboardModifiers() ? (int)Lumix::WorldEditor::MouseFlags::CONTROL : 0;
			flags |= Qt::AltModifier & QApplication::keyboardModifiers() ? (int)Lumix::WorldEditor::MouseFlags::ALT : 0;
			m_world_editor->onMouseMove(event->x(), event->y(), event->x() - m_last_x, event->y() - m_last_y, flags);
			m_last_x = event->x();
			m_last_y = event->y();
		}

		virtual void mouseReleaseEvent(QMouseEvent* event) override
		{
			m_world_editor->onMouseUp(event->x(), event->y(), event->button() == Qt::RightButton ? Lumix::MouseButton::RIGHT : Lumix::MouseButton::LEFT);
		}

		Lumix::WorldEditor* m_world_editor;
		int m_last_x;
		int m_last_y;
};

SceneView::SceneView(QWidget* parent) :
	QDockWidget(parent)
{
	m_pipeline = NULL;
	QWidget* root = new QWidget();
	QVBoxLayout* vertical_layout = new QVBoxLayout(root);
	QHBoxLayout* horizontal_layout = new QHBoxLayout(root);
	m_view = new ViewWidget(root);
	m_speed_input = new QDoubleSpinBox(root);
	m_speed_input->setSingleStep(0.1f);
	m_speed_input->setValue(0.1f);
	horizontal_layout->addWidget(m_speed_input);
	horizontal_layout->addStretch();
	vertical_layout->addWidget(m_view);
	vertical_layout->addLayout(horizontal_layout);
	vertical_layout->setContentsMargins(0, 0, 0, 0);
	setWidget(root);
	setWindowTitle("Scene");
	setObjectName("sceneView");
	setAcceptDrops(true);
}


void SceneView::setWorldEditor(Lumix::WorldEditor* world_editor)
{
	static_cast<ViewWidget*>(m_view)->m_world_editor = world_editor;
	m_world_editor = world_editor;
}


float SceneView::getNavivationSpeed() const
{
	return m_speed_input->value();
}

void SceneView::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
	{
		event->acceptProposedAction();
	}
}


void SceneView::dropEvent(QDropEvent *event)
{
	const QList<QUrl>& list = event->mimeData()->urls();
	if(!list.empty())
	{
		QString file = list[0].toLocalFile();
		if(file.endsWith(".msh"))
		{
			Lumix::Vec3 position;
			Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(m_world_editor->getEditCamera().scene);

			Lumix::Vec3 origin;
			Lumix::Vec3 dir;
			scene->getRay(m_world_editor->getEditCamera(), event->pos().x(), event->pos().y(), origin, dir);
			Lumix::RayCastModelHit hit = scene->castRay(origin, dir, Lumix::Component::INVALID);
			if (hit.m_is_hit)
			{
				position = hit.m_origin + hit.m_dir * hit.m_t;
			}
			else
			{
				position.set(0, 0, 0);
			}
			ViewWidget::InsertMeshCommand* command = new ViewWidget::InsertMeshCommand(static_cast<ViewWidget&>(*m_view), position, file.toLatin1().data());
			m_world_editor->executeCommand(command);
			m_world_editor->selectEntity(command->getEntity());
		}
	}
}



void SceneView::resizeEvent(QResizeEvent*)
{
	if (m_pipeline)
	{
		m_pipeline->resize(m_view->width(), m_view->height());
	}
}