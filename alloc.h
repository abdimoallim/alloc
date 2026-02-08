// Copyright 2026 Abdi Moalim
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ALLOC_H
#define ALLOC_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct allocator allocator;

struct allocator {
  void* (*alloc)(allocator* self, size_t size, size_t alignment);
  void* (*realloc)(allocator* self, void* ptr, size_t old_size, size_t new_size,
                   size_t alignment);
  void (*free)(allocator* self, void* ptr, size_t size);
  void* ctx;
};

static inline void* alloc_alloc(allocator* a, size_t size, size_t alignment) {
  return a->alloc(a, size, alignment);
}

static inline void* alloc_realloc(allocator* a, void* ptr, size_t old_size,
                                  size_t new_size, size_t alignment) {
  return a->realloc(a, ptr, old_size, new_size, alignment);
}

static inline void alloc_free(allocator* a, void* ptr, size_t size) {
  a->free(a, ptr, size);
}

static inline void* alloc_alloc_aligned(allocator* a, size_t size,
                                        size_t alignment) {
  return alloc_alloc(a, size, alignment);
}

static inline void* alloc_create(allocator* a, size_t size) {
  return alloc_alloc(a, size, sizeof(void*));
}

static inline void* alloc_create_array(allocator* a, size_t count,
                                       size_t elem_size) {
  return alloc_alloc(a, count * elem_size, sizeof(void*));
}

static inline void alloc_destroy(allocator* a, void* ptr, size_t size) {
  alloc_free(a, ptr, size);
}

static void* c_allocator_alloc(allocator* self, size_t size, size_t alignment) {
  (void)self;

  if (alignment <= sizeof(void*)) {
    return malloc(size);
  }

#if defined(_WIN32)
  return _aligned_malloc(size, alignment);
#else
  void* ptr = NULL;
  if (posix_memalign(&ptr, alignment, size) == 0)
    return ptr;
  return NULL;
#endif
}

static void* c_allocator_realloc(allocator* self, void* ptr, size_t old_size,
                                 size_t new_size, size_t alignment) {
  (void)self;
  (void)old_size;

  if (alignment <= sizeof(void*)) {
    return realloc(ptr, new_size);
  }

  void* new_ptr = c_allocator_alloc(self, new_size, alignment);

  if (new_ptr && ptr) {
    memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    c_allocator_alloc(self, 0, alignment);
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
  }

  return new_ptr;
}

static void c_allocator_free(allocator* self, void* ptr, size_t size) {
  (void)self;
  (void)size;
  if (!ptr)
    return;

#if defined(_WIN32)
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}

static allocator c_allocator_instance = {.alloc = c_allocator_alloc,
                                         .realloc = c_allocator_realloc,
                                         .free = c_allocator_free,
                                         .ctx = NULL};

static inline allocator* c_allocator(void) {
  return &c_allocator_instance;
}

typedef struct arena_allocator {
  uint8_t* buffer;
  size_t buffer_size;
  size_t offset;
  allocator* backing;
} arena_allocator;

static size_t align_forward(size_t ptr, size_t alignment) {
  size_t modulo = ptr & (alignment - 1);

  if (modulo != 0) {
    ptr += alignment - modulo;
  }

  return ptr;
}

static void* arena_allocator_alloc(allocator* self, size_t size,
                                   size_t alignment) {
  arena_allocator* arena = (arena_allocator*)self->ctx;

  size_t aligned_offset = align_forward(arena->offset, alignment);

  if (aligned_offset + size > arena->buffer_size) {
    return NULL;
  }

  void* ptr = arena->buffer + aligned_offset;
  arena->offset = aligned_offset + size;

  return ptr;
}

static void* arena_allocator_realloc(allocator* self, void* ptr,
                                     size_t old_size, size_t new_size,
                                     size_t alignment) {
  arena_allocator* arena = (arena_allocator*)self->ctx;

  if (!ptr) {
    return arena_allocator_alloc(self, new_size, alignment);
  }

  uint8_t* byte_ptr = (uint8_t*)ptr;

  if (byte_ptr + old_size == arena->buffer + arena->offset) {
    size_t aligned_offset =
        align_forward((size_t)(byte_ptr - arena->buffer), alignment);
    if (aligned_offset + new_size <= arena->buffer_size) {
      arena->offset = aligned_offset + new_size;
      return ptr;
    }
  }

  void* new_ptr = arena_allocator_alloc(self, new_size, alignment);
  if (new_ptr && ptr) {
    memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
  }

  return new_ptr;
}

