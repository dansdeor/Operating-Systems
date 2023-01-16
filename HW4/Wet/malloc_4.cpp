#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define SIZE_LIMIT (100000000)
#define SBRK_LIMIT (131072) // 128 KB
#define HUGE_PAGE_LIMIT (4194304) // 4MB
#define SCALLOC_HUGE_PAGE_LIMIT (2097152) // 2MB
#define REDUNDANT_SIZE ((128 + _size_meta_data()))
#define IS_REDUNDANT(block, block_size) ((block)->size - (block_size) >= REDUNDANT_SIZE)
#define TAIL_METADATA(block) ((tail_metadata_t*)((void*)(block) + (block)->size - sizeof(tail_metadata_t)))
#define IS_SBRK_ALLOC(block) (sbrk_head <= (block) && ((void*)(block)) <= sbrk(0))
#define ALLOC_SBRK(block_size) ((block_size) < SBRK_LIMIT)

typedef struct {
    size_t size;
    size_t is_free;
    head_metadata_t* next;
    head_metadata_t* prev;
} head_metadata_t;

typedef struct {
    uint32_t cookie;
    size_t size;
} tail_metadata_t;

uint32_t global_rand_cookie = 0;
head_metadata_t* sbrk_head = NULL;
head_metadata_t* sbrk_free_head = NULL;

size_t free_blocks_num = 0;
size_t free_bytes_num = 0;
size_t allocated_blocks_num = 0;
size_t allocated_bytes_num = 0;

// Challenge 7
size_t _8_bit_align(size_t size)
{
    while (size & 0x7) {
        size <<= 1;
    }
    return size;
}

size_t _num_free_blocks()
{
    return free_blocks_num;
}
size_t _num_free_bytes()
{
    return free_bytes_num;
}
size_t _num_allocated_blocks()
{
    return allocated_blocks_num;
}
size_t _num_allocated_bytes()
{
    return allocated_bytes_num;
}
size_t _size_meta_data()
{
    return sizeof(head_metadata_t) + sizeof(tail_metadata_t);
}
size_t _num_meta_data_bytes()
{
    return _size_meta_data() * allocated_blocks_num;
}

// has to be set after setting block head metadata
static void _set_tail(head_metadata_t* block)
{
    if (global_rand_cookie == 0) {
        srand(time(NULL));
    }
    while (global_rand_cookie == 0) {
        global_rand_cookie = rand();
    }
    tail_metadata_t* tail = TAIL_METADATA(block);
    tail->cookie = global_rand_cookie;
    tail->size = block->size;
}
// Challenge 5
static void _check_cookie(head_metadata_t* block)
{
    tail_metadata_t* tail = TAIL_METADATA(block);
    if (global_rand_cookie != 0 && global_rand_cookie != tail->cookie) {
        exit(0xdeadbeef);
    }
}

static head_metadata_t* _init_sbrk_alloc_block(head_metadata_t* block, size_t block_size, bool alloc)
{
    if (alloc) {
        if (sbrk(block_size) == (void*)(-1)) {
            return NULL;
        }
    }
    block->size = block_size;
    block->is_free = false;
    block->next = NULL;
    block->prev = NULL;
    _set_tail(block);
    return block;
}

static head_metadata_t* _wilderness_sbrk_block_increase(head_metadata_t* wilderness, size_t block_size)
{
    size_t delta = block_size - wilderness->size;
    if (sbrk(delta) == (void*)(-1)) {
        return NULL;
    }
    wilderness->size = block_size;
    _set_tail(wilderness);
    return wilderness;
}

// returns the best fit free block if not found returns NULL
static head_metadata_t* _find_sbrk_free_block(size_t block_size)
{
    head_metadata_t* min = NULL;
    head_metadata_t* wilderness = NULL;
    if (sbrk_free_head == NULL) {
        return NULL;
    }
    void* program_break = sbrk(0);
    for (head_metadata_t* current = sbrk_free_head; current != NULL; current = current->next) {
        _check_cookie(current);
        // Challenge 0
        if (current->size >= block_size && (min == NULL || min->size > current->size)) {
            min = current;
        }
        // Challenge 3
        if ((void*)current + current->size == program_break) {
            wilderness = current;
        }
    }
    if (min != NULL) {
        return min;
    }
    if (wilderness != NULL) {
        return _wilderness_sbrk_block_increase(wilderness, block_size);
    }
    return NULL;
}

