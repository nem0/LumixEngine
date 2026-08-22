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

typedef struct ls_default_arena {
	ls_arena arena;
	void* base;
	size_t reserve_size;
	size_t committed_size;
	size_t cursor;
	size_t page_size;
} ls_default_arena;

static inline uintptr ls_default_arena_align_up_uintptr(uintptr value, size_t align) {
	return (value + (uintptr)(align - 1)) & ~(uintptr)(align - 1);
}

static inline void* ls_default_arena_allocate(void* userdata, size_t size, size_t align) {
	ls_default_arena* arena = (ls_default_arena*)userdata;
	if (!arena) return NULL;
	if (size == 0) return NULL;
	if (align == 0) align = 1;
	if (align & (align - 1)) return NULL;

	uintptr base_addr = (uintptr)arena->base;
	size_t start = (size_t)(ls_default_arena_align_up_uintptr(base_addr + (uintptr)arena->cursor, align) - base_addr);
	size_t end = start + size;
	if (end < start || end > arena->reserve_size) {
		ASSERT(false);
		return NULL;
	}

	size_t committed_end = (size_t)ls_default_arena_align_up_uintptr((uintptr)end, arena->page_size);
	if (committed_end > arena->reserve_size) {
		ASSERT(false);
		return NULL;
	}
	if (committed_end > arena->committed_size) {
		void* commit_base = (char*)arena->base + arena->committed_size;
		size_t commit_size = committed_end - arena->committed_size;
		void* committed = ls_platform_commit(commit_base, commit_size) ? commit_base : NULL;
		if (!committed) {
			ASSERT(false);
			return NULL;
		}
		arena->committed_size = committed_end;
	}
	arena->cursor = end;
	return (char*)arena->base + start;
}

static inline void ls_default_arena_restore(void* userdata, void* ptr) {
	ls_default_arena* arena = (ls_default_arena*)userdata;
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

static inline void ls_default_arena_create(ls_arena* out) {
	ls_default_arena* arena = (ls_default_arena*)malloc(sizeof(ls_default_arena));
	arena->page_size = ls_platform_page_size();
	arena->reserve_size = 256ull * 1024ull * 1024ull;
	arena->committed_size = 0;
	arena->cursor = 0;
	arena->base = ls_platform_reserve(arena->reserve_size);
	arena->arena.allocate = &ls_default_arena_allocate;
	arena->arena.restore = &ls_default_arena_restore;
	arena->arena.user_data = arena;
	*out = arena->arena;
}

static inline void ls_default_arena_destroy(ls_arena* arena) {
	if (!arena) return;
	ls_default_arena* impl = (ls_default_arena*)arena->user_data;
	if (impl) {
		if (impl->base) ls_platform_release(impl->base, impl->reserve_size);
		free(impl);
	}
}
