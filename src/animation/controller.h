#pragma once


#include "engine/resource.h"
#include "engine/resource_manager_base.h"
#include "state_machine.h"


namespace Lumix
{


namespace Anim
{


class ControllerManager LUMIX_FINAL : public ResourceManagerBase
{
public:
	explicit ControllerManager(IAllocator& allocator)
		: ResourceManagerBase(allocator)
		, m_allocator(allocator)
	{}
	~ControllerManager() {}
	IAllocator& getAllocator() { return m_allocator; }

protected:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
};


class ControllerResource : public Resource
{
public:
	ControllerResource(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);

	void unload(void) override;
	bool load(FS::IFile& file) override;
	ComponentInstance* createInstance(IAllocator& allocator);
	InputDecl& getInputDecl() { return m_input_decl; }
	void setRoot(Component* component);
	Component* getRoot() const { return m_root; }
	void serialize(OutputBlob& blob);
	void deserialize(InputBlob& blob);

private:
	InputDecl m_input_decl;
	Component* m_root;
	IAllocator& m_allocator;
};


} // namespace Anim


} // namespace Lumix