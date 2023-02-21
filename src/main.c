#include "mem.h"
#include "mem_internals.h"
#include "util.h"
#include <stdint.h>

#define HEAP_INIT_SIZE 4096

static struct block_header *block_get_header(void *contents) {
    return (struct block_header *) (((uint8_t *) contents) - offsetof(struct block_header, contents));
}

void *heap;

void test_0_heap_creation() {
    heap = heap_init(HEAP_INIT_SIZE);
    if (heap == NULL) {
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 0: heap initialization didn't happen");
    }
    printf("Test 0 succeed \n");
    printf("Heap final: \n");
    debug_heap(stdout, heap);
    munmap(heap, HEAP_INIT_SIZE);
}

void test_1_malloc() {
    heap = heap_init(HEAP_INIT_SIZE);
    if (heap == NULL) {
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 1: Heap initialization didn't happen");
    }
    void *alloc = _malloc(HEAP_INIT_SIZE / 2);
    if (alloc == NULL) {
        debug_heap(stderr, heap);
        err("Failed test 1: Couldn't allocate memory");
    }
    puts("Heap before freeing \n");
    debug_heap(stdout, heap);
    _free(alloc);
    puts("Heap after freeing \n");
    debug_heap(stdout, heap);
    printf("Test 1 succeed");
    munmap(heap, HEAP_INIT_SIZE);
}

void test_2_free_one_region() {
    heap = heap_init(HEAP_INIT_SIZE);
    if (heap == NULL) {
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 2: Heap initialization didn't happen");
    }
    void *alc0 = _malloc(1024);
    void *alc1 = _malloc(1024);
    if (alc0 == NULL || alc1 == NULL) {
        printf("Heap on fail \n");
        debug_heap(stderr, heap);
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 2: Couldn't allocate memory");
    }
    printf("Heap before freeing \n");
    debug_heap(stdout, heap);
    _free(alc0);
    if (alc1 == NULL) {
        debug_heap(stderr, heap);
        err("Failed test 2: Freeing one region damages the other");
    }
    _free(alc1);
    printf("Heap after freeing \n");
    debug_heap(stdout, heap);
    printf("Test 2 succeed");
    munmap(heap, HEAP_INIT_SIZE);
}

void test_3_mem_end() {
    heap = heap_init(HEAP_INIT_SIZE);
    if (heap == NULL) {
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 3: Heap initialization didn't happen");
    }
    void *allocated0 = _malloc(HEAP_INIT_SIZE);
    void *allocated1 = _malloc(HEAP_INIT_SIZE);
    struct block_header *header0 = block_get_header(allocated0);
    struct block_header *header1 = block_get_header(allocated1);
    if (header0->next != header1) {
        munmap(heap, size_from_capacity((block_capacity){.bytes = 4096}).bytes);
        err("Failed test 3: Headers are not linked");
    }
    _free(allocated0);
    _free(allocated1);
    printf("Test 3 succeed \n");
    munmap(heap, HEAP_INIT_SIZE);
}

void test_4_several_blocks() {
    heap = heap_init(HEAP_INIT_SIZE);
    if (heap == NULL) {
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 4: Heap initialization didn't happen");
    }
    printf("Heap before allocating \n");
    debug_heap(stdout, heap);
    void *allocated0 = _malloc(HEAP_INIT_SIZE);
    void *allocated1 = _malloc(HEAP_INIT_SIZE*4);
    printf("Heap after allocating \n");
    debug_heap(stdout, heap);
    struct block_header *block_0 = block_get_header(allocated0);
    struct block_header *block_1 = block_get_header(allocated1);
    if (block_0 == NULL || block_0->next != block_1) {
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 4: Blocks damaged or not linked");
    }

    printf("Test 4 succeed \n");
    _free(allocated0);
    _free(allocated1);
    munmap(heap, HEAP_INIT_SIZE);
}

void test_5_another_heap() {
    heap = heap_init(HEAP_INIT_SIZE);
    if (heap == NULL) {
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 5: Heap initialization didn't happen");
    }
    void *allocated0 = _malloc(HEAP_INIT_SIZE*3);
    if (allocated0 == NULL){
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 5: First allocating gone wrong");
    }
    struct block_header *header0 = block_get_header(allocated0);
    if (header0 == NULL){
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 5: Couldn't get first header");
    }
    struct block_header *next_block = header0->next;
    printf("Heap after allocating \n");
    debug_heap(stdout, header0);
    void *reg = mmap(next_block->contents + next_block->capacity.bytes, 256, PROT_READ | PROT_WRITE, MAP_PRIVATE | 0x20, -1, 0);
    if (reg == NULL) {
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 5: Couldn't map region");
    }
    printf("Heap after mapping \n");
    debug_heap(stdout, header0);
    void *allocated1 = _malloc(1024*4);
    if (allocated1 == NULL){
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 5: Second allocating gone wrong");
    }
    struct block_header *header1 = block_get_header(allocated1);
    if (header1 == NULL){
        munmap(heap, HEAP_INIT_SIZE);
        err("Failed test 5: Couldn't get second header");
    }
    if (!(header0->is_free || header1->is_free)) {
        _free(allocated0);
        _free(allocated1);
    }
    debug_heap(stdout, header0);
    printf("Test 5 succeed \n");
    munmap(heap, HEAP_INIT_SIZE);
}


int main() {
    printf("Running test 0 \n");
    test_0_heap_creation();
    printf("===============\n");
    printf("Running test 1 \n");
    test_1_malloc();
    printf("===============\n");
    printf("Running test 2 \n");
    test_2_free_one_region();
    printf("===============\n");
    printf("Running test 3 \n");
    test_3_mem_end();
    printf("===============\n");
    printf("Running test 4 \n");
    test_4_several_blocks();
    printf("===============\n");
    printf("Running test 5 \n");
    test_5_another_heap();
    printf("===============\n");
    return 0;
}

