#include "animator.h"
#include "animation/animation.h"
#include "animation_editor.h"
#include "animation_editor_commands.h"
#include "core/blob.h"
#include "core/log.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/render_scene.h"
#include "property_view.h"
#include "property_view/property_editor.h"
#include "scripts/scriptcompiler.h"
#include <qcombobox.h>
#include <qfile.h>
#include <qmenu.h>
#include <qmessagebox.h>
#include <qmetaobject.h>
#include <qmetatype.h>
#include <qprocess.h>
#include <qtextstream.h>


static const QString MODULE_NAME = "amimation";
static const QString CPP_FILE_PATH = "tmp/animation.cpp";
static const QColor EDGE_COLOR(255, 255, 255);


QWidget* AnimatorInputTypeDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	if (index.column() == 1)
	{
		auto m = AnimatorInputType::staticMetaObject.enumerator(0);
		QStringList values;
		for (int i = 0; i < m.keyCount(); ++i)
		{
			values << m.key(i);
		}

		QComboBox* comboBox = new QComboBox(parent);
		comboBox->addItems(values);
		comboBox->setCurrentIndex(values.indexOf(index.data().toString()));
		return comboBox;
	}
	else
	{
		return QItemDelegate::createEditor(parent, option, index);
	}
}


class AnimatorInputModel : public QAbstractItemModel
{
	public:
		AnimatorInputModel(Animator& animator) : m_animator(animator) {}

		class Input
		{
			public:
				Input(const QString& name) : m_name(name), m_value(0.0f), m_type(AnimatorInputType::NUMBER) {}

				QString m_name;
				AnimatorInputType::Type m_type;
				QVariant m_value;
		};

		enum Columns
		{
			NAME,
			TYPE,
			VALUE,
			COLUMN_COUNT
		};

	public:
		virtual QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
		virtual QModelIndex parent(const QModelIndex &child) const override;
		virtual int rowCount(const QModelIndex &parent = QModelIndex()) const override;
		virtual int columnCount(const QModelIndex &parent = QModelIndex()) const override;
		virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
		virtual bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
		virtual Qt::ItemFlags flags(const QModelIndex &index) const override;
		virtual QVariant AnimatorInputModel::headerData(int section, Qt::Orientation orientation, int role) const override;
		const QList<Input>& getInputs() const { return m_inputs; }
		QList<Input>& getInputs() { return m_inputs; }

		void createInput();

	private:
		Animator& m_animator;
		QList<Input> m_inputs;
};


QVariant AnimatorInputModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
	{
		switch (section)
		{
			case NAME: return "Name";	break;
			case TYPE: return "Type";	break;
			case VALUE: return "Value";	break;
			default:
				Q_ASSERT(false);
				break;
		}
	}

	return QAbstractItemModel::headerData(section, orientation, role);
}


Qt::ItemFlags AnimatorInputModel::flags(const QModelIndex &index) const
{
	return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
}


bool AnimatorInputModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	if (role == Qt::EditRole)
	{
		switch (index.column())
		{
			case NAME:
				m_inputs[index.row()].m_name = value.toString();
				emit dataChanged(index, index);
				return true;
			case TYPE:
				m_inputs[index.row()].m_type = (AnimatorInputType::Type)AnimatorInputType::staticMetaObject.enumerator(0).keyToValue(value.toString().toLatin1().data());
				emit dataChanged(index, index);
				return true;
			case VALUE:
				m_inputs[index.row()].m_value = value;
				switch (m_inputs[index.row()].m_type)
				{
					case AnimatorInputType::NUMBER:
						m_animator.setInput(crc32(m_inputs[index.row()].m_name.toLatin1().data()), value.toFloat());
						break;
					default:
						Q_ASSERT(false);
						break;
				}

				emit dataChanged(index, index);
				return true;
			default:
				return false;
		}
	}
	return false;
}


void AnimatorInputModel::createInput()
{
	int row = m_inputs.size();
	beginInsertRows(QModelIndex(), row, row);
	m_inputs.push_back(Input("input"));
	endInsertRows();
}


QModelIndex AnimatorInputModel::index(int row, int column, const QModelIndex&) const
{
	return createIndex(row, column);
}


QModelIndex AnimatorInputModel::parent(const QModelIndex&) const
{
	return QModelIndex();
}


