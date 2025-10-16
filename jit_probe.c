#include <sys/mman.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    size_t page = sysconf(_SC_PAGESIZE);
    void* ptr = mmap(NULL, page, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON
#if defined(MAP_JIT)
                     | MAP_JIT
#endif
                     , -1, 0);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        return 1;
    }

    unsigned char* bytes = (unsigned char*)ptr;
    bytes[0] = 0xC3; // ret

    if (mprotect(ptr, page, PROT_READ | PROT_EXEC) != 0) {
        fprintf(stderr, "mprotect failed: %s\n", strerror(errno));
        return 2;
    }

    int (*func)(void) = (int (*)(void))ptr;
    int result = func();
    printf("func()=%d\n", result);
    munmap(ptr, page);
    return 0;
}