static void arena_allocator_free(allocator* self, void* ptr, size_t size) {
  (void)self;
  (void)ptr;
  (void)size;
}

static void arena_allocator_reset(arena_allocator* arena) {
  arena->offset = 0;
}

static void arena_allocator_init(arena_allocator* arena, void* buffer,
                                 size_t size) {
  arena->buffer = (uint8_t*)buffer;
  arena->buffer_size = size;
  arena->offset = 0;
  arena->backing = NULL;
}

static allocator arena_allocator_get(arena_allocator* arena) {
  allocator alloc = {.alloc = arena_allocator_alloc,
                     .realloc = arena_allocator_realloc,
                     .free = arena_allocator_free,
                     .ctx = arena};
  return alloc;
}

typedef struct pool_allocator {
  uint8_t* buffer;
  size_t chunk_size;
  size_t chunk_count;
  void** free_list;
  allocator* backing;
} pool_allocator;

static void pool_allocator_free(allocator* self, void* ptr, size_t size);

static void* pool_allocator_alloc(allocator* self, size_t size,
                                  size_t alignment) {
  pool_allocator* pool = (pool_allocator*)self->ctx;
  (void)alignment;

  if (size > pool->chunk_size) {
    return NULL;
  }

  if (!pool->free_list) {
    return NULL;
  }

  void* ptr = pool->free_list;
  pool->free_list = (void**)*pool->free_list;

  return ptr;
}

static void* pool_allocator_realloc(allocator* self, void* ptr, size_t old_size,
                                    size_t new_size, size_t alignment) {
  pool_allocator* pool = (pool_allocator*)self->ctx;

  if (new_size <= pool->chunk_size && old_size <= pool->chunk_size) {
    return ptr;
  }

  void* new_ptr = pool_allocator_alloc(self, new_size, alignment);

  if (new_ptr && ptr) {
    memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    pool_allocator_free(self, ptr, old_size);
  }

  return new_ptr;
}

static void pool_allocator_free(allocator* self, void* ptr, size_t size) {
  pool_allocator* pool = (pool_allocator*)self->ctx;
  (void)size;

  if (!ptr)
    return;

  void** free_node = (void**)ptr;
  *free_node = pool->free_list;
  pool->free_list = free_node;
}

static void pool_allocator_init(pool_allocator* pool, void* buffer,
                                size_t chunk_size, size_t chunk_count) {
  pool->buffer = (uint8_t*)buffer;
  pool->chunk_size = chunk_size;
  pool->chunk_count = chunk_count;
  pool->backing = NULL;
  pool->free_list = NULL;

  for (size_t i = 0; i < chunk_count; i++) {
    void* chunk = pool->buffer + (chunk_count - 1 - i) * chunk_size;
    void** free_node = (void**)chunk;
    *free_node = pool->free_list;
    pool->free_list = free_node;
  }
}

static allocator pool_allocator_get(pool_allocator* pool) {
  allocator alloc = {.alloc = pool_allocator_alloc,
                     .realloc = pool_allocator_realloc,
                     .free = pool_allocator_free,
                     .ctx = pool};
  return alloc;
}

typedef struct stack_allocator {
  uint8_t* buffer;
  size_t buffer_size;
  size_t offset;
  allocator* backing;
} stack_allocator;

typedef struct stack_marker {
  size_t offset;
} stack_marker;

static void* stack_allocator_alloc(allocator* self, size_t size,
                                   size_t alignment) {
  stack_allocator* stack = (stack_allocator*)self->ctx;

  size_t aligned_offset = align_forward(stack->offset, alignment);

  if (aligned_offset + size > stack->buffer_size) {
    return NULL;
  }

  void* ptr = stack->buffer + aligned_offset;
  stack->offset = aligned_offset + size;

  return ptr;
}

