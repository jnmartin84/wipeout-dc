#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "alloc.h"


/* This allocator is designed so that ideally all allocations larger
 * than 2k, fall on a 2k boundary. Smaller allocations will
 * never cross a 2k boundary.
 *
 * House keeping is stored in RAM to avoid reading back from the
 * VRAM to check for usage. Headers can't be easily stored in the
 * blocks anyway as they have to be 2k aligned (so you'd need to
 * store them in reverse or something)
 *
 * Defragmenting the pool will move larger allocations first, then
 * smaller ones, recursively until you tell it to stop, or until things
 * stop moving.
 *
 * The maximum pool size is 8M, made up of:
 *
 * - 4096 blocks of 2k
 * - each with 8 sub-blocks of 256 bytes
 *
 * Why?
 *
 * The PVR performs better if textures don't cross 2K memory
 * addresses, so we try to avoid that. Obviously we can't
 * if the allocation is > 2k, but in that case we can at least
 * align with 2k and the VQ codebook (which is usually 2k) will
 * be in its own page.
 *
 * The smallest PVR texture allowed is 8x8 at 16 bit (so 128 bytes)
 * but we're unlikely to use too many of those, so having a min sub-block
 * size of 256 should be OK (a 16x16 image is 512, so two sub-blocks).
 *
 * We could go down to 128 bytes if wastage is an issue, but then we have
 * to store double the number of usage markers.
 *
 * FIXME:
 *
 *  - Only operates on one pool (ignores what you pass)
 */

#include <assert.h>
#include <stdio.h>

#define EIGHT_MEG (8 * 1024 * 1024)
#define TWO_KILOBYTES (2 * 1024)
#define BLOCK_COUNT (EIGHT_MEG / TWO_KILOBYTES)

#define ALLOC_DEBUG 0
#if ALLOC_DEBUG
#define DBG_MSG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DBG_MSG(fmt, ...) do {} while (0)
#endif


static inline intptr_t round_up(intptr_t n, int multiple)
{
    if((n % multiple) == 0) {
        return n;
    }

    assert(multiple);
    return ((n + multiple - 1) / multiple) * multiple;
}

struct AllocEntry {
    void* pointer;
    size_t size;
    struct AllocEntry* next;
};


typedef struct {
    /* This is a usage bitmask for each block. A block
     * is divided into 8 x 256 byte subblocks. If a block
     * is entirely used, it's value will be 255, if
     * it's entirely free then it will be 0.
     */
    uint8_t block_usage[BLOCK_COUNT];
    uint8_t* pool;  // Pointer to the memory pool
    size_t pool_size; // Size of the memory pool
    uint8_t* base_address; // First 2k aligned address in the pool
    size_t block_count;  // Number of 2k blocks in the pool

    /* It's frustrating that we need to do this dynamically
     * but we need to know the size allocated when we free()...
     * we could store it statically but it would take 64k if we had
     * an array of block_index -> block size where there would be 2 ** 32
     * entries of 16 bit block sizes. The drawback (aside the memory usage)
     * would be that we won't be able to order by size, so defragging will
     * take much more time.*/
    struct AllocEntry* allocations;
} PoolHeader;


static PoolHeader pool_header = {
    {0}, NULL, 0, NULL, 0, NULL
};

void* alloc_base_address(void* pool) {
    (void) pool;
    return pool_header.base_address;
}

size_t alloc_block_count(void* pool) {
    (void) pool;
    return pool_header.block_count;
}

static inline void* calc_address(
    uint8_t* block_usage_iterator,
    int bit_offset,
    size_t required_subblocks,
    size_t* start_subblock_out
) {
    uintptr_t offset = (block_usage_iterator - pool_header.block_usage) * 8;
    offset += (bit_offset + 1);
    offset -= required_subblocks;

    if(start_subblock_out) {
        *start_subblock_out = offset;
    }

    return pool_header.base_address + (offset * 256);
}

