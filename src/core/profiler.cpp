#include "profiler.h"
#include "core/log.h"


namespace Lumix
{

	LUMIX_CORE_API Profiler g_profiler;


	Profiler::Profiler()
	{
		m_timer = Timer::create();
		m_current_block = NULL;
		m_root_block = NULL;
		m_is_recording = false;
		m_is_record_toggle_request = false;
	}


	Profiler::~Profiler()
	{
		LUMIX_DELETE(m_root_block);
		Timer::destroy(m_timer);
	}


	void Profiler::frame()
	{
		if(m_root_block)
		{
			if (m_is_recording)
			{
				m_frame_listeners.invoke();
				ASSERT(!m_current_block);
				m_root_block->frame();
			}
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
				}
				else
				{
					ASSERT(false); // there can be only one root
				}
			}
			else
			{
				Block* root = LUMIX_NEW(Block);
				root->m_parent = NULL;
				root->m_next = NULL;
				root->m_first_child = NULL;
				root->m_name = name;
				root->m_function = function;
				m_root_block = m_current_block = root;
			}
		}
		else
		{
			Block* child = m_current_block->m_first_child;
			while (child && child->m_name != name && child->m_function != function)
			{
				child = child->m_next;
			}
			if (!child)
			{
				child = LUMIX_NEW(Block);
				child->m_parent = m_current_block;
				child->m_first_child = NULL;
				child->m_name = name;
				child->m_function = function;
				child->m_next = m_current_block->m_first_child;
				m_current_block->m_first_child = child;
			}

			m_current_block = child;
		}
		Block::Hit& hit = m_current_block->m_hits.pushEmpty();
		hit.m_start = m_timer->getTimeSinceStart();
		hit.m_length = 0;
		g_log_info.log("profiler") << "begin block " << m_current_block->m_name << " " << m_current_block->m_hits.back().m_start;
	}

	void Profiler::endBlock()
	{
		if (!m_is_recording)
		{
			return;
		}
		ASSERT(m_current_block);
		float now = m_timer->getTimeSinceStart();
		m_current_block->m_hits.back().m_length = 1000.0f * (now - m_current_block->m_hits.back().m_start);
		g_log_info.log("profiler") << "begin block " << m_current_block->m_name << " " << m_current_block->m_hits.back().m_length << " now = " << now;
		m_current_block = m_current_block->m_parent;
	}


	float Profiler::Block::getLength()
	{
		float ret = 0;
		for (int i = 0, c = m_hits.size(); i < c; ++i)
		{
			ret += m_hits[i].m_length;
		}
		return ret;
	}


	void Profiler::Block::frame()
	{
		m_hits.clear();
		if (m_first_child)
		{
			m_first_child->frame();
		}
		if(m_next)
		{
			m_next->frame();
		}
	}


	Profiler::Block::~Block()
	{
		while (m_first_child)
		{
			Block* child = m_first_child->m_next;
			LUMIX_DELETE(m_first_child);
			m_first_child = child;
		}
	}


} // namespace Lumix
