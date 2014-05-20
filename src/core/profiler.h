#pragma once


#include "core/lux.h"
#include "core/delegate_list.h"
#include "core/string.h"
#include "core/timer.h"


namespace Lux
{


	class LUX_CORE_API Profiler
	{
		public: 
			class Block;

		public:
			Profiler();
			~Profiler();

			void frame();
			void toggleRecording();
			bool isRecording() const { return m_is_recording; }
			DelegateList<void (int)>& getFrameListeners() { return m_frame_listeners; }
			Block* getRootBlock() const { return m_root_block; }

			void beginBlock(const char* name, const char* function);
			void endBlock();
			

		private:
			bool m_is_recording;
			Block* m_current_block;
			Block* m_root_block;
			Timer* m_timer;
			int m_frame_uid;
			bool m_is_record_toggle_request;
			DelegateList<void (int)> m_frame_listeners;
	};

	class Profiler::Block
	{
		public:
			class Frame
			{
				public:
					int m_index;
					float m_length;
					float m_start;
			};
		
		public:
			Block()
				: m_frame_index(-1)
			{ 
			}
		
			~Block();

		public:
			Block* m_parent;
			Block* m_next;
			Block* m_first_child;
			Block* m_last_child;
			const char* m_name;
			const char* m_function;
			int m_frame_index;
			Frame m_frames[100];
	};


	extern LUX_CORE_API Profiler g_profiler;


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


#define BEGIN_PROFILE_BLOCK(name) Lux::g_profiler.beginBlock(name, __FUNCSIG__)
#define END_PROFILE_BLOCK() Lux::g_profiler.endBlock()
#define PROFILE_FUNCTION() Lux::ProfileScope profile_scope(__FUNCTION__, __FUNCSIG__);
#define PROFILE_BLOCK(name) Lux::ProfileScope profile_scope(name, __FUNCSIG__);

} // namespace Lux