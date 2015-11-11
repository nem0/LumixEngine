#include "profiler.h"
#include "core/log.h"
#include "core/timer.h"


namespace Lumix
{

	LUMIX_ENGINE_API Profiler g_profiler;


	Profiler::Profiler()
		: m_frame_listeners(m_allocator)
	{
		m_timer = Timer::create(m_allocator);
		m_current_block = nullptr;
		m_root_block = nullptr;
		m_is_recording = false;
		m_is_record_toggle_request = false;
	}


	Profiler::~Profiler()
	{
		LUMIX_DELETE(m_allocator, m_root_block);
		Timer::destroy(m_timer);
	}


	void Profiler::frame()
	{
		if(m_root_block)
		{
			if (m_is_recording)
			{
				m_frame_listeners.invoke();
				m_root_block->frame();
				auto* block = m_current_block;
				float t = m_timer->getTimeSinceStart();
				while (block)
				{
					auto& hit = block->m_hits.pushEmpty();
					hit.m_start = t;
					hit.m_length = 0;
					block = block->m_parent;
				}
			}
		}
	}


	void Profiler::toggleRecording()
	{
		m_is_record_toggle_request = true;
	}


	void Profiler::checkRecording()
	{
		ASSERT(!m_current_block);
		if (m_is_record_toggle_request)
		{
			m_is_recording = !m_is_recording;
			m_is_record_toggle_request = false;
		}
	}

	
	void Profiler::beginBlock(const char* name)
	{
		if (!m_is_recording)
		{
			return;
		}
		if (!m_current_block)
		{
			Block* LUMIX_RESTRICT root = m_root_block;
			while (root && root->m_name != name)
			{
				root = root->m_next;
			}
			if (root)
			{
				m_current_block = root;
			}
			else
			{
				Block* root = LUMIX_NEW(m_allocator, Block)(*this);
				root->m_parent = nullptr;
				root->m_next = m_root_block;
				root->m_first_child = nullptr;
				root->m_name = name;
				m_root_block = m_current_block = root;
			}
		}
		else
		{
			Block* LUMIX_RESTRICT child = m_current_block->m_first_child;
			while (child && child->m_name != name)
			{
				child = child->m_next;
			}
			if (!child)
			{
				child = LUMIX_NEW(m_allocator, Block)(*this);
				child->m_parent = m_current_block;
				child->m_first_child = nullptr;
				child->m_name = name;
				child->m_next = m_current_block->m_first_child;
				m_current_block->m_first_child = child;
			}

			m_current_block = child;
		}
		Block::Hit& hit = m_current_block->m_hits.pushEmpty();
		hit.m_start = m_timer->getTimeSinceStart();
		hit.m_length = 0;
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
			LUMIX_DELETE(m_profiler.m_allocator, m_first_child);
			m_first_child = child;
		}
	}


} // namespace Lumix
