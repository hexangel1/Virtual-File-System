#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "blockmanager.hpp"
#include "inodemanager.hpp"
#include "ivfs.hpp"

BlockManager::BlockManager() : bitmap(0), size(0), fd(-1)
{
        printf("size = %ld\n", sizeof(BlockAddress));
        pthread_mutex_init(&mtx, 0);
        for (uint32_t i = 0; i < storage_amount; i++) {
                storage_fds[i] = -1;
                free_blocks[i] = 0;
        }
}

BlockManager::~BlockManager()
{
        pthread_mutex_destroy(&mtx);
        if (bitmap) {
                msync(bitmap, size, MS_SYNC);
                munmap(bitmap, size);
        }
        if (fd != -1)
                close(fd);
        for (uint32_t i = 0; i < storage_amount; i++) {
                if (storage_fds[i] != -1)
                        close(storage_fds[i]);
        }
}

bool BlockManager::Init(int dir_fd)
{
        void *p;
        fd = openat(dir_fd, "free_blocks", O_RDWR);
        if (fd == -1) {
                perror("BlockManager::Init(): open");
                return false;
        }
        size = storage_size * storage_amount / 8;
        p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
                perror("BlockManager::Init(): mmap");
                return false;
        }
        bitmap = (char*)p;
        char storage_name[32];
        for (uint32_t i = 0; i < storage_amount; i++) {
                sprintf(storage_name, "storage%d", i);
                free_blocks[i] = CalculateFreeBlocks(i);
                storage_fds[i] = openat(dir_fd, storage_name, O_RDWR);
                if (storage_fds[i] == -1) {
                        perror("BlockManager::Init(): open");
                        return false;
                }
        }
        return true;
}

BlockAddress BlockManager::GetBlock(Inode *in, off_t num)
{
        BlockAddress retval;
        if (num < 8) {
                retval = in->block[num];
        } else if (num >= 8 && num < 8 + addr_in_block) {
                BlockAddress *lev1 = (BlockAddress*)ReadBlock(in->block[8]);
                retval = lev1[num - 8];
                UnmapBlock(lev1);
        } else {
                off_t idx1 = (num - 8 - addr_in_block) / addr_in_block;
                off_t idx0 = (num - 8 - addr_in_block) % addr_in_block;
                BlockAddress *lev2 = (BlockAddress*)ReadBlock(in->block[9]);
                BlockAddress *lev1 = (BlockAddress*)ReadBlock(lev2[idx1]);
                retval = lev1[idx0];
                UnmapBlock(lev1);
                UnmapBlock(lev2);
        }
        return retval;
}

BlockAddress BlockManager::AddBlock(Inode *in)
{
        BlockAddress new_block = AllocateBlock();
        if (in->blk_size < 8)
                in->block[in->blk_size] = new_block;
        else if (in->blk_size >= 8 && in->blk_size < 8 + addr_in_block)
                AddBlockToLev1(in, new_block);
        else if (in->blk_size >= 8 + addr_in_block)
                AddBlockToLev2(in, new_block);
        in->blk_size++;
        return new_block;
}

void BlockManager::FreeBlocks(Inode *in)
{
        for (off_t i = 0; i < in->blk_size; i++)
                FreeBlock(GetBlock(in, i));
        if (in->blk_size > 8)
                FreeBlock(in->block[8]);
        if (in->blk_size > 8 + addr_in_block) {
                BlockAddress *block_lev2 = (BlockAddress*)ReadBlock(in->block[9]);
                off_t r = (in->blk_size - 8 - addr_in_block) / addr_in_block;
                for (off_t i = 0; i < r + 1; i++)
                        FreeBlock(block_lev2[i]);
                UnmapBlock(block_lev2);
                FreeBlock(in->block[9]);
        }
        in->byte_size = 0;
        in->blk_size = 0;
}

void *BlockManager::ReadBlock(BlockAddress addr) const
{
        void *ptr = mmap(0, block_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, storage_fds[addr.storage_num],
                         addr.block_num * block_size);
        if (ptr == MAP_FAILED) {
                perror("BlockManager::ReadBlock(): mmap");
                return 0;
        }
        return ptr;
}

void BlockManager::UnmapBlock(void *ptr) const
{
        msync(ptr, block_size, MS_ASYNC);
        munmap(ptr, block_size);
}

