#ifndef IVFS_HPP_SENTRY
#define IVFS_HPP_SENTRY

#include "inodemanager.hpp"
#include "blockmanager.hpp"
#include "file.hpp"

#define MAX_FILENAME_LEN 27

struct DirRecord {
        char filename[MAX_FILENAME_LEN + 1];
        uint32_t inode_idx;
};

struct DirRecordList {
        DirRecord rec;
        DirRecordList *next;
};

struct OpenedFile {
        uint32_t inode_idx; 
        unsigned int opened;
        bool read_only;
        struct Inode in;
};

class IVFS {
        InodeManager im;
        BlockManager bm;
        OpenedFile **files;
        size_t arr_size;
        size_t arr_used;
        int dir_fd;
        pthread_mutex_t mtx;
public:
        static const uint32_t max_file_amount = 100000; 
        static const uint32_t storage_amount = 4;
        static const uint32_t storage_size = 16384;
        static const off_t block_size = 4096;
        static const off_t addr_in_block = block_size / sizeof(BlockAddr);
        static const off_t dirr_in_block = block_size / sizeof(DirRecord);        
        IVFS();
        ~IVFS();
        bool Mount(const char *path, bool makefs = false);
        void Umount();
        bool Create(const char *path);
        bool Remove(const char *path);
        File Open(const char *path, const char *flags);
private:
        File NewFile(const char *path, bool write_perm);
        OpenedFile *OpenFile(const char *path, bool write_perm);
        void CloseFile(OpenedFile *ofptr);
        OpenedFile *AddOpenedFile(uint32_t idx, bool write_perm);
        void DeleteOpenedFile(OpenedFile *ofptr);
        OpenedFile *SearchOpenedFile(uint32_t idx) const;
        uint32_t SearchInode(const char *path, bool write_perm);
        uint32_t SearchFileInDir(Inode *dir, const char *name) const;
        uint32_t CreateFileInDir(Inode *dir, const char *name, bool is_dir);
        DirRecordList *ReadDirectory(Inode *dir) const;
        void FreeDirRecordList(DirRecordList *ptr) const;
        void MakeDirRecord(Inode *dir, const char *file, uint32_t idx);
        void AppendDirRecord(Inode *dir, DirRecord *rec);
        BlockAddr GetBlockNum(Inode *in, uint32_t num) const;
        BlockAddr AddBlock(Inode *in);
        void AddBlockToLev1(Inode *in, BlockAddr new_block);
        void AddBlockToLev2(Inode *in, BlockAddr new_block);
        void FreeBlocks(Inode *in);
        void CreateRootDirectory();
        static void CreateFileSystem(int dir_fd);
        static const char *PathParsing(const char *path, char *filename);
        static bool CheckPath(const char *path);
        friend class File;
};

#endif /* IVFS_HPP_SENTRY */

