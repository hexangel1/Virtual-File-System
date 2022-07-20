#ifndef INODEMANAGER_HPP_SENTRY
#define INODEMANAGER_HPP_SENTRY

#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include "blockmanager.hpp"

struct Inode {
        bool is_busy;
        bool is_dir;
        off_t byte_size;
        off_t blk_size;
        BlockAddr block[10];
};

class InodeManager {
        static const int max_file_amount = 1000000;
        static const int inodes_cache_size = 16;
        int inodes_fd;
        int cache_used;
        int inodes_cache[inodes_cache_size];
        pthread_mutex_t gf_mtx;
        pthread_mutex_t rw_mtx;
public:
        InodeManager();
        ~InodeManager();
        bool Init(int dir);
        uint32_t GetInode();
        void FreeInode(uint32_t idx);
        bool ReadInode(Inode *ptr, uint32_t idx);
        bool WriteInode(const Inode *ptr, uint32_t idx);
        static bool CreateInodeSpace(int dir_fd);
private:
        void SearchFreeInodes();
};

#endif /* INODEMANAGER_HPP_SENTRY */

