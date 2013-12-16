#pragma once

#include "core/lux.h"
#include "core/ifile_device.h"

namespace Lux
{
	namespace Net
	{
		class TCPStream;
	}

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

		class LUX_CORE_API TCPFileDevice : public IFileDevice
		{
		public:
			virtual IFile* createFile(IFile* child) LUX_OVERRIDE;
			virtual const char* name() const LUX_OVERRIDE { return "tcp"; }

			void connect(const char* ip, uint16_t port);
			void disconnect();

			bool isInitialized() const;
			Net::TCPStream* getStream();

		private:
			TCPImpl* m_impl;
		};
	} // ~namespace FS
} // ~namespace Lux