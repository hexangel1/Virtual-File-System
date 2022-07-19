#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "blockmanager.hpp"
#include "inodemanager.hpp"
#include "ivfs.hpp"

BlockManager::BlockManager() : bitarray(0), size(0), fd(-1)
{
        mtx = PTHREAD_MUTEX_INITIALIZER;
        for (uint32_t i = 0; i < storage_amount; i++) {
                storage_fds[i] = -1;
                free_blocks[i] = 0;
        }
}

BlockManager::~BlockManager()
{
        if (bitarray) {
                msync(bitarray, size, MS_SYNC);
                munmap(bitarray, size);
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
        bitarray = (char*)p;
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

BlockAddr BlockManager::GetBlock(Inode *in, off_t num)
{
        BlockAddr retval;
        if (num < 8) {
                retval = in->block[num];
        } else if (num >= 8 && num < 8 + addr_in_block) {
                BlockAddr *lev1 = (BlockAddr*)ReadBlock(in->block[8]);
                retval = lev1[num - 8];
                UnmapBlock(lev1);
        } else {
                off_t idx1 = (num - 8 - addr_in_block) / addr_in_block;
                off_t idx0 = (num - 8 - addr_in_block) % addr_in_block;
                BlockAddr *lev2 = (BlockAddr*)ReadBlock(in->block[9]);
                BlockAddr *lev1 = (BlockAddr*)ReadBlock(lev2[idx1]);
                retval = lev1[idx0];
                UnmapBlock(lev1);
                UnmapBlock(lev2);
        }
        return retval;
}

BlockAddr BlockManager::AddBlock(Inode *in)
{
        BlockAddr new_block = AllocateBlock();
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
                BlockAddr *block_lev2 = (BlockAddr*)ReadBlock(in->block[9]);
                off_t r = (in->blk_size - 8 - addr_in_block) / addr_in_block;
                for (off_t i = 0; i < r + 1; i++)
                        FreeBlock(block_lev2[i]);
                UnmapBlock(block_lev2);
                FreeBlock(in->block[9]);
        }
        in->byte_size = 0;
        in->blk_size = 0;
}

void *BlockManager::ReadBlock(BlockAddr addr) const
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

BlockAddr BlockManager::AllocateBlock()
{
        BlockAddr addr;
        pthread_mutex_lock(&mtx);
        uint32_t idx = MostFreeStorage();
        addr.storage_num = idx;
        addr.block_num = SearchFreeBlock(idx);
        free_blocks[idx]--;
        pthread_mutex_unlock(&mtx);
        return addr;
}

void BlockManager::FreeBlock(BlockAddr addr)
{
        size_t idx = addr.storage_num * storage_size + addr.block_num;
        pthread_mutex_lock(&mtx);
        bitarray[idx / 8] |= 0x1 << idx % 8;
        free_blocks[addr.storage_num]++;
        pthread_mutex_unlock(&mtx);
}

void BlockManager::AddBlockToLev1(Inode *in, BlockAddr new_block)
{
        if (in->blk_size == 8)
                in->block[8] = AllocateBlock();
        BlockAddr *block_lev1 = (BlockAddr*)ReadBlock(in->block[8]);
        block_lev1[in->blk_size - 8] = new_block;
        UnmapBlock(block_lev1);
}

void BlockManager::AddBlockToLev2(Inode *in, BlockAddr new_block)
{
        off_t lev1_num = (in->blk_size - 8 - addr_in_block) / addr_in_block;
        off_t lev0_num = (in->blk_size - 8 - addr_in_block) % addr_in_block;
        if (in->blk_size == 8 + addr_in_block)
                in->block[9] = AllocateBlock();
        BlockAddr *block_lev2 = (BlockAddr*)ReadBlock(in->block[9]);
        if (lev0_num == 0)
                block_lev2[lev1_num] = AllocateBlock();
        BlockAddr *block_lev1 = (BlockAddr*)ReadBlock(block_lev2[lev1_num]);
        block_lev1[lev0_num] = new_block;
        UnmapBlock(block_lev1);
        UnmapBlock(block_lev2);
}

uint32_t BlockManager::SearchFreeBlock(uint32_t idx) const
{
        uint32_t blocks = storage_size / 8;
        for (uint32_t i = blocks * idx; i < blocks * (idx + 1); i++) {
                for (char mask = 0x1, j = 0; mask; mask <<= 1, j++) {
                        if (bitarray[i] & mask) {
                                bitarray[i] &= ~mask;
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
                        if (bitarray[i] & mask)
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