int AnimatorInputModel::rowCount(const QModelIndex&) const
{
	return m_inputs.size();
}


int AnimatorInputModel::columnCount(const QModelIndex&) const
{
	return COLUMN_COUNT;
}


QVariant AnimatorInputModel::data(const QModelIndex& index, int role) const
{
	if (role == Qt::DisplayRole)
	{
		switch (index.column())
		{
			case NAME:
				return m_inputs[index.row()].m_name;
			case TYPE:
				return AnimatorInputType::staticMetaObject.enumerator(0).valueToKey(m_inputs[index.row()].m_type);
			case VALUE:
				return m_inputs[index.row()].m_value;
			default:
				Q_ASSERT(false);
				break;
		}
	}
	return QVariant();
}


void AnimatorNode::serialize(Lumix::OutputBlob& blob)
{
	blob.write(m_uid);
	blob.write(m_position);
	blob.writeString(m_name.toLatin1().data());
	blob.write(m_content->getType());
	m_content->serialize(blob);
}


void AnimatorNode::deserialize(AnimationEditor& editor, Lumix::InputBlob& blob)
{
	char name[256];
	blob.read(m_uid);
	blob.read(m_position);
	blob.readString(name, sizeof(name));
	m_name = name;
	delete m_content;
	uint32_t content_type;
	blob.read(content_type);
	m_content = editor.createContent(*this, content_type);
	m_content->deserialize(editor, blob);
}


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