static void* stack_allocator_realloc(allocator* self, void* ptr,
                                     size_t old_size, size_t new_size,
                                     size_t alignment) {
  stack_allocator* stack = (stack_allocator*)self->ctx;

  if (!ptr) {
    return stack_allocator_alloc(self, new_size, alignment);
  }

  uint8_t* byte_ptr = (uint8_t*)ptr;

  if (byte_ptr + old_size == stack->buffer + stack->offset) {
    size_t aligned_offset =
        align_forward((size_t)(byte_ptr - stack->buffer), alignment);

    if (aligned_offset + new_size <= stack->buffer_size) {
      stack->offset = aligned_offset + new_size;
      return ptr;
    }
  }

  void* new_ptr = stack_allocator_alloc(self, new_size, alignment);

  if (new_ptr && ptr) {
    memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
  }

  return new_ptr;
}

static void stack_allocator_free(allocator* self, void* ptr, size_t size) {
  stack_allocator* stack = (stack_allocator*)self->ctx;

  if (!ptr)
    return;

  uint8_t* byte_ptr = (uint8_t*)ptr;

  if (byte_ptr + size == stack->buffer + stack->offset) {
    stack->offset = (size_t)(byte_ptr - stack->buffer);
  }
}

static void stack_allocator_init(stack_allocator* stack, void* buffer,
                                 size_t size) {
  stack->buffer = (uint8_t*)buffer;
  stack->buffer_size = size;
  stack->offset = 0;
  stack->backing = NULL;
}

static stack_marker stack_allocator_mark(stack_allocator* stack) {
  stack_marker marker = {.offset = stack->offset};
  return marker;
}

static void stack_allocator_restore(stack_allocator* stack,
                                    stack_marker marker) {
  stack->offset = marker.offset;
}

static void stack_allocator_reset(stack_allocator* stack) {
  stack->offset = 0;
}

static allocator stack_allocator_get(stack_allocator* stack) {
  allocator alloc = {.alloc = stack_allocator_alloc,
                     .realloc = stack_allocator_realloc,
                     .free = stack_allocator_free,
                     .ctx = stack};
  return alloc;
}

typedef struct scratch_allocator {
  allocator* backing;
  void** allocations;
  size_t allocation_count;
  size_t allocation_capacity;
} scratch_allocator;

static void* scratch_allocator_alloc(allocator* self, size_t size,
                                     size_t alignment) {
  scratch_allocator* scratch = (scratch_allocator*)self->ctx;

  void* ptr = alloc_alloc(scratch->backing, size, alignment);

  if (!ptr)
    return NULL;

  if (scratch->allocation_count >= scratch->allocation_capacity) {
    size_t new_capacity = scratch->allocation_capacity == 0
                              ? 8
                              : scratch->allocation_capacity * 2;
    void** new_allocations =
        (void**)alloc_realloc(scratch->backing, scratch->allocations,
                              scratch->allocation_capacity * sizeof(void*),
                              new_capacity * sizeof(void*), sizeof(void*));

    if (!new_allocations) {
      alloc_free(scratch->backing, ptr, size);
      return NULL;
    }

    scratch->allocations = new_allocations;
    scratch->allocation_capacity = new_capacity;
  }

  scratch->allocations[scratch->allocation_count++] = ptr;

  return ptr;
}

static void* scratch_allocator_realloc(allocator* self, void* ptr,
                                       size_t old_size, size_t new_size,
                                       size_t alignment) {
  scratch_allocator* scratch = (scratch_allocator*)self->ctx;

  if (!ptr) {
    return scratch_allocator_alloc(self, new_size, alignment);
  }

  void* new_ptr =
      alloc_realloc(scratch->backing, ptr, old_size, new_size, alignment);

  if (!new_ptr)
    return NULL;

  for (size_t i = 0; i < scratch->allocation_count; i++) {
    if (scratch->allocations[i] == ptr) {
      scratch->allocations[i] = new_ptr;
      break;
    }
  }

  return new_ptr;
}