static void _add_sbrk_free_block(head_metadata_t* block)
{
    block->is_free = true;
    block->prev = NULL;
    if (sbrk_free_head == NULL || block->size < sbrk_free_head->size) {
        head_metadata_t* temp = sbrk_free_head;
        sbrk_free_head = block;
        block->next = temp;
        return;
    }
    head_metadata_t* current;
    _check_cookie(sbrk_free_head);
    for (current = sbrk_free_head; current->next != NULL; current = current->next) {
        head_metadata_t* next = current->next;
        _check_cookie(next);
        if (block->size < next->size) {
            block->prev = current;
            block->next = next;
            next->prev = block;
            current->next = block;
            break;
        }
    }
    block->prev = current;
    block->next = NULL;
    current->next = block;
}

static void _remove_sbrk_free_block(head_metadata_t* block)
{
    block->is_free = false;
    if (sbrk_free_head == block) {
        sbrk_free_head = block->next;
        block->next = NULL;
        block->prev = NULL;
        return;
    }
    head_metadata_t* prev = block->prev;
    head_metadata_t* next = block->next;
    if (prev) {
        _check_cookie(prev);
        prev->next = next;
    }
    if (next) {
        _check_cookie(next);
        next->prev = prev;
    }
    block->next = NULL;
    block->prev = NULL;
}

static void _init_sbrk_free_block(head_metadata_t* block, size_t block_size)
{
    block->size = block_size;
    block->next = NULL;
    block->prev = NULL;
    _set_tail(block);
    _add_sbrk_free_block(block);
}

// Challenge 2
static head_metadata_t* _merge_sbrk_blocks(head_metadata_t* block, bool merge_left = true, bool merge_right = true, bool copy_data = false)
{
    size_t block_size_sum = block->size;
    head_metadata_t* returned_block = block;
    head_metadata_t* left_block = NULL;
    head_metadata_t* right_block = NULL;
    if (merge_left && sbrk_head != block) {
        size_t prev_block_size = ((tail_metadata_t*)((void*)block - sizeof(tail_metadata_t)))->size;
        left_block = (head_metadata_t*)((void*)block - prev_block_size);
        _check_cookie(left_block);
    }
    if (merge_right && ((void*)block + block->size != sbrk(0))) {
        right_block = (head_metadata_t*)((void*)block + block->size);
        _check_cookie(right_block);
    }
    if (left_block && left_block->is_free) {
        returned_block = left_block;
        block_size_sum += left_block->size;
        _remove_sbrk_free_block(left_block);
        if (copy_data) {
            memmove((void*)left_block + sizeof(head_metadata_t), (void*)block + sizeof(head_metadata_t), block->size - _size_meta_data());
        }
    }
    if (right_block && right_block->is_free) {
        block_size_sum += right_block->size;
        _remove_sbrk_free_block(right_block);
    }
    return _init_sbrk_alloc_block(returned_block, block_size_sum, false);
}

static head_metadata_t* _sbrk_malloc(size_t block_size)
{
    static bool first_time = true;
    head_metadata_t* last_block;
    if (!first_time) {
        head_metadata_t* last_searched = _find_sbrk_free_block(block_size);
        if (last_searched) {
            _remove_sbrk_free_block(last_searched);
            // Challenge 1
            if (IS_REDUNDANT(last_searched, block_size)) {
                _init_sbrk_alloc_block(last_searched, block_size, false);
                _init_sbrk_free_block((head_metadata_t*)((void*)last_searched + block_size), last_searched->size - block_size);
            }
            free_blocks_num--;
            free_bytes_num -= last_searched->size - _size_meta_data();
            return last_searched;
        }
    } else {
        sbrk_head = (head_metadata_t*)sbrk(0);
        if (sbrk_head == (head_metadata_t*)(-1)) {
            return NULL;
        }
        first_time = false;
    }
    last_block = (head_metadata_t*)sbrk(0);
    last_block = _init_sbrk_alloc_block(last_block, block_size, true);
    allocated_blocks_num++;
    allocated_bytes_num += block_size - _size_meta_data();
    return last_block;
}

