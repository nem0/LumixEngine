#pragma once


#ifndef _WIN32
#error Platform not supported
#endif

#define STRINGIZE_2( _ ) #_
#define STRINGIZE( _ ) STRINGIZE_2( _ )


#define JOIN_STRINGS_2(A, B) A ## B
#define JOIN_STRINGS(A, B) JOIN_STRINGS_2(A, B)


#define NOTE(msg) __pragma(message(__FILE__ "(" STRINGIZE(__LINE__) ") : NOTE: " msg))

#define TODO(msg) __pragma(message(__FILE__ "(" STRINGIZE(__LINE__) ") : TODO: " msg))

#define WARN(msg) __pragma(message(__FILE__ "(" STRINGIZE(__LINE__) ") : WARNING: " msg))


namespace Lumix
{
	typedef char					int8;
	typedef unsigned char			uint8;
	typedef short					int16;
	typedef unsigned short			uint16;
	typedef int						int32;
	typedef unsigned int			uint32;
	typedef long long				int64;
	typedef unsigned long long		uint64;

#ifdef _WIN64
	typedef uint64 uintptr;
#else
	typedef uint32 uintptr;
#endif

	static_assert(sizeof(int64) == 8, "Incorrect size of int64");
	static_assert(sizeof(int32) == 4, "Incorrect size of int32");
	static_assert(sizeof(int16) == 2, "Incorrect size of int16");
	static_assert(sizeof(int8) == 1, "Incorrect size of int8");
}


namespace Lumix
{
	const uint32 MAX_PATH_LENGTH = 260;
	typedef int ComponentIndex;
	typedef int Entity;
	const int INVALID_ENTITY = -1;
	const int INVALID_COMPONENT = -1;

	template <typename T, int count>
	int lengthOf(const T(&)[count])
	{
		return count;
	};
}


#ifndef ASSERT
	#ifdef _WIN32
		#ifdef NDEBUG
			#define ASSERT(x) { false ? (void)(x) : 0; } 
		#else
			#define ASSERT(x) { const volatile bool lumix_assert_b____ = !(x); if(lumix_assert_b____) __debugbreak(); } 
		#endif
	#else
		#define ASSERT(x) assert(x)
	#endif
#endif


#define LUMIX_NEW(allocator, type) new ((allocator).allocate(sizeof(type))) type
#define LUMIX_DELETE(allocator, var) (allocator).deleteObject(var);
#define LUMIX_LIBRARY_EXPORT __declspec(dllexport)
#define LUMIX_LIBRARY_IMPORT __declspec(dllimport)
#define LUMIX_ALIGN_OF(T) __alignof(T)
#define LUMIX_FORCE_INLINE __forceinline
#define LUMIX_RESTRICT __restrict


#ifdef BUILDING_PHYSICS
	#define LUMIX_PHYSICS_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_PHYSICS_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef BUILDING_ENGINE
	#define LUMIX_ENGINE_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_ENGINE_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef BUILDING_EDITOR
	#define LUMIX_EDITOR_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_EDITOR_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef BUILDING_RENDERER
	#define LUMIX_RENDERER_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_RENDERER_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef BUILDING_SCRIPT
	#define LUMIX_SCRIPT_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_SCRIPT_API LUMIX_LIBRARY_IMPORT
#endif

#ifdef BUILDING_ANIMATION
	#define LUMIX_ANIMATION_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_ANIMATION_API LUMIX_LIBRARY_IMPORT
#endif


#pragma warning(disable : 4251)
#pragma warning(disable : 4512)
#pragma warning(disable : 4996)