BlockAddress BlockManager::AllocateBlock()
{
        BlockAddress addr;
        pthread_mutex_lock(&mtx);
        uint32_t idx = MostFreeStorage();
        addr.storage_num = idx;
        addr.block_num = SearchFreeBlock(idx);
        free_blocks[idx]--;
        pthread_mutex_unlock(&mtx);
        return addr;
}

void BlockManager::FreeBlock(BlockAddress addr)
{
        size_t idx = addr.storage_num * storage_size + addr.block_num;
        pthread_mutex_lock(&mtx);
        bitmap[idx / 8] |= 0x1 << idx % 8;
        free_blocks[addr.storage_num]++;
        pthread_mutex_unlock(&mtx);
}

void BlockManager::AddBlockToLev1(Inode *in, BlockAddress new_block)
{
        if (in->blk_size == 8)
                in->block[8] = AllocateBlock();
        BlockAddress *block_lev1 = (BlockAddress*)ReadBlock(in->block[8]);
        block_lev1[in->blk_size - 8] = new_block;
        UnmapBlock(block_lev1);
}

void BlockManager::AddBlockToLev2(Inode *in, BlockAddress new_block)
{
        off_t lev1_num = (in->blk_size - 8 - addr_in_block) / addr_in_block;
        off_t lev0_num = (in->blk_size - 8 - addr_in_block) % addr_in_block;
        if (in->blk_size == 8 + addr_in_block)
                in->block[9] = AllocateBlock();
        BlockAddress *block_lev2 = (BlockAddress*)ReadBlock(in->block[9]);
        if (lev0_num == 0)
                block_lev2[lev1_num] = AllocateBlock();
        BlockAddress *block_lev1 = (BlockAddress*)ReadBlock(block_lev2[lev1_num]);
        block_lev1[lev0_num] = new_block;
        UnmapBlock(block_lev1);
        UnmapBlock(block_lev2);
}

uint32_t BlockManager::SearchFreeBlock(uint32_t idx) const
{
        uint32_t blocks = storage_size / 8;
        for (uint32_t i = blocks * idx; i < blocks * (idx + 1); i++) {
                for (char mask = 0x1, j = 0; mask; mask <<= 1, j++) {
                        if (bitmap[i] & mask) {
                                bitmap[i] &= ~mask;
                                return (i - blocks * idx) * 8 + j;
                        }
                }
        }
        return 0xFFFFFFFF;
}

uint32_t BlockManager::CalculateFreeBlocks(uint32_t idx) const
{
        uint32_t free_blocks = 0;
        uint32_t blocks = storage_size / 8;
        for (uint32_t i = blocks * idx; i < blocks * (idx + 1); i++) {
                for (char mask = 0x1; mask; mask <<= 1) {
                        if (bitmap[i] & mask)
                                free_blocks++;
                }
        }
        return free_blocks;
}

uint32_t BlockManager::MostFreeStorage() const
{
        uint32_t max = free_blocks[0];
        uint32_t idx = 0;
        for (uint32_t i = 1; i < storage_amount; i++) {
                if (free_blocks[i] > max) {
                        max = free_blocks[i];
                        idx = i;
                }
        }
        return idx;
}

bool BlockManager::CreateFreeBlockArray(int dir_fd)
{
        int fd = openat(dir_fd, "free_blocks",
                        O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
                perror("BlockManager::CreateFreeBlockArray(): open");
                return false;
        }
        size_t size = storage_amount * storage_size / 8;
        int res = ftruncate(fd, size);
        if (res == -1) {
                perror("BlockManager::CreateFreeBlockArray(): ftruncate");
                return false;
        }
        void *p = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
                perror("BlockManager::CreateFreeBlockArray(): mmap");
                return false;
        }
        memset(p, 0xFF, size);
        msync(p, size, MS_SYNC);
        munmap(p, size);
        close(fd);
        return true;
}

bool BlockManager::CreateBlockSpace(int dir_fd)
{
        for (uint32_t i = 0; i < storage_amount; i++) {
                char storage_name[32];
                sprintf(storage_name, "storage%d", i);
                int fd = openat(dir_fd, storage_name,
                                O_RDWR | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                        perror("BlockManager::CreateBlockSpace(): open");
                        return false;
                }
                size_t size = storage_size * block_size;
                int res = ftruncate(fd, size);
                if (res == -1) {
                        perror("BlockManager::CreateBlockSpace(): ftruncate");
                        return false;
                }
                close(fd);
        }
        return true;
}

