#include <unistd.h>

#define SIZE_LIMIT (100000000)

void* smalloc(size_t size)
{
    if (size == 0 || size > SIZE_LIMIT) {
        return NULL;
    }
    void* prev_program_break = sbrk(size);
    if (prev_program_break == (void*)(-1)) {
        return NULL;
    }
    return prev_program_break;
}
