#ifndef BLOCKMANAGER_HPP_SENTRY
#define BLOCKMANAGER_HPP_SENTRY

#include <cstddef>
#include <stdint.h>
#include <pthread.h>

struct Inode;

#pragma pack(push, 1)
struct BlockAddress {
        uint32_t storage_num;
        uint32_t block_num;
};
#pragma pack(pop)

class BlockManager {
        static const uint32_t storage_amount = 4;
        static const uint32_t storage_size = 16384;
        static const off_t block_size = 4096;
        static const off_t addr_in_block = block_size / sizeof(BlockAddress);
        char *bitmap;
        size_t size;
        int fd;
        int storage_fds[storage_amount];
        uint32_t free_blocks[storage_amount];
        pthread_mutex_t mtx;
public:
        BlockManager();
        ~BlockManager();
        bool Init(int dir_fd);
        BlockAddress GetBlock(Inode *in, off_t num);
        BlockAddress AddBlock(Inode *in);
        void FreeBlocks(Inode *in);
        void *ReadBlock(BlockAddress addr) const;
        void UnmapBlock(void *ptr) const;
        static bool CreateFreeBlockArray(int dir);
        static bool CreateBlockSpace(int dir);
        static off_t BlockSize() { return block_size; }
private:
        BlockAddress AllocateBlock();
        void FreeBlock(BlockAddress addr);
        void AddBlockToLev1(Inode *in, BlockAddress new_block);
        void AddBlockToLev2(Inode *in, BlockAddress new_block);
        uint32_t SearchFreeBlock(uint32_t idx) const;
        uint32_t CalculateFreeBlocks(uint32_t idx) const;
        uint32_t MostFreeStorage() const;
};

#endif /* BLOCKMANAGER_HPP_SENTRY */

