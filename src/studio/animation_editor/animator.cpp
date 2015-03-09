#include "animator.h"
#include "animation_editor.h"
#include "animation_editor_commands.h"
#include "core/log.h"
#include <qfile.h>
#include <qmenu.h>
#include <qmessagebox.h>
#include <qprocess.h>
#include <qtextstream.h>


void AnimatorNode::paintContainer(QPainter& painter)
{
	m_content->paintContainer(painter);
}


void AnimatorNode::paintContent(QPainter& painter)
{
	m_content->paint(painter);
}


AnimatorNode* AnimatorNode::getContentNodeAt(int x, int y)
{
	return m_content->getNodeAt(x, y);
}


void AnimatorNode::showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos)
{
	AnimatorNode* node = getContentNodeAt(pos.x(), pos.y());
	if (node)
	{
		node->m_content->showContextMenu(editor, widget, pos);
	}
	else
	{
		m_content->showContextMenu(editor, widget, pos);
	}
}


StateMachineNodeContent::~StateMachineNodeContent()
{
	for (int i = 0; i < m_children.size(); ++i)
	{
		delete m_children[i];
	}
}


QString StateMachineNodeContent::generateCode()
{
	QString code = "";
	QString members = "";
	int default_idx = 0;
	for (int i = 0; i < m_children.size(); ++i)
	{
		code += m_children[i]->getContent()->generateCode();
		members += QString("		Node%1 m_child%2;\n").arg(m_children[i]->getUID()).arg(i + 1);
		if (m_children[i]->getUID() == m_default_uid)
		{
			default_idx = i;
		}
	}

	code += QString(
		"class Node%1 : public NodeBase {\n"
		"	public:\n"
		"		Node%1() { m_current_node = &m_child%2; }\n"
		"		void getPose(Pose& pose, Context& context) override { m_current_node->getPose(pose, context); }\n"
		"		void update(float time_delta) override { m_current_node->update(time_delta); }\n"
		"	private:\n"
		"%3"
		"		NodeBase* m_current_node;\n"
		"};\n\n"
	).arg(getNode()->getUID()).arg(default_idx + 1).arg(members);

	return code;
}


bool StateMachineNodeContent::hitTest(int x, int y)
{
	int this_x = getNode()->getPosition().x();
	int this_y = getNode()->getPosition().y();
	return x >= this_x && x < this_x + 100 && y > this_y && y < this_y + 20;
}


AnimatorNode* StateMachineNodeContent::getNodeAt(int x, int y)
{
	for (int i = 0; i < m_children.size(); ++i)
	{
		if (m_children[i]->getContent()->hitTest(x, y))
		{
			return m_children[i];
		}
	}
	return getNode();
}


void StateMachineNodeContent::paintContainer(QPainter& painter)
{
	QRect rect(m_node->getPosition(), QSize(100, 20));
	QLinearGradient gradient(0, 0, 0, 100);
	gradient.setColorAt(0.0, QColor(100, 100, 100));
	gradient.setColorAt(1.0, QColor(64, 64, 64));
	gradient.setSpread(QGradient::Spread::ReflectSpread);
	painter.fillRect(rect, gradient);

	painter.setPen(QColor(255, 255, 255));
	painter.setFont(QFont("Arial", 10));
	painter.drawText(rect, Qt::AlignCenter, m_node->getName());
}


void StateMachineNodeContent::paint(QPainter& painter)
{
	for (int i = 0; i < m_children.size(); ++i)
	{
		m_children[i]->paintContainer(painter);
	}
}


void StateMachineNodeContent::removeChild(AnimatorNode* node)
{
	for (int i = 0; i < m_children.size(); ++i)
	{
		if (m_children[i] == node)
		{
			m_children.removeAt(i);
			break;
		}
	}
}


void StateMachineNodeContent::showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos)
{
	QMenu* menu = new QMenu();
	QAction* add_animation_action = menu->addAction("Add animation");
	QAction* add_state_machine_action = menu->addAction("Add state machine");
	QAction* remove_action = menu->addAction("Remove");
	QAction* selected_action = menu->exec(widget->mapToGlobal(pos));
	if (selected_action == remove_action)
	{
		editor.executeCommand(new DestroyAnimatorNodeCommand(editor.getAnimator(), getNode()->getUID()));
	}
	else if (selected_action == add_animation_action)
	{
		editor.executeCommand(new CreateAnimatorNodeCommand(CreateAnimatorNodeCommand::ANIMATION, editor.getAnimator(), getNode()->getUID(), pos));
	}
	else if (selected_action == add_state_machine_action)
	{
		editor.executeCommand(new CreateAnimatorNodeCommand(CreateAnimatorNodeCommand::STATE_MACHINE, editor.getAnimator(), getNode()->getUID(), pos));
	}

}


