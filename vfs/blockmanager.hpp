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
        char *bitarray;
        size_t size;
        int fd;
        int *storage_fds;
        uint32_t *free_blocks;
        pthread_mutex_t mtx;
public:
        static const off_t addr_in_block;
        BlockManager();
        ~BlockManager();
        bool Init(int dir_fd);
        BlockAddr GetBlockNum(Inode *in, off_t num);
        BlockAddr AddBlock(Inode *in);
        void AddBlockToLev1(Inode *in, BlockAddr new_block);
        void AddBlockToLev2(Inode *in, BlockAddr new_block);
        void FreeBlocks(Inode *in);
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

