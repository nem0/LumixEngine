#pragma once

#include "engine/lumix.h"
#include "engine/fs/ifile_device.h"

namespace Lumix
{
	namespace Net
	{
		class TCPStream;
	}

	struct IAllocator;

	namespace FS
	{
		struct IFile;
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
			i32 value;
		};

		class LUMIX_ENGINE_API TCPFileDevice LUMIX_FINAL : public IFileDevice
		{
		public:
			TCPFileDevice();

			void destroyFile(IFile* file) override;
			IFile* createFile(IFile* child) override;
			const char* name() const override { return "tcp"; }

			void connect(const char* ip, u16 port, IAllocator& allocator);
			void disconnect();

			Net::TCPStream* getStream();

		private:
			TCPImpl* m_impl;
		};
	} // ~namespace FS
} // ~namespace Lumix
