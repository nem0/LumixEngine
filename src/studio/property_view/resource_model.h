#pragma once


#include "core/resource.h"
#include "dynamic_object_model.h"


namespace Lumix
{
	class Resource;
}


class ResourceModel : public DynamicObjectModel
{
	public:
		explicit ResourceModel(Lumix::Resource* resource);
		~ResourceModel();

	private:
		void onResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state);
		void fillModelInfo();
		void fillMaterialInfo();
		void fillTextureInfo();

	private:
		Lumix::Resource* m_resource;
};
