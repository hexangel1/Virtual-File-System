#ifndef BLOCKMANAGER_HPP_SENTRY
#define BLOCKMANAGER_HPP_SENTRY

#include <cstdint>
#include <cstddef>

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
public:
        BlockManager();
        ~BlockManager();
        bool Init();
        BlockAddr AllocateBlock();
        void FreeBlock(BlockAddr addr); 
        void *ReadBlock(BlockAddr addr);
        void UnmapBlock(void *ptr);
        static bool CreateFreeBlockArray();
        static bool CreateBlockSpace(); 
private:
        uint32_t SearchFreeBlock(uint32_t idx);
        uint32_t CalculateFreeBlocks(uint32_t idx);
        uint32_t MostFreeStorage();
};

#endif /* BLOCKMANAGER_HPP_SENTRY */

