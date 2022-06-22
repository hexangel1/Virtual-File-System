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
                perror("inode_space.bin");
                return false;
        }
        SearchFreeInodes();
        return true;
}

bool InodeManager::CreateInodeSpace()
{
        int fd = creat("inode_space.bin", 0644);
        if (fd == -1) {
                perror("inode_space.bin");
                return false;
        }
        ftruncate(fd, IVFS::max_file_amount * sizeof(struct Inode));
        close(fd);
        return true;
}

uint32_t InodeManager::GetInode()
{
        uint32_t retval = -1;
        if (inodes_used == inodes_cache_size)
                SearchFreeInodes();
        if (inodes_used < inodes_cache_size) {
                retval = inodes_cache[inodes_used];
                inodes_used++;
        }
        return retval;
}

void InodeManager::FreeInode(uint32_t idx)
{
        if (inodes_used > 0) {
                inodes_used--;
                inodes_cache[inodes_used] = idx;
        }
}

bool InodeManager::ReadInode(Inode *ptr, uint32_t idx)
{
        int res;
        res = lseek(inodes_fd, idx * sizeof(Inode), SEEK_SET);
        if (res == -1)
                return false;
        res = read(inodes_fd, ptr, sizeof(Inode));
        if (res != sizeof(Inode))
                return false;
        return true;
}

bool InodeManager::WriteInode(const Inode *ptr, uint32_t idx)
{
        int res;
        res = lseek(inodes_fd, idx * sizeof(Inode), SEEK_SET);
        if (res == -1)
                return false;
        res = write(inodes_fd, ptr, sizeof(Inode));
        if (res != sizeof(Inode))
                return false;
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