void* alloc_next_available_ex(void* pool, size_t required_size, size_t* start_subblock, size_t* required_subblocks);

void* alloc_next_available(void* pool, size_t required_size) {
    return alloc_next_available_ex(pool, required_size, NULL, NULL);
}

void* alloc_next_available_ex(void* pool, size_t required_size, size_t* start_subblock_out, size_t* required_subblocks_out) {
    (void) pool;

    uint8_t* it = pool_header.block_usage;
    uint32_t required_subblocks = (required_size / 256);
    if(required_size % 256) required_subblocks += 1;

    /* Anything gte to 2048 must be aligned to a 2048 boundary */
    bool requires_alignment = required_size >= 2048;

    if(required_subblocks_out) {
        *required_subblocks_out = required_subblocks;
    }

    /* This is a fallback option. If while we're searching we find a possible slot
     * but it's not aligned, or it's straddling a 2k boundary, then we store
     * it here and if we reach the end of the search and find nothing better
     * we use this instead */
    uint8_t* poor_option = NULL;
    size_t poor_start_subblock = 0;

    uint32_t found_subblocks = 0;
    uint32_t found_poor_subblocks = 0;

    for(size_t j = 0; j < pool_header.block_count; ++j, ++it) {
        /* We just need to find enough consecutive blocks */
        if(found_subblocks < required_subblocks) {
            uint8_t t = *it;

            /* Optimisation only. Skip over full blocks */
            if(t == 255) {
                found_subblocks = 0;
                found_poor_subblocks = 0;
            } else {
                /* Now let's see how many consecutive blocks we can find */
                for(int i = 0; i < 8; ++i) {
                    if((t & 0x80) == 0) {
                        bool block_overflow = (
                            required_size < 2048 && found_subblocks > 0 && i == 0
                        );

                        bool reset_subblocks = (
                            (requires_alignment && found_subblocks == 0 && i != 0) ||
                            block_overflow
                        );

                        if(reset_subblocks) {
                            // Ignore this subblock, because we want the first subblock to be aligned
                            // at a 2048 boundary and this one isn't (i != 0)
                            found_subblocks = 0;
                        } else {
                            found_subblocks++;
                        }

                        /* If we reset the subblocks due to an overflow, we still
                         * want to count this free subblock in our count */
                        if(block_overflow) {
                            found_subblocks++;
                        }

                        found_poor_subblocks++;

                        if(found_subblocks >= required_subblocks) {
                            /* We found space! Now calculate the address */
                            return calc_address(it, i, required_subblocks, start_subblock_out);
                        }

                        if(!poor_option && (found_poor_subblocks >= required_subblocks)) {
                            poor_option = calc_address(it, i, required_subblocks, &poor_start_subblock);
                        }

                    } else {
                        found_subblocks = 0;
                        found_poor_subblocks = 0;
                    }

                    t <<= 1;
                }
            }
        }
    }

    if(poor_option) {
        if(start_subblock_out) {
            *start_subblock_out = poor_start_subblock;
        }

        return poor_option;
    } else {
        return NULL;
    }
}

int alloc_init(void* pool, size_t size) {
    (void) pool;

    if(pool_header.pool) {
        return -1;
    }

    if(size > EIGHT_MEG) {  // FIXME: >= ?
        return -1;
    }

    uint8_t* p = (uint8_t*) pool;

    memset(pool_header.block_usage, 0, BLOCK_COUNT);
    pool_header.pool = pool;

    intptr_t base_address = (intptr_t) pool_header.pool;
    base_address = round_up(base_address, 2048);

    pool_header.base_address = (uint8_t*) base_address;
    pool_header.block_count = ((p + size) - pool_header.base_address) / 2048;

    /* The pool size might be less than the passed size if the memory
     * wasn't aligned to 2048 */
    pool_header.pool_size = pool_header.block_count * 2048;

    pool_header.allocations = NULL;

    assert(((uintptr_t) pool_header.base_address) % 2048 == 0);

    return 0;
}

