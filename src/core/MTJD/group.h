#pragma once

#include "core/MTJD/base_entry.h"

namespace Lux
{
	namespace MTJD
	{
		class LUX_CORE_API Group : public BaseEntry
		{
		public:
			Group(bool sync_event = false);
			~Group();

			void addStaticDependency(BaseEntry* entry);

			virtual void incrementDependency();
			virtual void decrementDependency();

		protected:

			void dependencyNotReady();
			void dependencyReady();

			TDependencyTable m_static_dependency_table;
		};
	} // ~namepsace MTJD
} // ~namepsace Lux