#define _DEFAULT_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mem_internals.h"
#include "mem.h"
#include "util.h"

void debug_block(struct block_header* b, const char* fmt, ... );
void debug(const char* fmt, ... );

extern inline block_size size_from_capacity( block_capacity cap );
extern inline block_capacity capacity_from_size( block_size sz );

static bool            block_is_big_enough( size_t query, struct block_header* block ) { return block->capacity.bytes >= query; }
static size_t          pages_count   ( size_t mem )                      { return mem / getpagesize() + ((mem % getpagesize()) > 0); }
static size_t          round_pages   ( size_t mem )                      { return getpagesize() * pages_count( mem ) ; }

static void block_init( void* restrict addr, block_size block_sz, void* restrict next ) {
  *((struct block_header*)addr) = (struct block_header) {
    .next = next,
    .capacity = capacity_from_size(block_sz),
    .is_free = true
  };
}

static size_t region_actual_size( size_t query ) { return size_max( round_pages( query ), REGION_MIN_SIZE ); }

extern inline bool region_is_invalid( const struct region* r );



static void* map_pages(void const* addr, size_t length, int additional_flags) {
  return mmap( (void*) addr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | additional_flags , -1, 0 );
}

/*  аллоцировать регион памяти и инициализировать его блоком */
static struct region alloc_region  ( void const * addr, size_t query ) {
    size_t act_size = region_actual_size(size_from_capacity((block_capacity) {.bytes = query}).bytes);
    void* area = map_pages(addr, act_size, MAP_FIXED_NOREPLACE);
    if (area == MAP_FAILED) {
        area = map_pages(addr, act_size, 0);
    }
    if (area != MAP_FAILED) {
        struct region allocated_region = (struct region){.addr = area, .size = act_size, .extends = (area == addr)};
        block_init(area, (block_size){.bytes = act_size}, NULL);
        return allocated_region;
    }
    return REGION_INVALID;
}



static void* block_after( struct block_header const* block )         ;

void* heap_init( size_t initial ) {
  const struct region region = alloc_region( HEAP_START, initial );
  if ( region_is_invalid(&region) ) return NULL;

  return region.addr;
}

#define BLOCK_MIN_CAPACITY 24

/*  --- Разделение блоков (если найденный свободный блок слишком большой )--- */

static bool block_splittable( struct block_header* restrict block, size_t query) {
  return block-> is_free && query + offsetof( struct block_header, contents ) + BLOCK_MIN_CAPACITY <= block->capacity.bytes;
}

static bool split_if_too_big( struct block_header* block, size_t query ) {
  if (block_splittable(block, query)){
      void* query_offset = block->contents + query;
      block_size new_block_size = (block_size){.bytes=(block->capacity.bytes-query)};
      block_init(query_offset, new_block_size, block->next);
      block->capacity.bytes = query;
      block->next = query_offset;
      return true;
  }
  return false;
}


/*  --- Слияние соседних свободных блоков --- */

static void* block_after( struct block_header const* block )              {
  return  (void*) (block->contents + block->capacity.bytes);
}
static bool blocks_continuous (
                               struct block_header const* fst,
                               struct block_header const* snd ) {
  return (void*)snd == block_after(fst);
}

static bool mergeable(struct block_header const* restrict fst, struct block_header const* restrict snd) {
  return fst->is_free && snd->is_free && blocks_continuous( fst, snd ) ;
}

static bool try_merge_with_next( struct block_header* block ) {
  if(block->next == NULL || !mergeable(block, block->next)) return false;
  struct block_header* nxt = block->next;
  block->capacity.bytes += size_from_capacity(nxt->capacity).bytes;
  block->next = nxt->next;
  return true;
}


/*  --- ... ecли размера кучи хватает --- */

struct block_search_result {
  enum {BSR_FOUND_GOOD_BLOCK, BSR_REACHED_END_NOT_FOUND, BSR_CORRUPTED} type;
  struct block_header* block;
};


static struct block_search_result find_good_or_last  ( struct block_header* restrict block, size_t sz ){
    if (block == NULL) return (struct block_search_result) {.type = BSR_CORRUPTED,.block = NULL};
    bool merged = true;
    while (block != NULL){
        do {
            merged = try_merge_with_next(block);
        } while(merged);
        if (block->is_free && block_is_big_enough(sz, block)){
            return (struct block_search_result) {.type = BSR_FOUND_GOOD_BLOCK, .block = block};
        }
        if (block->next == NULL) {
            return (struct block_search_result) {.type = BSR_REACHED_END_NOT_FOUND, .block = block};
        }
        block = block->next;
    }
    return (struct block_search_result) {.type = BSR_REACHED_END_NOT_FOUND, .block = NULL};
}

/*  Попробовать выделить память в куче начиная с блока `block` не пытаясь расширить кучу
 Можно переиспользовать как только кучу расширили. */
static struct block_search_result try_memalloc_existing ( size_t query, struct block_header* block ) {
  struct block_search_result searched = find_good_or_last(block, query);
  if (searched.type == BSR_FOUND_GOOD_BLOCK){
      split_if_too_big(searched.block, query);
      searched.block->is_free = false;
  }
  return searched;
}



static struct block_header* grow_heap( struct block_header* restrict last, size_t query ) {
    if (!last) return NULL;
    struct region new = alloc_region(block_after(last), query);
    if (!region_is_invalid(&new)){
        last->next = new.addr;
        if (try_merge_with_next(last)) return last;
        return last->next;
    }
    return NULL;
}

/*  Реализует основную логику malloc и возвращает заголовок выделенного блока */
static struct block_header* memalloc( size_t query, struct block_header* heap_start) {
    size_t new_query = size_max(BLOCK_MIN_CAPACITY, query);
    struct block_search_result searched = try_memalloc_existing(new_query,heap_start);
    switch (searched.type) {
        default: return NULL;
        case BSR_FOUND_GOOD_BLOCK: return searched.block;
        case BSR_CORRUPTED: return NULL;
        case BSR_REACHED_END_NOT_FOUND:
            searched.block = grow_heap(searched.block, new_query);
            searched = try_memalloc_existing(new_query, searched.block);
            if (searched.block != NULL) searched.block->is_free = false;
            return searched.block;
    }
}

void* _malloc( size_t query ) {
  struct block_header* const addr = memalloc( query, (struct block_header*) HEAP_START );
  if (addr) return addr->contents;
  else return NULL;
}

static struct block_header* block_get_header(void* contents) {
  return (struct block_header*) (((uint8_t*)contents)-offsetof(struct block_header, contents));
}

void _free( void* mem ) {
  if (!mem) return ;
  struct block_header* header = block_get_header( mem );
  header->is_free = true;
  bool merged_flag = try_merge_with_next(header);
  while(merged_flag){
      merged_flag = try_merge_with_next(header);
  }
}