void alloc_shutdown(void* pool) {
    (void) pool;

    if(!pool_header.pool) {
        return;
    }

    struct AllocEntry* it = pool_header.allocations;
    while(it) {
        struct AllocEntry* next = it->next;
        free(it);
        it = next;
    }

    memset(&pool_header, 0, sizeof(pool_header));
    pool_header.pool = NULL;
}

static inline uint32_t size_to_subblock_count(size_t size) {
    uint32_t required_subblocks = (size / 256);
    if(size % 256) required_subblocks += 1;
    return required_subblocks;
}

static inline uint32_t subblock_from_pointer(void* p) {
    uint8_t* ptr = (uint8_t*) p;
    return (ptr - pool_header.base_address) / 256;
}

static inline void block_and_offset_from_subblock(size_t sb, size_t* b, uint8_t* off) {
    *b = sb / 8;
    *off = (sb % 8);
}

static void* alloc_malloc_internal(void* pool, size_t size, bool for_defrag) {
    DBG_MSG("Allocating: %d\n", size);

    size_t start_subblock, required_subblocks;
    void* ret = alloc_next_available_ex(pool, size, &start_subblock, &required_subblocks);

    if(ret) {
        size_t block;
        uint8_t offset;

        block_and_offset_from_subblock(start_subblock, &block, &offset);

        uint8_t mask = 0;

        DBG_MSG("Alloc: size: %d, rs: %d, sb: %d, b: %d, off: %d\n", size, required_subblocks, start_subblock, start_subblock / 8, start_subblock % 8);

        /* Toggle any bits for the first block */
        int c = (required_subblocks < 8) ? required_subblocks : 8;
        for(int i = 0; i < c; ++i) {
            mask |= (1 << (7 - (offset + i)));
            required_subblocks--;
        }

        if(mask) {
            pool_header.block_usage[block++] |= mask;
        }

        /* Fill any full blocks in the middle of the allocation */
        while(required_subblocks > 8) {
            pool_header.block_usage[block++] = 255;
            required_subblocks -= 8;
        }

        /* Fill out any trailing subblocks */
        mask = 0;
        for(size_t i = 0; i < required_subblocks; ++i) {
            mask |= (1 << (7 - i));
        }

        if(mask) {
            pool_header.block_usage[block++] |= mask;
        }

        // defrag allocations don't create new entries, they reuse old ones
        if(for_defrag) {
            return ret;
        }

        /* Insert allocations in the list by size descending so that when we
         * defrag we can move the larger blocks before the smaller ones without
         * much effort */
        struct AllocEntry* new_entry = (struct AllocEntry*) malloc(sizeof(struct AllocEntry));
        new_entry->pointer = ret;
        new_entry->size = size;
        new_entry->next = NULL;

        struct AllocEntry* it = pool_header.allocations;
        struct AllocEntry* last = NULL;

        if(!it) {
            pool_header.allocations = new_entry;
        } else {
            while(it) {
                if(it->size < size) {
                    if(last) {
                        last->next = new_entry;
                    } else {
                        pool_header.allocations = new_entry;
                    }

                    new_entry->next = it;
                    break;
                } else if(!it->next) {
                    it->next = new_entry;
                    new_entry->next = NULL;
                    break;
                }

                last = it;
                it = it->next;
            }
        }
    }

    DBG_MSG("Alloc done\n");

    return ret;
}

void* alloc_malloc(void* pool, size_t size) {
    return alloc_malloc_internal(pool, size, false);
}

