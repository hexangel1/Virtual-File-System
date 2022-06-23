#ifndef BLOCKMANAGER_HPP_SENTRY
#define BLOCKMANAGER_HPP_SENTRY

#include <cstdint>
#include <cstddef>
#include <mutex>

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
        mutable std::mutex mtx;
public:
        BlockManager();
        ~BlockManager();
        bool Init();
        BlockAddr AllocateBlock() const;
        void FreeBlock(BlockAddr addr) const;
        void *ReadBlock(BlockAddr addr) const;
        void UnmapBlock(void *ptr) const;
        void SyncBlocks() const;
        static bool CreateFreeBlockArray();
        static bool CreateBlockSpace();
private:
        uint32_t SearchFreeBlock(uint32_t idx) const;
        uint32_t CalculateFreeBlocks(uint32_t idx) const;
        uint32_t MostFreeStorage() const;
};

#endif /* BLOCKMANAGER_HPP_SENTRY */

