#pragma once


#include "core/lumix.h"
#include "core/delegate_list.h"
#include "core/string.h"
#include "core/timer.h"


namespace Lumix
{


	class LUMIX_CORE_API Profiler
	{
		public: 
			class Block;

		public:
			Profiler();
			~Profiler();

			void frame();
			void toggleRecording();
			bool isRecording() const { return m_is_recording; }
			DelegateList<void ()>& getFrameListeners() { return m_frame_listeners; }
			Block* getRootBlock() const { return m_root_block; }

			void beginBlock(const char* name);
			void endBlock();
			

		private:
			DefaultAllocator m_allocator;
			bool m_is_recording;
			Block* m_current_block;
			Block* m_root_block;
			Timer* m_timer;
			bool m_is_record_toggle_request;
			DelegateList<void ()> m_frame_listeners;
	};

	class LUMIX_CORE_API Profiler::Block
	{
		public:
			class Hit
			{
				public:
					float m_length;
					float m_start;
			};
		
		public:
			Block(Profiler& profiler)
				: m_profiler(profiler)
				, m_hits(profiler.m_allocator)
			{ }

			~Block();
			void frame();
			float getLength();
			int getHitCount() const { return m_hits.size(); }

		public:
			Block* m_parent;
			Block* m_next;
			Block* m_first_child;
			const char* m_name;
			Profiler& m_profiler;
			Array<Hit> m_hits;
	};


	extern LUMIX_CORE_API Profiler g_profiler;


	class ProfileScope
	{
		public:
			ProfileScope(const char* name)
			{
				g_profiler.beginBlock(name);
			}

			~ProfileScope()
			{
				g_profiler.endBlock();
			}
	};


#define BEGIN_PROFILE_BLOCK(name) Lumix::g_profiler.beginBlock(name)
#define END_PROFILE_BLOCK() Lumix::g_profiler.endBlock()
#define PROFILE_FUNCTION() Lumix::ProfileScope profile_scope(__FUNCTION__);
#define PROFILE_BLOCK(name) Lumix::ProfileScope profile_scope(name);

} // namespace Lumix
