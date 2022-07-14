#ifndef BLOCKMANAGER_HPP_SENTRY
#define BLOCKMANAGER_HPP_SENTRY

#include <stdint.h>
#include <cstddef>
#include <pthread.h>

struct BlockAddr {
        uint32_t storage_num;
        uint32_t block_num;
};

class BlockManager {
        char *bitarray;
        size_t size;
        int fd;
        int *storage_fds;
        uint32_t *free_blocks;
        pthread_mutex_t mtx;
public:
        BlockManager();
        ~BlockManager();
        bool Init(int dir_fd);
        BlockAddr AllocateBlock();
        void FreeBlock(BlockAddr addr);
        void *ReadBlock(BlockAddr addr) const;
        void UnmapBlock(void *ptr) const;
        void SyncBlocks() const;
        static bool CreateFreeBlockArray(int dir);
        static bool CreateBlockSpace(int dir);
private:
        uint32_t SearchFreeBlock(uint32_t idx) const;
        uint32_t CalculateFreeBlocks(uint32_t idx) const;
        uint32_t MostFreeStorage() const;
};

#endif /* BLOCKMANAGER_HPP_SENTRY */

