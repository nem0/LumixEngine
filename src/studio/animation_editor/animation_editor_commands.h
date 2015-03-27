#pragma once


#include <qpoint.h>
#include <qundostack.h>
#include <cstdint>
#include "animator.h"
#include "core/blob.h"


class CreateAnimatorNodeCommand : public QUndoCommand
{
	public:

	public:
		CreateAnimatorNodeCommand(uint32_t type, Animator* animator, int parent_uid, const QPoint& position);

		virtual void undo() override;
		virtual void redo() override;

	private:
		Animator* m_animator;
		int m_parent_uid;
		int m_node_uid;
		QPoint m_position;
		uint32_t m_type;
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
		int m_parent_uid;
		uint32_t m_node_content_type;
		Lumix::OutputBlob m_blob;
};