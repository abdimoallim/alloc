#include "alloc.h"
#include <stdio.h>

int main(void) {
  printf("=== c allocator ===\n");
  allocator* c_alloc = c_allocator();
  int* nums = (int*)alloc_create_array(c_alloc, 5, sizeof(int));

  for (int i = 0; i < 5; i++) {
    nums[i] = i * 10;
  }

  for (int i = 0; i < 5; i++) {
    printf("%d ", nums[i]);
  }

  printf("\n");
  alloc_destroy(c_alloc, nums, 5 * sizeof(int));

  printf("\n=== arena allocator ===\n");
  uint8_t arena_buffer[1024];
  arena_allocator arena;
  arena_allocator_init(&arena, arena_buffer, sizeof(arena_buffer));
  allocator arena_alloc = arena_allocator_get(&arena);

  int* a = (int*)alloc_create(&arena_alloc, sizeof(int));
  int* b = (int*)alloc_create(&arena_alloc, sizeof(int));
  *a = 42;
  *b = 99;
  printf("a = %d, b = %d\n", *a, *b);
  printf("arena used: %zu bytes\n", arena.offset);

  arena_allocator_reset(&arena);
  printf("arena reset, offset: %zu\n", arena.offset);

  printf("\n=== pool allocator ===\n");
  uint8_t pool_buffer[256];
  pool_allocator pool;
  pool_allocator_init(&pool, pool_buffer, 32, 8);
  allocator pool_alloc = pool_allocator_get(&pool);

  void* p1 = alloc_alloc(&pool_alloc, 32, 1);
  void* p2 = alloc_alloc(&pool_alloc, 32, 1);
  void* p3 = alloc_alloc(&pool_alloc, 32, 1);
  printf("allocated 3 chunks: %p, %p, %p\n", p1, p2, p3);

  alloc_free(&pool_alloc, p2, 32);
  printf("freed middle chunk\n");

  void* p4 = alloc_alloc(&pool_alloc, 32, 1);
  printf("reallocated chunk: %p (should reuse freed chunk)\n", p4);

  printf("\n=== stack allocator ===\n");
  uint8_t stack_buffer[512];
  stack_allocator stack;
  stack_allocator_init(&stack, stack_buffer, sizeof(stack_buffer));
  allocator stack_alloc = stack_allocator_get(&stack);

  int* x = (int*)alloc_create(&stack_alloc, sizeof(int));
  *x = 123;
  stack_marker mark = stack_allocator_mark(&stack);

  int* y = (int*)alloc_create(&stack_alloc, sizeof(int));
  int* z = (int*)alloc_create(&stack_alloc, sizeof(int));
  *y = 456;
  *z = 789;
  printf("x = %d, y = %d, z = %d\n", *x, *y, *z);
  printf("stack offset: %zu\n", stack.offset);

  stack_allocator_restore(&stack, mark);
  printf("restored to marker, offset: %zu\n", stack.offset);

  printf("\n=== scratch allocator ===\n");
  scratch_allocator scratch;
  scratch_allocator_init(&scratch, c_allocator());
  allocator scratch_alloc = scratch_allocator_get(&scratch);

  int* arr1 = (int*)alloc_create_array(&scratch_alloc, 10, sizeof(int));
  int* arr2 = (int*)alloc_create_array(&scratch_alloc, 20, sizeof(int));
  printf("allocated 2 arrays, total allocations: %zu\n",
         scratch.allocation_count);

  scratch_allocator_reset(&scratch);
  printf("reset scratch, all freed, allocations: %zu\n",
         scratch.allocation_count);

  scratch_allocator_destroy(&scratch);

  printf("\n=== freelist allocator ===\n");
  uint8_t freelist_buffer[1024];
  freelist_allocator freelist;
  freelist_allocator_init(&freelist, freelist_buffer, sizeof(freelist_buffer));
  allocator freelist_alloc = freelist_allocator_get(&freelist);

  void* f1 = alloc_alloc(&freelist_alloc, 64, 8);
  void* f2 = alloc_alloc(&freelist_alloc, 128, 8);
  void* f3 = alloc_alloc(&freelist_alloc, 64, 8);
  printf("allocated 3 blocks\n");

  alloc_free(&freelist_alloc, f2, 128);
  printf("freed middle block\n");

  void* f4 = alloc_alloc(&freelist_alloc, 100, 8);
  printf("allocated new block (should fit in freed space)\n");

  return 0;
}