QString AnimationNodeContent::generateCode()
{
	return QString(
		"class Node%1 : public NodeBase {\n"
		"	public:\n"
		"		Node%1() { m_time = 0; m_animation = (Animation*)g_animation_manager->load(Path(\"%2\")); }\n"
		"		void getPose(Pose& pose, Context& context) override { m_animation->getPose(m_time, pose, context.m_model); }\n"
		"		void update(float time_delta) override { m_time += time_delta; }\n"
		"	private:\n"
		"		Animation* m_animation;\n"
		"		float m_time;\n"
		"};\n\n"
	).arg(getNode()->getUID()).arg(m_animation_path);
}


void AnimationNodeContent::showContextMenu(AnimationEditor& editor, QWidget* widget, const QPoint& pos)
{
	QMenu* menu = new QMenu();
	QAction* remove_action = menu->addAction("Remove");
	QAction* selected_action = menu->exec(widget->mapToGlobal(pos));
	if (selected_action == remove_action)
	{
		editor.executeCommand(new DestroyAnimatorNodeCommand(editor.getAnimator(), getNode()->getUID()));
	}
}


AnimatorNode* AnimationNodeContent::getNodeAt(int x, int y)
{
	if (hitTest(x, y))
		return getNode();
	return NULL;
}


bool AnimationNodeContent::hitTest(int x, int y)
{
	int this_x = getNode()->getPosition().x();
	int this_y = getNode()->getPosition().y();
	return x >= this_x && x < this_x + 100 && y > this_y && y < this_y + 20;
}


void AnimationNodeContent::paint(QPainter& painter)
{
	QRect rect(m_node->getPosition(), QSize(100, 20));
	QLinearGradient gradient(0, 0, 0, 100);
	gradient.setColorAt(0.0, QColor(100, 100, 100));
	gradient.setColorAt(1.0, QColor(64, 64, 64));
	gradient.setSpread(QGradient::Spread::ReflectSpread);
	painter.fillRect(rect, gradient);

	painter.setPen(QColor(255, 255, 255));
	painter.setFont(QFont("Arial", 10));
	painter.drawText(rect, Qt::AlignCenter, m_node->getName());
}


void AnimationNodeContent::paintContainer(QPainter& painter)
{
	paint(painter);
}


Animator::Animator()
{
	m_last_uid = 0;
	m_root = createNode(NULL);
	auto sm = new StateMachineNodeContent(m_root);
	m_root->setContent(sm);
	m_root->setName("Root");
}


void Animator::destroyNode(int uid)
{
	for (int i = 0; i < m_nodes.size(); ++i)
	{
		if (m_nodes[i]->getUID() == uid)
		{
			AnimatorNode* parent = m_nodes[i]->getParent();
			if (parent)
			{
				parent->getContent()->removeChild(m_nodes[i]);
			}
			delete m_nodes[i];
			m_nodes.removeAt(i);
			break;
		}
	}
}


AnimatorNode* Animator::createNode(AnimatorNode* parent)
{
	AnimatorNode* node = new AnimatorNode(++m_last_uid, parent);
	m_nodes.push_back(node);
	if (parent)
	{
		parent->getContent()->addChild(node);
	}
	
	return node;
}


bool Animator::compile(const QString& base_path)
{
	QString code(
		"#include \"animation/animation.h\"\n"
		"#include \"graphics/model.h\"\n"
		"#include \"graphics/pose.h\"\n"
		"using namespace Lumix;"
		"struct Context {\n"
		"	Model& m_model;\n"
		"};\n\n"
		"AnimationManager* g_animation_manager;\n\n"
		"class NodeBase {\n"
		"	public:\n"
		"		virtual void getPose(Pose&, Context&) = 0;\n"
		"		virtual void update(float time_delta) = 0;\n"
		"};\n\n"
	);
	code += m_root->getContent()->generateCode();

	QFile file(QString("%1/tmp/anim.cpp").arg(base_path));
	file.open(QIODevice::WriteOnly | QIODevice::Text);
	QTextStream out(&file);
	out << code;
	file.close();

	QProcess* process = new QProcess;
	QStringList list;
	list << "/C";
	list << QString("%1/tmp/compile.bat %1/tmp/anim.cpp").arg(base_path);
	process->start("cmd.exe", list);
	m_library.unload();
	process->connect(process, (void (QProcess::*)(int))&QProcess::finished, [process, this, base_path](int exit_code){
		m_library.setFileName(QString("%1/tmp/anim").arg(base_path));
		if (exit_code != 0)
		{
			QString compile_message = process->readAll();
			Lumix::g_log_error.log("animation") << compile_message.toLatin1().data();
		}
		else if (!m_library.load())
		{
			Lumix::g_log_error.log("animation") << "Could not load " << m_library.fileName().toLatin1().data();
		}
		process->deleteLater();
	});
	return true;
}


AnimatorNode* Animator::getNode(int uid)
{
	for (int i = 0; i < m_nodes.size(); ++i)
	{
		if (m_nodes[i]->getUID() == uid)
		{
			return m_nodes[i];
		}
	}
	return NULL;
}
