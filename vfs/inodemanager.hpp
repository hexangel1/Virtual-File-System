#ifndef INODEMANAGER_HPP_SENTRY
#define INODEMANAGER_HPP_SENTRY

#include <cstdint>
#include <mutex>
#include "blockmanager.hpp"

struct Inode {
        bool is_busy;
        bool is_dir;
        uint64_t byte_size;
        uint32_t blk_size;
        BlockAddr block[10];
};

class InodeManager {
        static const int inodes_cache_size = 16;
        uint32_t inodes_cache[inodes_cache_size];
        int inodes_used;
        int inodes_fd;
        mutable std::mutex gf_mtx;
        mutable std::mutex rw_mtx;
public:
        InodeManager();
        ~InodeManager();
        bool Init();
        uint32_t GetInode();
        void FreeInode(uint32_t idx);
        bool ReadInode(Inode *ptr, uint32_t idx) const;
        bool WriteInode(const Inode *ptr, uint32_t idx) const;
        static bool CreateInodeSpace();
private:
        void SearchFreeInodes();
};

#endif /* INODEMANAGER_HPP_SENTRY */

