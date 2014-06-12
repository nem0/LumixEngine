#include "profiler.h"


namespace Lux
{

	LUX_CORE_API Profiler g_profiler;


	Profiler::Profiler()
	{
		m_timer = Timer::create();
		m_frame_uid = 0;
		m_current_block = NULL;
		m_root_block = NULL;
		m_is_recording = false;
		m_is_record_toggle_request = false;
	}


	Profiler::~Profiler()
	{
		LUX_DELETE(m_root_block);
		Timer::destroy(m_timer);
	}


	void Profiler::frame()
	{
		if (m_is_recording)
		{
			m_frame_listeners.invoke(m_frame_uid);
			++m_frame_uid;
		}
		if (m_is_record_toggle_request)
		{
			m_is_recording = !m_is_recording;
			m_is_record_toggle_request = false;
		}
	}


	void Profiler::toggleRecording()
	{
		m_is_record_toggle_request = true;
	}

	
	void Profiler::beginBlock(const char* name, const char* function)
	{
		if (!m_is_recording)
		{
			return;
		}
		if (!m_current_block)
		{
			if (m_root_block)
			{
				if (m_root_block->m_name == name && m_root_block->m_function == function)
				{
					m_current_block = m_root_block;
					if (m_frame_uid != m_root_block->m_frames[m_frame_uid % Block::MAX_FRAMES].m_index)
					{
						m_root_block->m_frames[m_frame_uid % Block::MAX_FRAMES].m_index = m_frame_uid;
						m_root_block->m_frame_index = m_frame_uid;
						m_root_block->m_frames[m_frame_uid % Block::MAX_FRAMES].m_length = 0;
					}
					m_root_block->m_frames[m_frame_uid % Block::MAX_FRAMES].m_start = m_timer->getTimeSinceStart();
				}
				else
				{
					ASSERT(false); // there can be only one root
				}
				return;
			}
			else
			{
				Block* root = LUX_NEW(Block);
				root->m_parent = NULL;
				root->m_next = NULL;
				root->m_first_child = root->m_last_child = NULL;
				root->m_name = name;
				root->m_function = function;
				root->m_frame_index = m_frame_uid;
				m_root_block = m_current_block = root;
				m_root_block->m_frames[0].m_index = 0;
				m_root_block->m_frames[0].m_length = 0;
				m_root_block->m_frames[0].m_start = m_timer->getTimeSinceStart();
				return;
			}
		}
		if (m_current_block)
		{
			Block* child = m_current_block->m_first_child;
			while (child && child->m_name != name && child->m_function != function)
			{
				child = child->m_next;
			}
			if (!child)
			{
				child = LUX_NEW(Block);
				if(m_current_block->m_last_child)
				{
					m_current_block->m_last_child->m_next = child;
				}
				if(!m_current_block->m_first_child)
				{
					m_current_block->m_first_child = child;
				}
				m_current_block->m_last_child = child;
				child->m_parent = m_current_block;
				child->m_next = NULL;
				child->m_first_child = child->m_last_child = NULL;
				child->m_name = name;
				child->m_function = function;
				child->m_frame_index = m_frame_uid;
			}
			if (m_frame_uid != child->m_frames[m_frame_uid % Block::MAX_FRAMES].m_index)
			{
				child->m_frames[m_frame_uid % Block::MAX_FRAMES].m_index = m_frame_uid;
				child->m_frames[m_frame_uid % Block::MAX_FRAMES].m_length = 0;
			}
			child->m_frames[m_frame_uid % Block::MAX_FRAMES].m_start = m_timer->getTimeSinceStart();

			m_current_block = child;
		}
	}

	void Profiler::endBlock()
	{
		if (!m_is_recording)
		{
			return;
		}
		ASSERT(m_current_block);
		int idx = m_frame_uid % Block::MAX_FRAMES;
		m_current_block->m_frames[idx].m_length += 1000.0f * (m_timer->getTimeSinceStart() - m_current_block->m_frames[idx].m_start);
		m_current_block->m_frame_index = m_frame_uid;
		m_current_block = m_current_block->m_parent;
	}


	Profiler::Block::~Block()
	{
		while (m_first_child)
		{
			Block* child = m_first_child->m_next;
			LUX_DELETE(m_first_child);
			m_first_child = child;
		}
	}


} // namespace Lux
