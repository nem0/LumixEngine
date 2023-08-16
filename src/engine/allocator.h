#pragma once 


#include "engine/lumix.h"

#define LUMIX_NEW(allocator, ...) new (Lumix::NewPlaceholder(), (allocator).allocate(sizeof(__VA_ARGS__), alignof(__VA_ARGS__))) __VA_ARGS__
#define LUMIX_DELETE(allocator, var) (allocator).deleteObject(var);

namespace Lumix { struct NewPlaceholder {}; }
inline void* operator new(size_t, Lumix::NewPlaceholder, void* where) { return where; }
inline void operator delete(void*, Lumix::NewPlaceholder,  void*) { } 

namespace Lumix {

struct LUMIX_ENGINE_API IAllocator {
	virtual ~IAllocator() {}
	virtual bool isDebug() const { return false; }
	virtual bool isTagAllocator() const { return false; }
	virtual IAllocator* getParent() const { return nullptr; }

	virtual void* allocate(size_t size, size_t align) = 0;
	virtual void deallocate(void* ptr) = 0;
	virtual void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) = 0;

	template <typename T> void deleteObject(T* ptr) {
		if (ptr)
		{
			ptr->~T();
			deallocate(ptr);
		}
	}
};

template <typename T>
struct Local
{
	~Local() {
		if (obj) obj->~T();
	}

	void operator =(const Local&) = delete;

	template <typename... Args>
	void create(Args&&... args) {
		ASSERT(!obj);
		obj = new (NewPlaceholder(), mem) T(static_cast<Args&&>(args)...);
	}

	void destroy() {
		ASSERT(obj);
		obj->~T();
		obj = nullptr;
	}

	T& operator*() { ASSERT(obj); return *obj; }
	T* operator->() const { ASSERT(obj); return obj; }
	T* get() const { return obj; }

private:
	alignas(T) u8 mem[sizeof(T)];
	T* obj = nullptr;
};

template <typename T>
struct UniquePtr {
	UniquePtr()
		: m_ptr(nullptr)
		, m_allocator(nullptr)
	{}

	UniquePtr(T* obj, IAllocator* allocator)
		: m_ptr(obj)
		, m_allocator(allocator)
	{}
	
	template <typename T2>
	UniquePtr(UniquePtr<T2>&& rhs) 
	{
		*this = static_cast<UniquePtr<T2>&&>(rhs);
	}

	~UniquePtr() {
		if (m_ptr) {
			LUMIX_DELETE(*m_allocator, m_ptr);
		}
	}

	UniquePtr(const UniquePtr& rhs) = delete; 
	void operator=(const UniquePtr& rhs) = delete;

	template <typename T2>
	void operator=(UniquePtr<T2>&& rhs) {
		if (m_ptr) {
			LUMIX_DELETE(*m_allocator, m_ptr);
		}
		m_allocator = rhs.getAllocator();
		m_ptr = static_cast<T*>(rhs.detach());
	}

	template <typename... Args> static UniquePtr<T> create(IAllocator& allocator, Args&&... args) {
		return UniquePtr<T>(LUMIX_NEW(allocator, T) (static_cast<Args&&>(args)...), &allocator);
	}

	T* detach() {
		T* res = m_ptr;
		m_ptr = nullptr;
		m_allocator = nullptr;
		return res;
	}

	UniquePtr&& move() { return static_cast<UniquePtr&&>(*this); }

	void reset() {
		if (m_ptr) {
			LUMIX_DELETE(*m_allocator, m_ptr);
		}
		m_ptr = nullptr;
		m_allocator = nullptr;
	}

	T* get() const { return m_ptr; }
	IAllocator* getAllocator() const { return m_allocator; }
	T& operator *() const { ASSERT(m_ptr); return *m_ptr; }
	T* operator ->() const { ASSERT(m_ptr); return m_ptr; }
	operator bool() const { return m_ptr; }

private:
	T* m_ptr = nullptr;
	IAllocator* m_allocator = nullptr;
};

} // namespace Lumix