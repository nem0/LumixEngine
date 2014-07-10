#pragma once

namespace Lumix
{
	namespace MTJD
	{
		enum class Priority
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

		enum class Flags
		{

			AutoDestroy = 0,
			Scheduled,
			Executed,
			Count,

		};

		enum class JobType
		{
			None = -1,
			Animation,
			Clipper,
			Count,
		};
	} // ~namepsace MTJD
} // ~namepsace Lumix
