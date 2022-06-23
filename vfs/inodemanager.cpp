#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include "inodemanager.hpp"
#include "ivfs.hpp"

InodeManager::InodeManager()
{
        for (int i = 0; i < inodes_cache_size; i++)
                inodes_cache[i] = 0;
        inodes_used = inodes_cache_size;
        inodes_fd = -1;
}

InodeManager::~InodeManager()
{
        if (inodes_fd != -1)
                close(inodes_fd);
}

bool InodeManager::Init()
{
        inodes_fd = open("inode_space.bin", O_RDWR);
        if (inodes_fd == -1) {
                perror("InodeManager::Init(): open");
                return false;
        }
        SearchFreeInodes();
        return true;
}

uint32_t InodeManager::GetInode()
{
        uint32_t retval = -1;
        gf_mtx.lock();
        if (inodes_used == inodes_cache_size)
                SearchFreeInodes();
        if (inodes_used < inodes_cache_size) {
                retval = inodes_cache[inodes_used];
                inodes_used++;
        }
        gf_mtx.unlock();
        return retval;
}

void InodeManager::FreeInode(uint32_t idx)
{
        gf_mtx.lock();
        if (inodes_used > 0) {
                inodes_used--;
                inodes_cache[inodes_used] = idx;
        }
        gf_mtx.unlock();
}

bool InodeManager::ReadInode(Inode *ptr, uint32_t idx)
{
        int res;
        rw_mtx.lock();
        res = lseek(inodes_fd, idx * sizeof(Inode), SEEK_SET);
        if (res == -1) {
                rw_mtx.unlock();
                return false;
        }
        res = read(inodes_fd, ptr, sizeof(Inode));
        rw_mtx.unlock();
        return res == sizeof(Inode);
}

bool InodeManager::WriteInode(const Inode *ptr, uint32_t idx)
{
        int res;
        rw_mtx.lock();
        res = lseek(inodes_fd, idx * sizeof(Inode), SEEK_SET);
        if (res == -1) {
                rw_mtx.unlock();
                return false;
        }
        res = write(inodes_fd, ptr, sizeof(Inode));
        rw_mtx.unlock();
        return res == sizeof(Inode);
}

bool InodeManager::CreateInodeSpace()
{
        int fd = open("inode_space.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
                perror("InodeManager::CreateInodeSpace(): open");
                return false;
        }
        int res = ftruncate(fd, IVFS::max_file_amount * sizeof(struct Inode));
        if (res == -1) {
                perror("InodeManager::CreateInodeSpace(): ftruncate");
                return false;
        }
        close(fd);
        return true;
}

void InodeManager::SearchFreeInodes()
{
        for (uint32_t idx = 1; idx < IVFS::max_file_amount; idx++) {
                Inode in;
                ReadInode(&in, idx);
                if (in.is_busy)
                        continue;
                inodes_used--;
                inodes_cache[inodes_used] = idx;
                if (inodes_used == 0)
                        break;
        }
}

