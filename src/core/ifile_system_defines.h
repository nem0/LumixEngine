#pragma once

#include "core/lux.h"

namespace Lux
{
	namespace FS
	{
		class IFile;

		typedef void (*ReadCallback)(IFile* file, bool success, void* user_data);
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
} // ~namespace Lux