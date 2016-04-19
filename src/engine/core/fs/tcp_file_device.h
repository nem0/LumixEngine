#pragma once

#include "lumix.h"
#include "core/fs/ifile_device.h"

namespace Lumix
{
	namespace Net
	{
		class TCPStream;
	}

	class IAllocator;

	namespace FS
	{
		class IFile;
		class TCPFileSystemTask;
		struct TCPImpl;

		struct TCPCommand
		{
			enum Value
			{
				OpenFile = 0,
				Close,
				Read,
				Write,
				Size,
				Seek,
				Pos,
				Disconnect,
			};

			TCPCommand() : value(0) {}
			TCPCommand(Value _value) : value(_value) { }
			TCPCommand(int _value) : value(_value) { }
			operator Value() const { return (Value)value; }
			int32 value;
		};

		class LUMIX_ENGINE_API TCPFileDevice : public IFileDevice
		{
		public:
			TCPFileDevice();

			void destroyFile(IFile* file) override;
			IFile* createFile(IFile* child) override;
			const char* name() const override { return "tcp"; }

			void connect(const char* ip, uint16 port, IAllocator& allocator);
			void disconnect();

			Net::TCPStream* getStream();

		private:
			TCPImpl* m_impl;
		};
	} // ~namespace FS
} // ~namespace Lumix
