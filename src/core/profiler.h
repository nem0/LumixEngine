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

			void beginBlock(const char* name, const char* function);
			void endBlock();
			

		private:
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
			~Block();
			void frame();
			float getLength();

		public:
			Block* m_parent;
			Block* m_next;
			Block* m_first_child;
			const char* m_name;
			const char* m_function;
			Array<Hit> m_hits;
	};


	extern LUMIX_CORE_API Profiler g_profiler;


	class ProfileScope
	{
		public:
			ProfileScope(const char* name, const char* function)
			{
				g_profiler.beginBlock(name, function);
			}

			~ProfileScope()
			{
				g_profiler.endBlock();
			}
	};


#define BEGIN_PROFILE_BLOCK(name) Lumix::g_profiler.beginBlock(name, __FUNCSIG__)
#define END_PROFILE_BLOCK() Lumix::g_profiler.endBlock()
#define PROFILE_FUNCTION() Lumix::ProfileScope profile_scope(__FUNCTION__, __FUNCSIG__);
#define PROFILE_BLOCK(name) Lumix::ProfileScope profile_scope(name, __FUNCSIG__);

} // namespace Lumix
