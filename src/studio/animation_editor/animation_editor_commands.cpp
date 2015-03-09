#include "animation_editor_commands.h"
#include "animator.h"


CreateAnimatorNodeCommand::CreateAnimatorNodeCommand(Type type, Animator* animator, int parent_uid, const QPoint& position)
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
	switch (m_type)
	{
		case ANIMATION:
			node->setContent(new AnimationNodeContent(node));
			node->setName("new animation");
			break;
		case STATE_MACHINE:
			node->setContent(new StateMachineNodeContent(node));
			node->setName("new state machine");
			break;
	}
	node->setPosition(m_position);
	m_node_uid = node->getUID();
}


void CreateAnimatorNodeCommand::undo()
{
	Q_ASSERT(false); /// TODO
	/*AnimatorNode* parent_node = m_animator->getNode(m_parent_uid);
	AnimatorNode* node = m_animator->getNode(m_node_uid);
	parent_node->removeChild(node);*/
}


DestroyAnimatorNodeCommand::DestroyAnimatorNodeCommand(Animator* animator, int uid)
{
	m_animator = animator;
	m_uid = uid;
}


void DestroyAnimatorNodeCommand::undo()
{
	Q_ASSERT(false); /// TODO
}


void DestroyAnimatorNodeCommand::redo()
{
	m_animator->destroyNode(m_uid);
}
