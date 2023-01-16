#include <cstring>
#include <unistd.h>

#define SIZE_LIMIT (100000000)

typedef struct {
    size_t size;
    size_t is_free;
} head_metadata_t;

size_t free_blocks_num = 0;
size_t free_bytes_num = 0;
size_t allocated_blocks_num = 0;
size_t allocated_bytes_num = 0;

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
size_t _num_meta_data_bytes()
{
    return sizeof(head_metadata_t) * allocated_blocks_num;
}
size_t _size_meta_data()
{
    return sizeof(head_metadata_t);
}

// returns the first fit free block if not found returns the last searched block
static head_metadata_t* _find_free_block(head_metadata_t* head, size_t block_size)
{
    head_metadata_t* last_searched;
    head_metadata_t* program_break = (head_metadata_t*)sbrk(0);
    ;
    for (last_searched = head; last_searched + last_searched->size < program_break; last_searched += last_searched->size) {
        if (last_searched->is_free && last_searched->size >= block_size) {
            return last_searched;
        }
    }
    return last_searched;
}

static head_metadata_t* _init_alloc_block(head_metadata_t* block, size_t block_size)
{
    if (sbrk(block_size) == (void*)(-1)) {
        return NULL;
    }
    block->size = block_size;
    block->is_free = false;
    return block;
}

void* smalloc(size_t size)
{
    static void* global_head = sbrk(0);
    head_metadata_t* last_block;
    size_t block_size = size + _size_meta_data();
    if (size == 0 || block_size > SIZE_LIMIT) {
        return NULL;
    }
    if (_num_allocated_blocks() != 0) {
        head_metadata_t* last_searched = _find_free_block((head_metadata_t*)global_head, block_size);
        if (last_searched->is_free) {
            last_searched->is_free = false;
            free_blocks_num--;
            free_bytes_num -= last_searched->size - _size_meta_data();
            return ((void*)last_searched + sizeof(head_metadata_t));
        }
        // last search block is not free
        last_block = last_searched + last_searched->size;
        last_block = _init_alloc_block(last_block, block_size);
    } else {
        last_block = (head_metadata_t*)global_head;
        last_block = _init_alloc_block(last_block, block_size);
    }
    if (last_block == NULL) {
        return NULL;
    }
    allocated_blocks_num++;
    allocated_bytes_num += size;
    return ((void*)last_block + sizeof(head_metadata_t));
}

void* scalloc(size_t num, size_t size)
{
    void* alloc = smalloc(num * size);
    if (alloc == NULL) {
        return NULL;
    }
    memset(alloc, 0, num * size);
    return alloc;
}

void sfree(void* p)
{
    if (p == NULL) {
        return;
    }
    head_metadata_t* block_to_free = (head_metadata_t*)((void*)p - sizeof(head_metadata_t));
    if (block_to_free->is_free) {
        return;
    }
    block_to_free->is_free = true;
    free_blocks_num++;
    free_bytes_num += block_to_free->size - _size_meta_data();
}

void* srealloc(void* oldp, size_t size)
{
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
    void* newp = smalloc(size);
    if (newp == NULL) {
        return NULL;
    }
    memmove(newp, oldp, old_block->size - _size_meta_data());
    sfree(oldp);
    return newp;
}
