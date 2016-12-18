/*
 * Copyright 2014-2015 Dario Manesku. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef CMFT_ALLOCATOR_H_HEADER_GUARD
#define CMFT_ALLOCATOR_H_HEADER_GUARD

#include <string.h> // size_t

#include <stdint.h>
#include <stdlib.h>

namespace cmft
{
    #ifndef CMFT_ALLOCATOR_DEBUG
    #   define CMFT_ALLOCATOR_DEBUG 0
    #endif // CMFT_ALLOCATOR_DEBUG

    #ifndef CMFT_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT
    #   define CMFT_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT 8
    #endif // CMFT_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT

    #if defined(_MSC_VER)
    #   define CMFT_NO_VTABLE __declspec(novtable)
    #else
    #   define CMFT_NO_VTABLE
    #endif

    struct CMFT_NO_VTABLE AllocatorI
    {
        virtual void* realloc(void* _ptr, size_t _size, size_t _align, const char* _file, size_t _line) = 0;
    };

    struct CMFT_NO_VTABLE StackAllocatorI : AllocatorI
    {
        virtual void push(const char* _file, size_t _line) = 0;
        virtual void pop(const char* _file, size_t _line) = 0;
    };

    struct StackAllocatorScope
    {
        StackAllocatorScope(StackAllocatorI* _stackAlloc) : m_stack(_stackAlloc)
        {
            m_stack->push(0,0);
        }

        ~StackAllocatorScope()
        {
            m_stack->pop(0,0);
        }

    private:
        StackAllocatorI* m_stack;
    };

    #if CMFT_ALLOCATOR_DEBUG
    #   define CMFT_ALLOC(_allocator, _size)                         (_allocator)->realloc(NULL, _size,      0, __FILE__, __LINE__)
    #   define CMFT_REALLOC(_allocator, _ptr, _size)                 (_allocator)->realloc(_ptr, _size,      0, __FILE__, __LINE__)
    #   define CMFT_FREE(_allocator, _ptr)                           (_allocator)->realloc(_ptr,     0,      0, __FILE__, __LINE__)
    #   define CMFT_ALIGNED_ALLOC(_allocator, _size, _align)         (_allocator)->realloc(NULL, _size, _align, __FILE__, __LINE__)
    #   define CMFT_ALIGNED_REALLOC(_allocator, _ptr, _size, _align) (_allocator)->realloc(_ptr, _size, _align, __FILE__, __LINE__)
    #   define CMFT_ALIGNED_FREE(_allocator, _ptr, _align)           (_allocator)->realloc(_ptr,     0, _align, __FILE__, __LINE__)
    #   define CMFT_PUSH(_stackAllocator) (_stackAllocator)->push(__FILE__, __LINE__)
    #   define CMFT_POP(_stackAllocator)  (_stackAllocator)->pop(__FILE__, __LINE__)
    #else
    #   define CMFT_ALLOC(_allocator, _size)                         (_allocator)->realloc(NULL, _size,      0, 0, 0)
    #   define CMFT_REALLOC(_allocator, _ptr, _size)                 (_allocator)->realloc(_ptr, _size,      0, 0, 0)
    #   define CMFT_FREE(_allocator, _ptr)                           (_allocator)->realloc(_ptr,     0,      0, 0, 0)
    #   define CMFT_ALIGNED_ALLOC(_allocator, _size, _align)         (_allocator)->realloc(NULL, _size, _align, 0, 0)
    #   define CMFT_ALIGNED_REALLOC(_allocator, _ptr, _size, _align) (_allocator)->realloc(_ptr, _size, _align, 0, 0)
    #   define CMFT_ALIGNED_FREE(_allocator, _ptr, _align)           (_allocator)->realloc(_ptr,     0, _align, 0, 0)
    #   define CMFT_PUSH(_stackAllocator) (_stackAllocator)->push(0, 0)
    #   define CMFT_POP(_stackAllocator)  (_stackAllocator)->pop(0, 0)
    #endif // CMFT_ALLOCATOR_DEBUG

    struct CrtAllocator : AllocatorI
    {
        virtual void* realloc(void* _ptr, size_t _size, size_t _align, const char* /*_file*/, size_t /*_line*/)
        {
            (void)_align; // Ignoring alignment for now.

            if (0 == _ptr)
            {
                return ::malloc(_size);
            }
            else if (0 == _size)
            {
                ::free(_ptr);
                return NULL;
            }
            else
            {
                return ::realloc(_ptr, _size);
            }
        }
    };
    extern CrtAllocator g_crtAllocator;

    struct CrtStackAllocator : StackAllocatorI
    {
        virtual void* realloc(void* _ptr, size_t _size, size_t _align, const char* /*_file*/, size_t /*_line*/)
        {
            (void)_align; // Ignoring alignment for now.

            if (0 == _ptr)
            {
                void* ptr = ::malloc(_size);
                m_ptrs[m_curr++] = ptr;
                return ptr;
            }
            else if (0 == _size)
            {
                ::free(_ptr);
                return NULL;
            }
            else
            {
                void* ptr = ::realloc(_ptr, _size);
                m_ptrs[m_curr++] = ptr;
                return ptr;
            }
        }

        virtual void push(const char* /*_file*/, size_t /*_line*/)
        {
            m_frames[m_frameIdx++] = m_curr;
        }

        virtual void pop(const char* /*_file*/, size_t /*_line*/)
        {
            uint16_t prev = m_frames[--m_frameIdx];
            for (uint16_t ii = prev, iiEnd = m_curr; ii < iiEnd; ++ii)
            {
                ::free(m_ptrs[ii]);
            }
            m_curr = prev;
        }

        enum
        {
            MaxAllocations = 4096,
            MaxFrames      = 4096,
        };

        uint16_t m_curr;
        uint16_t m_frameIdx;
        void* m_ptrs[MaxAllocations];
        uint16_t m_frames[MaxAllocations];
    };
    extern CrtStackAllocator g_crtStackAllocator;

    extern AllocatorI*      g_allocator;
    extern StackAllocatorI* g_stackAllocator;

    void setAllocator(AllocatorI* _allocator);
    void setStackAllocator(StackAllocatorI* _stackAllocator);
};

#endif // CMFT_ALLOCATOR_H_HEADER_GUARD

/* vim: set sw=4 ts=4 expandtab: */
