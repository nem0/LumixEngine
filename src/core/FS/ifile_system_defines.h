#pragma once

#include "core/lumix.h"
#include "core/delegate.h"

namespace Lumix
{
	namespace FS
	{
		class IFile;
		class FileSystem;

		typedef Delegate<void (IFile*, bool, FileSystem&)> ReadCallback;
		struct Mode
		{
			enum Value
			{
				NONE			= 0,
				READ			= 0x1,
				WRITE			= READ << 1,
				OPEN			= WRITE << 1,
				CREATE			= OPEN << 1,
				OPEN_OR_CREATE	= CREATE << 1,
				RECREATE		= OPEN_OR_CREATE << 1,
			};

			Mode() : value(0) {}
			Mode(Value _value) : value(_value) { }
			Mode(int32_t _value) : value(_value) { }
			operator Value() const { return (Value)value; }
			int32_t value;
		};

		struct SeekMode
		{
			enum Value
			{
				BEGIN = 0,
				END,
				CURRENT,
			};
			SeekMode(Value _value) : value(_value) {}
			SeekMode(uint32_t _value) : value(_value) {}
			operator Value() { return (Value)value; }
			uint32_t value;
		};
	} // ~namespace FS
} // ~namespace Lumix
