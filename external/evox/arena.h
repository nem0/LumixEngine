#pragma once

#include <stddef.h>
#include <stdlib.h>

#include "platform.h"

#ifdef ERROR
	#undef ERROR
#endif
#ifdef CONST
	#undef CONST
#endif
#ifdef VOID
	#undef VOID
#endif
#ifdef TRUE
	#undef TRUE
#endif
#ifdef FALSE
	#undef FALSE
#endif

#include "capi.h"

typedef struct ex_default_arena {
	ex_arena arena;
	void* base;
	size_t reserve_size;
	size_t committed_size;
	size_t cursor;
	size_t page_size;
} ex_default_arena;

static inline uintptr ex_default_arena_align_up_uintptr(uintptr value, size_t align) {
	return (value + (uintptr)(align - 1)) & ~(uintptr)(align - 1);
}

static inline void* ex_default_arena_allocate(void* userdata, size_t size, size_t align) {
	ex_default_arena* arena = (ex_default_arena*)userdata;
	if (!arena) return NULL;
	if (size == 0) return NULL;
	if (align == 0) align = 1;
	if (align & (align - 1)) return NULL;

	uintptr base_addr = (uintptr)arena->base;
	size_t start = (size_t)(ex_default_arena_align_up_uintptr(base_addr + (uintptr)arena->cursor, align) - base_addr);
	size_t end = start + size;
	if (end < start || end > arena->reserve_size) {
		ASSERT(false);
		return NULL;
	}

	size_t committed_end = (size_t)ex_default_arena_align_up_uintptr((uintptr)end, arena->page_size);
	if (committed_end > arena->reserve_size) {
		ASSERT(false);
		return NULL;
	}
	if (committed_end > arena->committed_size) {
		void* commit_base = (char*)arena->base + arena->committed_size;
		size_t commit_size = committed_end - arena->committed_size;
		void* committed = ex_platform_commit(commit_base, commit_size) ? commit_base : NULL;
		if (!committed) {
			ASSERT(false);
			return NULL;
		}
		arena->committed_size = committed_end;
	}
	arena->cursor = end;
	return (char*)arena->base + start;
}

static inline void ex_default_arena_restore(void* userdata, void* ptr) {
	ex_default_arena* arena = (ex_default_arena*)userdata;
	if (!arena) return;

	size_t new_cursor = 0;
	if (ptr) {
		uintptr base_addr = (uintptr)arena->base;
		uintptr ptr_addr = (uintptr)ptr;
		if (ptr_addr < base_addr || ptr_addr > base_addr + (uintptr)arena->reserve_size) return;
		new_cursor = (size_t)(ptr_addr - base_addr);
	}

	arena->cursor = new_cursor;
}

static inline void ex_default_arena_create(ex_arena* out) {
	ex_default_arena* arena = (ex_default_arena*)malloc(sizeof(ex_default_arena));
	arena->page_size = ex_platform_page_size();
	arena->reserve_size = 256ull * 1024ull * 1024ull;
	arena->committed_size = 0;
	arena->cursor = 0;
	arena->base = ex_platform_reserve(arena->reserve_size);
	arena->arena.allocate = &ex_default_arena_allocate;
	arena->arena.restore = &ex_default_arena_restore;
	arena->arena.user_data = arena;
	*out = arena->arena;
}

static inline void ex_default_arena_destroy(ex_arena* arena) {
	if (!arena) return;
	ex_default_arena* impl = (ex_default_arena*)arena->user_data;
	if (impl) {
		if (impl->base) ex_platform_release(impl->base, impl->reserve_size);
		free(impl);
	}
}