QPoint AnimatorNode::getCenter() const
{
	return m_position + QPoint(50, 10);
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


static QPointF normalize(QPoint point)
{
	float l = QPoint::dotProduct(point, point);
	return QPointF(point) / sqrtf(l);
}


QString StateMachineNodeContent::generateConditionCode() const
{
	QString ret;
	for (const auto* edge : m_edges)
	{
		ret += QString("	bool condition%1() { return %2; }\n").arg(edge->getUID()).arg(edge->getCondition());
	}
	for (const auto* child : m_children)
	{
		ret += child->getContent()->generateConditionCode();
	}
	return ret;
}


void StateMachineNodeContent::drawEdges(QPainter& painter)
{
	painter.setPen(EDGE_COLOR);
	for (int i = 0; i < m_edges.size(); ++i)
	{
		QPoint from = m_edges[i]->getFromPosition();
		QPoint to = m_edges[i]->getToPosition();
		QPoint center = (from + to) * 0.5f;
		QPointF dir = normalize(to - from);
		QPointF ortho(dir.y(), -dir.x());
		painter.drawLine(from, to);
		painter.drawLine(center - dir * 5 + ortho * 5, center);
		painter.drawLine(center - dir * 5 - ortho * 5, center);
	}
}


void StateMachineNodeContent::createEdge(Animator& animator, AnimatorNode* from, AnimatorNode* to)
{
	if (from == to)
	{
		return;
	}
	for (int i = 0; i < m_edges.size(); ++i)
	{
		if (m_edges[i]->getFrom() == from && m_edges[i]->getTo() == to)
		{
			return;
		}
	}
	m_edges.push_back(animator.createEdge(from, to));
	from->edgeAdded(m_edges.last());
}


void StateMachineNodeContent::serialize(Lumix::OutputBlob& blob)
{
	blob.write(m_default_uid);
	blob.write((int)m_children.size());
	for (int i = 0; i < m_children.size(); ++i)
	{
		m_children[i]->serialize(blob);
	}
	blob.write((int)m_edges.size());
	for (int i = 0; i < m_edges.size(); ++i)
	{
		blob.write(m_edges[i]->getFrom()->getUID());
		blob.write(m_edges[i]->getTo()->getUID());
		blob.writeString(m_edges[i]->getCondition().toLatin1().data());
	}
}


void StateMachineNodeContent::deserialize(AnimationEditor& editor, Lumix::InputBlob& blob)
{
	blob.read(m_default_uid);
	int children_count;
	blob.read(children_count);
	for (int i = 0; i < children_count; ++i)
	{
		AnimatorNode* node = editor.getAnimator()->createNode(getNode());
		node->deserialize(editor, blob);
	}
	int edge_count;
	blob.read(edge_count);
	for (int i = 0; i < edge_count; ++i)
	{
		uint32_t uid_from, uid_to;
		blob.read(uid_from);
		blob.read(uid_to);
		char condition[256];
		blob.readString(condition, sizeof(condition));
		AnimatorEdge* edge = editor.getAnimator()->createEdge(editor.getAnimator()->getNode(uid_from), editor.getAnimator()->getNode(uid_to));
		edge->setCondition(condition);
		edge->getFrom()->edgeAdded(edge);
		m_edges.push_back(edge);
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
	bool default_found = false;
	for (int i = 0; i < m_children.size(); ++i)
	{
		default_found = default_found || m_children[i]->getUID() == m_default_uid;
		code += m_children[i]->getContent()->generateCode();
		members += QString(
			"		Node%1 m_child%1;\n"
			"		void checkNode%1Condition(Context& context) {\n"
		).arg(m_children[i]->getUID());
		for (const auto* edge : m_edges)
		{
			if (edge->getFrom() == m_children[i])
			{
				members += QString("			if(context.m_input.condition%1()) { m_current_node = &m_edge%1; m_edge%1.enter(); m_check_condition = &Node%2::checkEdge%1End; return; }\n")
					.arg(edge->getUID())
					.arg(getNode()->getUID());
			}
		}
		members += "		}\n";
	}
	QString contructor_edges = "";
	bool first_edge = true;
	for (const auto* edge : m_edges)
	{
		if (first_edge)
		{
			first_edge = false;
			contructor_edges += QString("			: m_edge%1(*this)\n").arg(edge->getUID());
		}
		else
		{
			contructor_edges += QString("			, m_edge%1(*this)\n").arg(edge->getUID());
		}
		members += QString(
			"		struct Edge%1 : public NodeBase {\n"
			"			Edge%1(Node%3& node) { m_edge_duration = %5; m_time = 0; m_from = &node.m_child%4; m_to = &node.m_child%2; }\n"
			"			void enter() { m_time = 0; }\n"
			"			void getPose(Pose& pose, Context& context) override {\n"
			"				DefaultAllocator al;\n"
			"				Pose tmp_pose(al);\n"
			"				tmp_pose.resize(pose.getCount());\n"
			"				m_from->getPose(pose, context);\n"
			"				m_to->getPose(tmp_pose, context);\n"
			"				pose.blend(tmp_pose, m_time / m_edge_duration);\n"
			"			}\n"
			"			void update(float time_delta, Context& context) override { m_time += time_delta; m_from->update(time_delta, context); m_to->update(time_delta, context); }\n"
			"			NodeBase* m_from;\n"
			"			NodeBase* m_to;\n"
			"			float m_time;\n"
			"			float m_edge_duration;"
			"		} m_edge%1;\n"
			"		void checkEdge%1End(Context& context) { if(m_edge%1.m_time > m_edge%1.m_edge_duration) { m_current_node = m_edge%1.m_to; m_check_condition = &Node%3::checkNode%2Condition; } }\n"
		)
		.arg(edge->getUID())
		.arg(edge->getTo()->getUID())
		.arg(getNode()->getUID())
		.arg(edge->getFrom()->getUID())
		.arg(edge->getDuration());
	}
	if (!default_found)
	{
		m_default_uid = m_children[0]->getUID();
	}

	code += QString(
		"class Node%1 : public NodeBase {\n"
		"	public:\n"
		"		typedef void (Node%1::*CheckConditionFunction)(Context&);\n"
		"		Node%1() %3"
		"			{ m_current_node = &m_child%2; m_check_condition = &Node%1::checkNode%2Condition; }\n"
		"		void getPose(Pose& pose, Context& context) override { m_current_node->getPose(pose, context); }\n"
		"		void update(float time_delta, Context& context) override { m_current_node->update(time_delta, context); (this->*m_check_condition)(context); }\n"
		"		bool isReady() const { return "
	)
	.arg(getNode()->getUID())
	.arg(m_default_uid)
	.arg(contructor_edges);

	if (!m_children.empty())
	{
		code += QString(" m_child%1.isReady()").arg(m_children.first()->getUID());
	}
	for (const auto* child : m_children)
	{
		code += QString(" && m_child%1.isReady()").arg(child->getUID());
	}

	code += QString(
	"; }\n"
	"	private:\n"
		"%1"
		"		NodeBase* m_current_node;\n"
		"		CheckConditionFunction m_check_condition;"
		"};\n\n"
	).arg(members);

	return code;
}


bool StateMachineNodeContent::hitTest(int x, int y) const
{
	int this_x = getNode()->getPosition().x();
	int this_y = getNode()->getPosition().y();
	return x >= this_x && x < this_x + 100 && y > this_y && y < this_y + 20;
}


AnimatorEdge* StateMachineNodeContent::getEdgeAt(int x, int y) const
{
	for (auto* edge : m_edges)
	{
		if (edge->hitTest(x, y))
		{
			return edge;
		}
	}
	return NULL;
}


AnimatorNode* StateMachineNodeContent::getNodeAt(int x, int y) const
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
	drawEdges(painter);

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
			for (int j = m_edges.size() - 1; j >= 0; --j)
			{
				if (m_edges[j]->getFrom() == node || m_edges[j]->getTo() == node)
				{
					m_edges[j]->getFrom()->edgeRemoved(m_edges[j]);
					delete m_edges[j];
					m_edges.removeAt(j);
				}
			}
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


uint32_t StateMachineNodeContent::getType() const
{
	return crc32("state_machine");
}


void AnimationNodeContent::serialize(Lumix::OutputBlob& blob)
{
	blob.writeString(m_animation_path.toLatin1().data());
}


void AnimationNodeContent::deserialize(AnimationEditor&, Lumix::InputBlob& blob)
{
	char path[LUMIX_MAX_PATH];
	blob.readString(path, sizeof(path));
	m_animation_path = path;
}


uint32_t AnimationNodeContent::getType() const
{
	return crc32("animation");
}


QString AnimationNodeContent::generateCode()
{
	return QString(
		"class Node%1 : public NodeBase {\n"
		"	public:\n"
		"		Node%1() { m_time = 0; m_animation = (Animation*)g_animation_manager->load(Path(\"%2\")); }\n"
		"		void getPose(Pose& pose, Context& context) override { m_animation->getPose(m_time, pose, *context.m_model); }\n"
		"		void update(float time_delta, Context& context) override { m_time += time_delta; m_time = fmod(m_time, m_animation->getLength()); }\n"
		"		bool isReady() const { return m_animation->isReady(); }\n"
		"	private:\n"
		"		Animation* m_animation;\n"
		"		float m_time;\n"
		"};\n\n"
	).arg(getNode()->getUID()).arg(m_animation_path);
}


void AnimationNodeContent::fillPropertyView(PropertyView& view)
{
	QTreeWidgetItem* item = view.newTopLevelItem();
	PropertyEditor<const char*>::create("name", item, getNode()->getName().toLatin1().data(), [this](const char* value) { getNode()->setName(value); });
	PropertyEditor<Lumix::Path>::create(view, "animation", item, Lumix::Path(m_animation_path.toLatin1().data()), [this](const char* value) { m_animation_path = value; });
	item->setText(0, "Animation node");
	item->treeWidget()->expandToDepth(1);
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


AnimatorNode* AnimationNodeContent::getNodeAt(int x, int y) const
{
	if (hitTest(x, y))
		return getNode();
	return NULL;
}


bool AnimationNodeContent::hitTest(int x, int y) const
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


Animator::Animator(ScriptCompiler& compiler)
	: m_compiler(compiler)
{
	m_update_function = NULL;
	m_last_uid = 0;
	m_root = createNode(NULL);
	auto sm = new StateMachineNodeContent(m_root);
	m_root->setContent(sm);
	m_root->setName("Root");
	m_input_model = new AnimatorInputModel(*this);
}


void Animator::setPath(const QString& path)
{
	QFileInfo info(path);
	m_path = path;
	m_compiler.destroyModule(MODULE_NAME);
	m_compiler.addScript(MODULE_NAME, CPP_FILE_PATH);
	QString output_path = info.path() + "/" + info.baseName();
	m_compiler.setModuleOutputPath(MODULE_NAME, output_path);
	m_library.setFileName(output_path);
}


void Animator::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
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


AnimatorEdge* Animator::createEdge(AnimatorNode* from, AnimatorNode* to)
{
	return new AnimatorEdge(++m_last_uid, from, to);
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


void Animator::serialize(Lumix::OutputBlob& blob)
{
	m_root->serialize(blob);

	const QList<AnimatorInputModel::Input>& inputs = m_input_model->getInputs();
	blob.write((int32_t)inputs.size());
	for (int i = 0; i < inputs.size(); ++i)
	{
		const AnimatorInputModel::Input& input = inputs[i];
		blob.writeString(input.m_name.toLatin1().data());
		blob.write((int32_t)input.m_type);
		switch (input.m_type)
		{
			case AnimatorInputType::STRING:
				blob.writeString(input.m_value.toString().toLatin1().data());
				break;
			case AnimatorInputType::NUMBER:
				blob.write(input.m_value.toFloat());
				break;
			default:
				Q_ASSERT(false);
				break;
		}
	}
}


void Animator::setInput(unsigned int name_hash, float value)
{
	if (m_set_input_function)
	{
		m_set_input_function(m_object, name_hash, &value);
	}
}


void Animator::deserialize(AnimationEditor& editor, Lumix::InputBlob& blob)
{
	m_root->deserialize(editor, blob);

	QList<AnimatorInputModel::Input>& inputs = m_input_model->getInputs();
	int32_t count;
	blob.read(count);
	for (int i = 0; i < count; ++i)
	{
		char str[100];
		blob.readString(str, sizeof(str));
		AnimatorInputModel::Input input(str);
		int32_t type;
		blob.read(type);
		input.m_type = (AnimatorInputType::Type)type;
		switch (type)
		{
			case AnimatorInputType::NUMBER:
				float f;
				blob.read(f);
				input.m_value = f;
				break;
			case AnimatorInputType::STRING:
				blob.readString(str, sizeof(str));
				input.m_value = str;
				break;
			default:
				Q_ASSERT(false);
				break;
		}
		inputs.push_back(input);
	}
}


void Animator::update(float time_delta)
{
	if (m_update_function)
	{
		auto& selected = m_editor->getSelectedEntities();
		if (selected.size() == 1)
		{
			auto renderable = m_editor->getComponent(selected[0], crc32("renderable"));
			if (renderable.isValid())
			{
				Lumix::RenderScene* scene = static_cast<Lumix::RenderScene*>(renderable.scene);
				auto& pose = scene->getPose(renderable);
				auto* model = scene->getRenderableModel(renderable);
				if (model && m_is_ready_function(m_object))
				{
					m_update_function(m_object, *model, pose, time_delta);
				}
			}
		}
	}
}


void Animator::run()
{
	if (m_update_function)
	{
		m_update_function = NULL;
		m_library.unload();
	}
	else
	{
		if (!m_library.isLoaded())
		{
			m_library.load();
		}
		if (!m_library.isLoaded())
		{
			return;
		}
		CreateFunction creator = (CreateFunction)m_library.resolve("create");
		m_update_function = (UpdateFunction)m_library.resolve("update");
		m_is_ready_function = (IsReadyFunction)m_library.resolve("isReady");
		m_set_input_function = (SetInputFunction)m_library.resolve("setInput");
		AnimationManagerSetter setAnimationManager = (AnimationManagerSetter)m_library.resolve("setAnimationManager");
		if (setAnimationManager)
		{
			setAnimationManager((Lumix::AnimationManager*)m_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::ANIMATION));
			m_object = creator();
		}
		else
		{
			m_update_function = NULL;
			m_library.unload();
		}
	}
}


QString Animator::generateInputsCode() const
{
	QString ret = "struct Inputs {\n";
	auto typeToString = [](AnimatorInputType::Type type) -> QString {
		switch (type)
		{
			case AnimatorInputType::STRING:
				return "unsigned int";
				break;
			case AnimatorInputType::NUMBER:
				return "float";
				break;
			default:
				Q_ASSERT(false);
				return "int";
		}
	};

	for (const auto& input : m_input_model->getInputs())
	{
		ret += QString("	") + typeToString(input.m_type) + " " + input.m_name + ";\n";
	}
	ret += "	void setInput(const unsigned int name_hash, void* value) {\n";
	for (const auto& input : m_input_model->getInputs())
	{
		ret += QString("	if(name_hash == %1) %2 = *(%3*)value;\n").arg(crc32(input.m_name.toLatin1().data())).arg(input.m_name).arg(typeToString(input.m_type));
	}
	ret += "	};";
	ret += m_root->getContent()->generateConditionCode();
	ret += "};\n";
	return ret;
}


bool Animator::compile()
{
	m_update_function = NULL;
	m_is_ready_function = NULL;
	m_set_input_function = NULL;
	m_library.unload();
	QString code(
		"#include \"animation/animation.h\"\n"
		"#include \"graphics/model.h\"\n"
		"#include \"graphics/pose.h\"\n"
		"#include <cmath>\n"
		"using namespace Lumix;\n"
	);
	code += generateInputsCode();
	code +=
		"struct Context {\n"
		"	Model* m_model;\n"
		"	Inputs m_input;\n"
		"	void* m_root;\n"
		"};\n\n"
		"AnimationManager* g_animation_manager;\n\n"
		"class NodeBase {\n"
		"	public:\n"
		"		virtual void getPose(Pose&, Context&) = 0;\n"
		"		virtual void update(float time_delta, Context& context) = 0;\n"
		"};\n\n";
		code += m_root->getContent()->generateCode();
	code += QString(
		"extern \"C\" __declspec(dllexport) void setAnimationManager(AnimationManager* mng) {\n"
		"	g_animation_manager = mng;\n"
		"}\n"
		"extern \"C\" __declspec(dllexport) void* create() {\n"
		"	Context* context = new Context;\n"
		"	context->m_root = new Node%1;\n"
		"	return context;\n"
		"}\n"
		"extern \"C\" __declspec(dllexport) bool isReady(void* object) {\n"
		"	Context* context = (Context*)object;\n"
		"	Node%1* node = (Node%1*)context->m_root;\n"
		"	return node->isReady();\n"
		"}\n"
		"extern \"C\" __declspec(dllexport) void setInput(void* object, unsigned int name_hash, void* value) {\n"
		"	Context* context = (Context*)object;\n"
		"	context->m_input.setInput(name_hash, value);\n"
		"}\n"
		"extern \"C\" __declspec(dllexport) void update(void* object, Model& model, Pose& pose, float time_delta) {\n"
		"	Context* context = (Context*)object;\n"
		"	NodeBase* node = (NodeBase*)context->m_root;\n"
		"	node->update(time_delta, *context);\n"
		"	context->m_model = &model;\n"
		"	node->getPose(pose, *context);\n"
		"}").arg(m_root->getUID());

	QFile file(CPP_FILE_PATH);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		return false;
	}
	QTextStream out(&file);
	out << code;
	file.close();

	m_compiler.compileModule(MODULE_NAME);
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


void Animator::createInput()
{
	m_input_model->createInput();
}


QAbstractItemModel* Animator::getInputModel() const
{
	return m_input_model;
}


QPoint AnimatorEdge::getFromPosition() const
{
	QPoint pos = m_from->getPosition() + QPoint(50, 10);
	QPoint dir = m_to->getPosition() - pos;
	dir *= 7 / sqrtf(QPoint::dotProduct(dir, dir));
	return pos + QPoint(dir.y(), -dir.x());
}


QPoint AnimatorEdge::getToPosition() const
{
	QPoint pos = m_to->getPosition() + QPoint(50, 10);
	QPointF dir = 7 * normalize(m_from->getPosition() - pos);
	return pos + QPoint(-dir.y(), dir.x());

}


bool AnimatorEdge::hitTest(int x, int y) const
{
	static const float MAX_DIST = 3;
	QLine line(getFromPosition(), getToPosition());
	QPointF dir = normalize(QPoint(line.dx(), line.dy()));
	QPointF normal(-dir.y(), dir.x());
	float c = -QPointF::dotProduct(normal, line.p1());
	
	float d = c + QPointF::dotProduct(normal, QPoint(x, y));
	if (fabs(d) < MAX_DIST)
	{
		float c = -QPointF::dotProduct(dir, line.p1());
		float d = c + QPointF::dotProduct(dir, QPoint(x, y));
		if (d < 0)
			return false;
		c = -QPointF::dotProduct(-dir, line.p2());
		d = c + QPointF::dotProduct(-dir, QPoint(x, y));
		if (d < 0)
			return false;
		return true;
	}
	return false;
}


void AnimatorEdge::fillPropertyView(PropertyView& view)
{
	QTreeWidgetItem* item = view.newTopLevelItem();
	PropertyEditor<const char*>::create("condition", item, getCondition().toLatin1().data(), [this](const char* value) { setCondition(value); });
	PropertyEditor<float>::create("duration", item, getDuration(), [this](float value) { setDuration(value); });
	item->setText(0, "Edge");
	item->treeWidget()->expandToDepth(1);
}
