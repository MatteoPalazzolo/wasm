#define EXPORT(name) __attribute__((export_name(name)))

#define HEAP_SIZE 1024

#define size_t unsigned int
#define NULL ((void *)0)

// clang --target=wasm32 -ffreestanding -nostdlib -O2 "-Wl,--no-entry" -o math.wasm math.c
// clang --target=wasm32 -ffreestanding -nostdlib -O2 "-Wl,--no-entry" -o demo/math.wasm demo/math.c

static char heap[HEAP_SIZE];
static size_t heap_pointer = 0;

EXPORT("add")
int add(int a, int b) {
    return a + b;
}

EXPORT("alloc")
void *alloc(size_t size) {
    // forma preferibile col meno per prevenire overflow
    if (size > HEAP_SIZE - heap_pointer)
        return NULL;
    void *ptr = heap + heap_pointer;
    heap_pointer += size;
    return ptr;
}

EXPORT("clear_heap")
void clear_heap(void) {
    heap_pointer = 0;
}

EXPORT("merge_strings")
char *merge_strings(const char *a, const char *b) {
    
    size_t size_a = 0;
    while(a[size_a] != '\0') size_a++;

    size_t size_b = 0;
    while(b[size_b] != '\0') size_b++;

    char *c = alloc(size_a + size_b + 1);
    if (c == NULL) return NULL;

    size_t c_counter = 0;
    
    for (size_t i = 0; i < size_a; i++) {
        c[c_counter++] = a[i];
    }
    
    for (size_t i = 0; i < size_b; i++) {
        c[c_counter++] = b[i];
    }

    c[c_counter] = '\0';
    
    return c;
}