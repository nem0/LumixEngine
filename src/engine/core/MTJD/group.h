#pragma once

#include "core/MTJD/base_entry.h"

namespace Lumix
{
	namespace MTJD
	{
		class LUMIX_ENGINE_API Group : public BaseEntry
		{
		public:
			Group(bool sync_event, IAllocator& allocator);
			~Group();

			void addStaticDependency(BaseEntry* entry);

			virtual void incrementDependency() override;
			virtual void decrementDependency() override;

		protected:

			void dependencyNotReady();
			void dependencyReady();

			DependencyTable m_static_dependency_table;
		};
	} // ~namepsace MTJD
} // ~namepsace Lumix
