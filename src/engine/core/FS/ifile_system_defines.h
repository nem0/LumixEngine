#pragma once

#include "lumix.h"
#include "core/delegate.h"

namespace Lumix
{
	namespace FS
	{
		class IFile;
		class FileSystem;

		typedef Delegate<void (IFile&, bool)> ReadCallback;
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

				OPEN_AND_READ = OPEN | READ
			};

			Mode() : value(0) {}
			Mode(Value _value) : value(_value) { }
			Mode(int32 _value) : value(_value) { }
			operator Value() const { return (Value)value; }
			int32 value;
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
			SeekMode(uint32 _value) : value(_value) {}
			operator Value() { return (Value)value; }
			uint32 value;
		};
	} // ~namespace FS
} // ~namespace Lumix