static void alloc_release_blocks(struct AllocEntry* it) {
    size_t used_subblocks = size_to_subblock_count(it->size);
    size_t subblock = subblock_from_pointer(it->pointer);
    size_t block;
    uint8_t offset;
    block_and_offset_from_subblock(subblock, &block, &offset);

    uint8_t mask = 0;

    DBG_MSG("Free: size: %d, us: %d, sb: %d, off: %d\n", it->size, used_subblocks, block, offset);

    /* Wipe out any leading subblocks */
    int c = (used_subblocks < 8) ? used_subblocks : 8;
    for(int i = 0; i < c; ++i) {
        mask |= (1 << (7 - (offset + i)));
        used_subblocks--;
    }

    if(mask) {
        pool_header.block_usage[block++] &= ~mask;
    }

    /* Clear any full blocks in the middle of the allocation */
    while(used_subblocks > 8) {
        pool_header.block_usage[block++] = 0;
        used_subblocks -= 8;
    }

    /* Wipe out any trailing subblocks */
    mask = 0;
    for(size_t i = 0; i < used_subblocks; ++i) {
        mask |= (1 << (7 - i));
    }

    if(mask) {
        pool_header.block_usage[block++] &= ~mask;
    }
}

void alloc_free(void* pool, void* p) {
    (void) pool;

    struct AllocEntry* it = pool_header.allocations;
    struct AllocEntry* last = NULL;
    while(it) {
        if(it->pointer == p) {
            alloc_release_blocks(it);

            if(last) {
                last->next = it->next;
            } else {
                assert(it == pool_header.allocations);
                pool_header.allocations = it->next;
            }

            DBG_MSG("Freed: size: %d, us: %d, sb: %d, off: %d\n", it->size, used_subblocks, block, offset);
            free(it);
            return;
        }

        last = it;
        it = it->next;
    }

    assert("Freed pointer not found, heap corruption?" && 0);
}

void alloc_run_defrag(void* pool, defrag_address_move callback, int max_iterations, void* user_data) {

    for(int i = 0; i < max_iterations; ++i) {
        bool move_occurred = false;

        struct AllocEntry* it = pool_header.allocations;

        if(!it) {
            return;
        }

        while(it) {
            void* potential_dest = alloc_next_available(pool, it->size);
            if(potential_dest && potential_dest < it->pointer) {
                potential_dest = alloc_malloc_internal(pool, it->size, true);
                memcpy(potential_dest, it->pointer, it->size);

                /* Mark this block as now free, but don't fiddle with the
                 * allocation list */
                alloc_release_blocks(it);

                callback(it->pointer, potential_dest, user_data);

                it->pointer = potential_dest;
                move_occurred = true;
            }

            it = it->next;
        }

        if(!move_occurred) {
            return;
        }
    }
}

static inline uint8_t count_ones(uint8_t byte) {
    static const uint8_t NIBBLE_LOOKUP [16] = {
        0, 1, 1, 2, 1, 2, 2, 3,
        1, 2, 2, 3, 2, 3, 3, 4
    };
    return NIBBLE_LOOKUP[byte & 0x0F] + NIBBLE_LOOKUP[byte >> 4];
}

size_t alloc_count_free(void* pool) {
    (void) pool;

    size_t total_used = 0;

    for(size_t i = 0; i < pool_header.block_count; ++i) {
        total_used += count_ones(pool_header.block_usage[i]) * 256;
    }

    return pool_header.pool_size - total_used;
}

size_t alloc_count_continuous(void* pool) {
    (void) pool;

    size_t contiguous_free = 0;
    size_t most_contiguous = 0;

    for(size_t i = 0; i < pool_header.block_count; ++i) {
        uint8_t t = pool_header.block_usage[i];

        for(int i = 7; i >= 0; --i) {
            bool bitset = (t & (1 << i));
            if(!bitset) {
                ++contiguous_free;
            } else {
                if(contiguous_free > most_contiguous) {
                    most_contiguous = contiguous_free;
                }
                contiguous_free = 0;
            }
        }
    }

    if(contiguous_free > most_contiguous) {
        most_contiguous = contiguous_free;
    }
    return most_contiguous * 256;
}