#include "core/library.h"
#include "core/iallocator.h"
#include "core/log.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


namespace Lumix
{


class LibraryPC : public Library
{
	public:
		LibraryPC(const Path& path, IAllocator& allocator) : m_allocator(allocator), m_path(path), m_module(nullptr) {}
		~LibraryPC() { unload(); }
		IAllocator& getAllocator() { return m_allocator; }
	
		virtual bool isLoaded() const override
		{
			return m_module != nullptr;
		}


		virtual bool load() override
		{
			ASSERT(!isLoaded());
			m_module = LoadLibrary(m_path.c_str());
			return m_module != nullptr;
		}


		virtual bool unload() override
		{
			bool status = FreeLibrary(m_module) == TRUE;
			if (status)
			{
				m_module = nullptr;
			}
			return status;
		}


		virtual void* resolve(const char* name) override
		{
			return GetProcAddress(m_module, name);
		}


	private:
		IAllocator& m_allocator;
		HMODULE m_module;
		Path m_path;
};


Library* Library::create(const Path& path, IAllocator& allocator)
{
	return LUMIX_NEW(allocator, LibraryPC)(path, allocator);
}


void Library::destroy(Library* library)
{
	LUMIX_DELETE(static_cast<LibraryPC*>(library)->getAllocator(), library);
}


} // namespace Lumix
