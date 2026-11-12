/* Host check for the production block-classification callback. The build
 * extracts its unchanged types and callback into build/ram_heap_walk.inc. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define configMAX_TASK_NAME_LEN 16
typedef unsigned UBaseType_t;
typedef struct { void *pxStackBase; void *xHandle; } TaskStatus_t;
typedef struct { intptr_t start, end; } walker_heap_into_t;
typedef struct { void *ptr; size_t size; bool used; } walker_block_info_t;
#include "ram_heap_walk.inc"

static void visit(task_snapshot_t *s, uintptr_t ptr, size_t bytes, bool used)
{
    assert(SystemHeapBlockVisit((walker_heap_into_t){0},
           (walker_block_info_t){(void *)ptr, bytes, used}, s));
}

int main(void)
{
    task_snapshot_t s = {0};
    s.count = 2;
    s.status[0].pxStackBase = (void *)0x1008;
    s.status[0].xHandle = (void *)0x3000;
    s.status[1].pxStackBase = (void *)0x3c001000; /* External, absent in walk. */
    visit(&s, 0x1000, 1536, true);
    visit(&s, 0x3000, 360, true);
    visit(&s, 0x4000, 4096, false);
    visit(&s, 0x4000, 0, true);
    visit(&s, 0x5000, 64, true);
    visit(&s, 0x6000, 65, true);
    visit(&s, 0x7000, 16385, true);
    assert(s.stack_bytes[0] == 1536 && s.stack_bytes[1] == 0);
    assert(s.tcb_bytes[0] == 360);
    assert(s.used_bytes == 1536 + 360 + 64 + 65 + 16385);
    assert(s.other_bytes == 64 + 65 + 16385 && s.other_blocks == 3);
    assert(s.bin_bytes[0] == 64 && s.bin_bytes[1] == 65);
    assert(s.bin_bytes[HEAP_DETAIL_BINS - 1] == 16385);
    assert(s.top[0].size == 16385 && s.top[1].size == 65 && s.top[2].size == 64);
    for (unsigned i = 0; i < 30; ++i) visit(&s, 0x10000 + i * 0x1000, 100 + i, true);
    size_t histogram = 0;
    for (unsigned i = 0; i < HEAP_DETAIL_BINS; ++i) histogram += s.bin_bytes[i];
    assert(histogram == s.other_bytes);
    assert(s.used_bytes == s.other_bytes + s.stack_bytes[0] + s.tcb_bytes[0]);
    for (unsigned i = 1; i < HEAP_DETAIL_TOP; ++i) assert(s.top[i-1].size >= s.top[i].size);
    assert(s.top[0].size == 16385 && s.top[HEAP_DETAIL_TOP - 1].size == 119);
    task_snapshot_t nested = {0};
    walker_heap_into_t parent_heap = {0x20000, 0x40000};
    walker_heap_into_t child_heap = {0x21000, 0x28fff};
    assert(SystemHeapRegionVisit(parent_heap, (walker_block_info_t){0}, &nested));
    assert(SystemHeapRegionVisit(child_heap, (walker_block_info_t){0}, &nested));
    assert(SystemHeapRegionVisit(child_heap, (walker_block_info_t){0}, &nested));
    assert(nested.region_count == 2);
    assert(SystemHeapBlockVisit(parent_heap,
           (walker_block_info_t){(void *)0x21000, 32768, true}, &nested));
    assert(SystemHeapBlockVisit(child_heap,
           (walker_block_info_t){(void *)0x21100, 2048, true}, &nested));
    assert(nested.backing_bytes == 32768);
    assert(nested.used_bytes == 2048 && nested.other_bytes == 2048);
    puts("RAM heap walk classification: PASS");
    return 0;
}
