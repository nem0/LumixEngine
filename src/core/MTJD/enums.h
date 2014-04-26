#pragma once

namespace Lux
{
	namespace MTJD
	{
		class Priority
		{
		public:
			enum Value
			{
				None = -1,
				High,
				AboveNormal,
				Normal,
				Low,
				VeryLow,
				Idle,
				Count,
				Default = Normal,
			};

			Priority(Value value) : _value(value) {}

			operator Value() const
			{
				return _value;
			}

		private:
			Value _value;
		};

		class Flags
		{
		public:
			enum Value
			{
				AutoDestroy = 0,
				Scheduled,
				Executed,
				Count,
			};

			Flags(Value value) : _value(value) {}

			operator Value() const
			{
				return _value;
			}

		private:
			Value _value;
		};

		class JobType
		{
		public:
			enum Value
			{
				None = -1,
				Animation,
				Clipper,
				Count,
			};

			JobType(Value value) : _value(value) {}

			operator Value() const
			{
				return _value;
			}

		private:
			Value _value;
		};
	} // ~namepsace MTJD
} // ~namepsace Lux