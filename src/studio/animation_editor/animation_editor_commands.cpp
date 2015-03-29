#include "animation_editor_commands.h"
#include "animator.h"
#include "core/crc32.h"


static const uint32_t ANIMATION_HASH = crc32("animation");
static const uint32_t STATE_MACHINE_HASH = crc32("state_machine");


DestroyAnimationInputCommand::DestroyAnimationInputCommand(Animator* animator, int input_index)
{
	m_animator = animator;
	m_input_index = input_index;
}


void DestroyAnimationInputCommand::redo()
{
	auto* model = m_animator->getInputModel();
	m_data.clear();
	for (int i = 0; i < model->columnCount(); ++i)
	{
		m_data.push_back(model->data(model->index(m_input_index, i)));
	}
	m_animator->destroyInput(m_input_index);
}


void DestroyAnimationInputCommand::undo()
{
	m_animator->createInput();
	auto* model = m_animator->getInputModel();
	for (int i = 0; i < model->columnCount(); ++i)
	{
		model->setData(model->index(m_input_index, i), m_data[i]);
	}
}

CreateAnimationInputCommand::CreateAnimationInputCommand(Animator* animator)
{
	m_animator = animator;
}


void CreateAnimationInputCommand::undo()
{
	m_animator->destroyInput(m_input_index);
}


void CreateAnimationInputCommand::redo()
{
	m_input_index = m_animator->getInputCount();
	m_animator->createInput();
}


CreateAnimatorNodeCommand::CreateAnimatorNodeCommand(uint32_t type, Animator* animator, int parent_uid, const QPoint& position)
{
	m_animator = animator;
	m_parent_uid = parent_uid;
	m_position = position;
	m_type = type;
}


void CreateAnimatorNodeCommand::redo()
{
	AnimatorNode* parent_node = m_animator->getNode(m_parent_uid);
	AnimatorNode* node = m_animator->createNode(parent_node);

	if (m_type == ANIMATION_HASH)
	{
		node->setContent(new AnimationNodeContent(node));
		node->setName("new animation");
	}
	else if (m_type == STATE_MACHINE_HASH)
	{
		node->setContent(new StateMachineNodeContent(node));
		node->setName("new state machine");
	}
	node->setPosition(m_position);
	m_node_uid = node->getUID();
}


void CreateAnimatorNodeCommand::undo()
{
	m_animator->destroyNode(m_node_uid);
}


DestroyAnimatorNodeCommand::DestroyAnimatorNodeCommand(Animator* animator, int uid)
	: m_blob(animator->getAllocator())
{
	m_animator = animator;
	m_uid = uid;
}


void DestroyAnimatorNodeCommand::undo()
{
	AnimatorNode* parent_node = m_animator->getNode(m_parent_uid);
	AnimatorNode* node = m_animator->createNode(parent_node);
	if (m_node_content_type == ANIMATION_HASH)
	{
		node->setContent(new AnimationNodeContent(node));
	}
	else if (m_node_content_type == STATE_MACHINE_HASH)
	{
		node->setContent(new StateMachineNodeContent(node));
	}
	Lumix::InputBlob blob(m_blob);
	node->deserialize(m_animator->getEditor(), blob);
}


void DestroyAnimatorNodeCommand::redo()
{
	AnimatorNode* node = m_animator->getNode(m_uid);
	AnimatorNode* parent_node = node->getParent();
	m_parent_uid = parent_node ? parent_node->getUID() : -1;
	m_animator->destroyNode(m_uid);
	m_node_content_type = node->getContent()->getType();
	node->serialize(m_blob);
}
