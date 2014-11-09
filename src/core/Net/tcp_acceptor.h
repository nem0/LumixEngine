#pragma once

#include "core/lumix.h"

namespace Lumix
{
	class IAllocator;

	namespace Net
	{
		class TCPStream;

		class LUMIX_CORE_API TCPAcceptor
		{
		public:
			TCPAcceptor(IAllocator& allocator) : m_allocator(allocator) {}
			~TCPAcceptor();

			bool start(const char* ip, uint16_t port);
			TCPStream* accept();
			void close(TCPStream* stream);
			
		private:
			IAllocator& m_allocator;
			uintptr_t m_socket;
		};
	} // ~namespace Net
} // ~namespace Lumix
