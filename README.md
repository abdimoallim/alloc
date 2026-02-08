### alloc

A header-only C allocator library providing various memory allocation strategies through a unified interface. All allocators implement the same `allocator` interface with the state is also visible and controllable. There are no dependencies besides libc. Performance characteristics are predictable and the provided allocators are composable.

### Allocators

#### C Allocator

Standard malloc/free wrapper with alignment support.

```c
allocator* alloc = c_allocator();
int* ptr = (int*)alloc_create(alloc, sizeof(int));
alloc_destroy(alloc, ptr, sizeof(int));
```

#### Arena Allocator

Fast bump allocator that allocates from a fixed buffer. Free all at once.

```c
uint8_t buffer[4096];
arena_allocator arena;
arena_allocator_init(&arena, buffer, sizeof(buffer));
allocator alloc = arena_allocator_get(&arena);

int* data = (int*)alloc_create(&alloc, sizeof(int));
arena_allocator_reset(&arena);
```

#### Pool Allocator

Fixed-size chunk allocator with $O(1)$ allocation and deallocation.

```c
uint8_t buffer[1024];
pool_allocator pool;
pool_allocator_init(&pool, buffer, 64, 16);
allocator alloc = pool_allocator_get(&pool);

void* chunk = alloc_alloc(&alloc, 64, 8);
alloc_free(&alloc, chunk, 64);
```

#### Stack Allocator

LIFO allocator with save/restore markers for scoped allocations.

```c
uint8_t buffer[2048];
stack_allocator stack;
stack_allocator_init(&stack, buffer, sizeof(buffer));
allocator alloc = stack_allocator_get(&stack);

stack_marker mark = stack_allocator_mark(&stack);
int* temp = (int*)alloc_create(&alloc, sizeof(int));
stack_allocator_restore(&stack, mark);
```

#### Scratch Allocator

Temporary allocator that tracks all allocations for bulk freeing.

```c
scratch_allocator scratch;
scratch_allocator_init(&scratch, c_allocator());
allocator alloc = scratch_allocator_get(&scratch);

int* arr1 = (int*)alloc_create_array(&alloc, 100, sizeof(int));
int* arr2 = (int*)alloc_create_array(&alloc, 200, sizeof(int));

scratch_allocator_reset(&scratch);
scratch_allocator_destroy(&scratch);
```

#### Freelist Allocator

General-purpose allocator managing free blocks with coalescing.

```c
uint8_t buffer[8192];
freelist_allocator freelist;
freelist_allocator_init(&freelist, buffer, sizeof(buffer));
allocator alloc = freelist_allocator_get(&freelist);

void* ptr = alloc_alloc(&alloc, 256, 8);
alloc_free(&alloc, ptr, 256);
```

### API

#### Core Interface

```c
void* alloc_alloc(allocator* a, size_t size, size_t alignment);
void* alloc_realloc(allocator* a, void* ptr, size_t old_size, size_t new_size, size_t alignment);
void alloc_free(allocator* a, void* ptr, size_t size);
```

#### Helper Functions

```c
void* alloc_create(allocator* a, size_t size);
void* alloc_create_array(allocator* a, size_t count, size_t elem_size);
void alloc_destroy(allocator* a, void* ptr, size_t size);
```

### Usage

Include the [`alloc.h`](/alloc.h) header in your program.

### License

Apache v2.0 License
