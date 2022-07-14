#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include "inodemanager.hpp"
#include "ivfs.hpp"

InodeManager::InodeManager()
{
        gf_mtx = PTHREAD_MUTEX_INITIALIZER;
        rw_mtx = PTHREAD_MUTEX_INITIALIZER;
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

bool InodeManager::Init(int dir_fd)
{
        inodes_fd = openat(dir_fd, "inode_space", O_RDWR);
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
        Inode in;
        memset(&in, 0, sizeof(in));
        in.is_busy = true;
        pthread_mutex_lock(&gf_mtx);
        if (inodes_used == inodes_cache_size)
                SearchFreeInodes();
        if (inodes_used < inodes_cache_size) {
                retval = inodes_cache[inodes_used];
                inodes_used++;
        }
        WriteInode(&in, retval);
        pthread_mutex_unlock(&gf_mtx);
        return retval;
}

void InodeManager::FreeInode(uint32_t idx)
{
        pthread_mutex_lock(&gf_mtx);
        if (inodes_used > 0) {
                inodes_used--;
                inodes_cache[inodes_used] = idx;
        }
        pthread_mutex_unlock(&gf_mtx);
}

bool InodeManager::ReadInode(Inode *ptr, uint32_t idx)
{
        int res;
        pthread_mutex_lock(&rw_mtx);
        res = lseek(inodes_fd, idx * sizeof(Inode), SEEK_SET);
        if (res == -1) {
                pthread_mutex_unlock(&rw_mtx);
                return false;
        }
        res = read(inodes_fd, ptr, sizeof(Inode));
        pthread_mutex_unlock(&rw_mtx);
        return res == sizeof(Inode);
}

bool InodeManager::WriteInode(const Inode *ptr, uint32_t idx)
{
        int res;
        pthread_mutex_lock(&rw_mtx);
        res = lseek(inodes_fd, idx * sizeof(Inode), SEEK_SET);
        if (res == -1) {
                pthread_mutex_unlock(&rw_mtx);
                return false;
        }
        res = write(inodes_fd, ptr, sizeof(Inode));
        pthread_mutex_unlock(&rw_mtx);
        return res == sizeof(Inode);
}

bool InodeManager::CreateInodeSpace(int dir)
{
        int fd = openat(dir, "inode_space", O_RDWR | O_CREAT | O_TRUNC, 0644);
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

