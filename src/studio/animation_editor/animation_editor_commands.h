#pragma once


#include <qpoint.h>
#include <qundostack.h>


class Animator;


class CreateAnimatorNodeCommand : public QUndoCommand
{
	public:
		enum Type
		{
			ANIMATION,
			STATE_MACHINE,
		};

	public:
		CreateAnimatorNodeCommand(Type type, Animator* animator, int parent_uid, const QPoint& position);

		virtual void undo() override;
		virtual void redo() override;

	private:
		Animator* m_animator;
		int m_parent_uid;
		int m_node_uid;
		QPoint m_position;
		Type m_type;
};


class DestroyAnimatorNodeCommand : public QUndoCommand
{
	public:
		DestroyAnimatorNodeCommand(Animator* animator, int uid);

		virtual void undo() override;
		virtual void redo() override;

	private:
		Animator* m_animator;
		int m_uid;
};