static void scratch_allocator_free(allocator* self, void* ptr, size_t size) {
  (void)self;
  (void)ptr;
  (void)size;
}

static void scratch_allocator_init(scratch_allocator* scratch,
                                   allocator* backing) {
  scratch->backing = backing;
  scratch->allocations = NULL;
  scratch->allocation_count = 0;
  scratch->allocation_capacity = 0;
}

static void scratch_allocator_reset(scratch_allocator* scratch) {
  for (size_t i = 0; i < scratch->allocation_count; i++) {
    alloc_free(scratch->backing, scratch->allocations[i], 0);
  }

  scratch->allocation_count = 0;
}

static void scratch_allocator_destroy(scratch_allocator* scratch) {
  scratch_allocator_reset(scratch);

  if (scratch->allocations) {
    alloc_free(scratch->backing, scratch->allocations,
               scratch->allocation_capacity * sizeof(void*));
  }

  scratch->allocations = NULL;
  scratch->allocation_capacity = 0;
}

static allocator scratch_allocator_get(scratch_allocator* scratch) {
  allocator alloc = {.alloc = scratch_allocator_alloc,
                     .realloc = scratch_allocator_realloc,
                     .free = scratch_allocator_free,
                     .ctx = scratch};
  return alloc;
}

typedef struct freelist_node freelist_node;

struct freelist_node {
  size_t size;
  freelist_node* next;
};

typedef struct freelist_allocator {
  uint8_t* buffer;
  size_t buffer_size;
  freelist_node* free_list;
  allocator* backing;
} freelist_allocator;

static void freelist_allocator_free(allocator* self, void* ptr, size_t size);

static void* freelist_allocator_alloc(allocator* self, size_t size,
                                      size_t alignment) {
  freelist_allocator* freelist = (freelist_allocator*)self->ctx;

  size_t total_size = size + alignment - 1;

  freelist_node** prev_ptr = &freelist->free_list;
  freelist_node* node = freelist->free_list;

  while (node) {
    if (node->size >= total_size) {
      uintptr_t addr = (uintptr_t)node;
      uintptr_t aligned_addr = align_forward(addr, alignment);
      size_t padding = aligned_addr - addr;

      if (node->size >= size + padding) {
        *prev_ptr = node->next;

        if (node->size > total_size + sizeof(freelist_node)) {
          freelist_node* new_node =
              (freelist_node*)((uint8_t*)node + size + padding);
          new_node->size = node->size - (size + padding);
          new_node->next = freelist->free_list;
          freelist->free_list = new_node;
        }

        return (void*)aligned_addr;
      }
    }

    prev_ptr = &node->next;
    node = node->next;
  }

  return NULL;
}

static void* freelist_allocator_realloc(allocator* self, void* ptr,
                                        size_t old_size, size_t new_size,
                                        size_t alignment) {
  if (!ptr) {
    return freelist_allocator_alloc(self, new_size, alignment);
  }

  void* new_ptr = freelist_allocator_alloc(self, new_size, alignment);

  if (new_ptr && ptr) {
    memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    freelist_allocator_free(self, ptr, old_size);
  }

  return new_ptr;
}

static void freelist_allocator_free(allocator* self, void* ptr, size_t size) {
  freelist_allocator* freelist = (freelist_allocator*)self->ctx;

  if (!ptr)
    return;

  freelist_node* node = (freelist_node*)ptr;
  node->size = size;
  node->next = freelist->free_list;
  freelist->free_list = node;
}

static void freelist_allocator_init(freelist_allocator* freelist, void* buffer,
                                    size_t size) {
  freelist->buffer = (uint8_t*)buffer;
  freelist->buffer_size = size;
  freelist->backing = NULL;

  freelist->free_list = (freelist_node*)buffer;
  freelist->free_list->size = size;
  freelist->free_list->next = NULL;
}

static allocator freelist_allocator_get(freelist_allocator* freelist) {
  allocator alloc = {.alloc = freelist_allocator_alloc,
                     .realloc = freelist_allocator_realloc,
                     .free = freelist_allocator_free,
                     .ctx = freelist};
  return alloc;
}

#endif
