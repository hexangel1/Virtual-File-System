#ifndef BLOCKMANAGER_HPP_SENTRY
#define BLOCKMANAGER_HPP_SENTRY

#include <stdint.h>
#include <cstddef>
#include <pthread.h>

struct Inode;

struct BlockAddr {
        uint32_t storage_num;
        uint32_t block_num;
};

class BlockManager {
public:
        static const uint32_t storage_amount = 4;
        static const uint32_t storage_size = 16384;
        static const off_t block_size = 4096;
        static const off_t addr_in_block = block_size / sizeof(BlockAddr);
private:
        char *bitarray;
        size_t size;
        int fd;
        int storage_fds[storage_amount];
        uint32_t free_blocks[storage_amount];
        pthread_mutex_t mtx;
public:
        BlockManager();
        ~BlockManager();
        bool Init(int dir_fd);
        BlockAddr GetBlock(Inode *in, off_t num);
        BlockAddr AddBlock(Inode *in);
        void FreeBlocks(Inode *in);
        void *ReadBlock(BlockAddr addr) const;
        void UnmapBlock(void *ptr) const;
        static bool CreateFreeBlockArray(int dir);
        static bool CreateBlockSpace(int dir);
private:
        BlockAddr AllocateBlock();
        void FreeBlock(BlockAddr addr);
        void AddBlockToLev1(Inode *in, BlockAddr new_block);
        void AddBlockToLev2(Inode *in, BlockAddr new_block);
        uint32_t SearchFreeBlock(uint32_t idx) const;
        uint32_t CalculateFreeBlocks(uint32_t idx) const;
        uint32_t MostFreeStorage() const;
};

#endif /* BLOCKMANAGER_HPP_SENTRY */