// Challenge 4
static head_metadata_t* _mmap_malloc(size_t block_size, bool force_hugepage = false)
{
    // Challenge 6
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    flags |= (force_hugepage || block_size >= HUGE_PAGE_LIMIT) ? MAP_HUGETLB : 0;
    void* mmap_addr = mmap(NULL, block_size, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (mmap_addr == (void*)(-1)) {
        return NULL;
    }
    head_metadata_t* block = (head_metadata_t*)mmap_addr;
    // We use the sbrk function because it fits our needs (we don't call sbrk of course)
    _init_sbrk_alloc_block(block, block_size, false);
    allocated_blocks_num++;
    allocated_bytes_num += block_size - _size_meta_data();
    return block;
}

void* smalloc(size_t size)
{
    head_metadata_t* block;
    size = _8_bit_align(size);
    size_t block_size = size + _size_meta_data();
    if (size == 0 || block_size > SIZE_LIMIT) {
        return NULL;
    }
    if (ALLOC_SBRK(block_size)) {
        block = _sbrk_malloc(block_size);
    } else {
        _mmap_malloc(block_size);
    }
    return (block) ? ((void*)block + sizeof(head_metadata_t)) : NULL;
}

void* scalloc(size_t num, size_t size)
{
    void* alloc;
    size = _8_bit_align(num * size);
    size_t block_size = size + _size_meta_data();
    if (block_size >= SCALLOC_HUGE_PAGE_LIMIT) {
        head_metadata_t* block = _mmap_malloc(block_size);
        if (block == NULL) {
            return NULL;
        }
        alloc = (void*)block + sizeof(head_metadata_t);
    } else {
        alloc = smalloc(size);
        if (alloc == NULL) {
            return NULL;
        }
    }
    memset(alloc, 0, size);
    return alloc;
}

void sbrk_free(head_metadata_t* block_to_free)
{
    free_blocks_num++;
    free_bytes_num += block_to_free->size - _size_meta_data();
    block_to_free = _merge_sbrk_blocks(block_to_free);
    _add_sbrk_free_block(block_to_free);
}

void mmap_free(head_metadata_t* block_to_free)
{
    munmap((void*)block_to_free, block_to_free->size);
}

void sfree(void* p)
{
    if (p == NULL) {
        return;
    }
    head_metadata_t* block_to_free = (head_metadata_t*)((void*)p - sizeof(head_metadata_t));
    _check_cookie(block_to_free);
    if (block_to_free->is_free) {
        return;
    }
    if (IS_SBRK_ALLOC(block_to_free)) {
        sbrk_free(block_to_free);
    } else {
        mmap_free(block_to_free);
    }
}

static void* _sbrk_realloc(head_metadata_t* block, size_t block_size)
{
    // Try to merge with lower address
    block = _merge_sbrk_blocks(block, true, false, true);
    if (block->size >= block_size) {
        return ((void*)block + sizeof(head_metadata_t));
    }
    void* program_break = sbrk(0);
    // Is wilderness block
    if ((void*)block + block_size == program_break) {
        block = _wilderness_sbrk_block_increase(block, block_size);
        return ((void*)block + sizeof(head_metadata_t));
    }
    // Try to merge with higher address
    block = _merge_sbrk_blocks(block, false, true, false);
    if (block->size >= block_size) {
        return ((void*)block + sizeof(head_metadata_t));
    }
    // Try to merge 3 block all toghether
    block = _merge_sbrk_blocks(block, true, true, true);
    if (block->size >= block_size) {
        return ((void*)block + sizeof(head_metadata_t));
    }
    // Is wilderness block
    if ((void*)block + block_size == program_break) {
        block = _wilderness_sbrk_block_increase(block, block_size);
        return ((void*)block + sizeof(head_metadata_t));
    }
    // If non of the options worked just allocate and copy to new block
    return NULL;
}

void* srealloc(void* oldp, size_t size)
{
    void* newp;
    size = _8_bit_align(size);
    if (oldp == NULL) {
        return smalloc(size);
    }
    head_metadata_t* old_block = (head_metadata_t*)((void*)oldp - sizeof(head_metadata_t));
    size_t block_size = size + _size_meta_data();
    if (size == 0 || block_size > SIZE_LIMIT) {
        return NULL;
    }
    if (old_block->size >= block_size) {
        return oldp;
    }
    if (IS_SBRK_ALLOC(old_block)) {
        newp = _sbrk_realloc(old_block, block_size);
        if (newp) {
            return newp;
        }
    }
    void* newp = smalloc(size);
    if (newp == NULL) {
        return NULL;
    }
    memmove(newp, oldp, old_block->size - _size_meta_data());
    sfree(oldp);
    return newp;
}
