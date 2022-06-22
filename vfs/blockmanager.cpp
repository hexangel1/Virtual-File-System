#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "blockmanager.hpp"
#include "ivfs.hpp"

BlockManager::BlockManager() : bitarray(0), size(0), fd(-1)
{
        storage_fds = new int[IVFS::storage_amount];
        free_blocks = new uint32_t[IVFS::storage_amount];
        for (uint32_t i = 0; i < IVFS::storage_amount; i++) {
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
        for (uint32_t i = 0; i < IVFS::storage_amount; i++) {
                if (storage_fds[i] != -1)
                        close(storage_fds[i]);
        }
        delete []storage_fds;
        delete []free_blocks;
}

bool BlockManager::Init()
{
        void *p;
        fd = open("free_blocks.bin", O_RDWR);
        if (fd == -1) {
                perror("open");
                return false;
        }
        size = IVFS::storage_size * IVFS::storage_amount / 8;
        p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
                perror("mmap");
                return false;
        }
        bitarray = (char*)p;
        char storage_name[32];
        for (uint32_t i = 0; i < IVFS::storage_amount; i++) {
                sprintf(storage_name, "storage%d.bin", i);
                free_blocks[i] = CalculateFreeBlocks(i);
                storage_fds[i] = open(storage_name, O_RDWR);
                if (storage_fds[i] == -1) {
                        perror("open");
                        return false;
                }
        }
        return true;
}

BlockAddr BlockManager::AllocateBlock()
{
        BlockAddr addr;
        uint32_t idx = MostFreeStorage();
        addr.storage_num = idx;
        addr.block_num = SearchFreeBlock(idx);
        free_blocks[idx]--;
        return addr;
}

void BlockManager::FreeBlock(BlockAddr addr)
{
        size_t idx = addr.storage_num * IVFS::storage_size + addr.block_num;
        bitarray[idx / 8] |= 0x1 << idx % 8;
        free_blocks[addr.storage_num]++;
}

void *BlockManager::ReadBlock(BlockAddr addr)
{
        void *ptr = mmap(0, IVFS::block_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, storage_fds[addr.storage_num],
                         addr.block_num * IVFS::block_size);
        if (ptr == MAP_FAILED) {
                perror("immap");
                return 0;
        }
        return ptr;
}

void BlockManager::UnmapBlock(void *ptr)
{
        msync(ptr, IVFS::block_size, MS_SYNC);
        munmap(ptr, IVFS::block_size);
}

uint32_t BlockManager::SearchFreeBlock(uint32_t idx)
{
        uint32_t blocks = IVFS::storage_size / 8;
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
 
uint32_t BlockManager::CalculateFreeBlocks(uint32_t idx)
{
        uint32_t free_blocks = 0;
        uint32_t blocks = IVFS::storage_size / 8;
        for (uint32_t i = blocks * idx; i < blocks * (idx + 1); i++) {
                for (char mask = 0x1; mask; mask <<= 1) {
                        if (bitarray[i] & mask)
                                free_blocks++;
                }
        }
        return free_blocks;
}

uint32_t BlockManager::MostFreeStorage()
{
        uint32_t max = free_blocks[0];
        uint32_t idx = 0;
        for (uint32_t i = 1; i < IVFS::storage_amount; i++) {
                if (free_blocks[i] > max) {
                        max = free_blocks[i];
                        idx = i;
                }
        }
        return idx;
}

bool BlockManager::CreateFreeBlockArray()
{
        int fd = open("free_blocks.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
                perror("open");
                return false;
        }
        size_t size = IVFS::storage_amount * IVFS::storage_size / 8;
        ftruncate(fd, size); 
        void *p = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
                perror("mmap");
                return false;
        }
        memset(p, 0xFF, size);
        msync(p, size, MS_SYNC);
        munmap(p, size);
        close(fd);
        return true;
}

bool BlockManager::CreateBlockSpace()
{
        char storage_name[32];
        int fd;
        for (uint32_t i = 0; i < IVFS::storage_amount; i++) {
                sprintf(storage_name, "storage%d.bin", i);
                fd = creat(storage_name, 0644);
                if (fd == -1) {
                        perror(storage_name);
                        return false;
                }
                ftruncate(fd, IVFS::storage_size * IVFS::block_size);
                close(fd);
        }
        return true;
}

