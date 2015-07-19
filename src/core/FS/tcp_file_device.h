#pragma once

#include "core/lumix.h"
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
			int32_t value;
		};

		class LUMIX_ENGINE_API TCPFileDevice : public IFileDevice
		{
		public:
			virtual void destroyFile(IFile* file) override;
			virtual IFile* createFile(IFile* child) override;
			virtual const char* name() const override { return "tcp"; }

			void connect(const char* ip, uint16_t port, IAllocator& allocator);
			void disconnect();

			bool isInitialized() const;
			Net::TCPStream* getStream();

		private:
			TCPImpl* m_impl;
		};
	} // ~namespace FS
} // ~namespace Lumix
