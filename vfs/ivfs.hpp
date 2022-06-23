#ifndef IVFS_HPP_SENTRY
#define IVFS_HPP_SENTRY

#include "inodemanager.hpp"
#include "blockmanager.hpp"

#define MAX_FILENAME_LEN 27

struct OpenedFile {
        int opened;
        bool read_only;
        uint32_t inode_idx;
        struct Inode in;
};

struct File {
        size_t cur_pos;
        size_t cur_block;
        char *block;
        OpenedFile *master;
};

struct DirRecord {
        char filename[MAX_FILENAME_LEN + 1];
        uint32_t inode_idx;
};

struct DirRecordList {
        DirRecord rec;
        DirRecordList *next;
};

class IVFS {
        InodeManager im;
        BlockManager bm;
        OpenedFile **files;
        size_t arr_size;
        size_t arr_used;
        std::mutex mtx;
public:
        static const uint32_t storage_amount = 4;
        static const uint32_t storage_size = 16384;
        static const uint32_t block_size = 4096;
        static const uint32_t max_file_amount = 100000;
        static const uint32_t addr_in_block = block_size / sizeof(BlockAddr);
        static const uint32_t dirr_in_block = block_size / sizeof(DirRecord);

        IVFS();
        ~IVFS();
        bool Init(bool makefs = false);
        File *Open(const char *path);
        File *Create(const char *path);
        void Close(File *f);
        size_t Read(File *f, char *buf, size_t len);
        size_t Write(File *f, const char *buf, size_t len);
private:
        File *NewFile(const char *path, bool write_perm);
        OpenedFile *OpenFile(const char *path, bool write_perm);
        OpenedFile *AddOpenedFile(uint32_t idx, bool write_perm);
        void DeleteOpenedFile(OpenedFile *ofptr);
        OpenedFile *SearchOpenedFile(uint32_t idx);
        uint32_t SearchInode(const char *path, bool write_perm);
        uint32_t SearchFileInDir(Inode *dir, const char *name);
        uint32_t CreateFileInDir(Inode *dir, const char *name, bool is_dir);
        DirRecordList *ReadDirectory(Inode *dir);
        void FreeDirRecordList(DirRecordList *ptr);
        void MakeDirRecord(Inode *dir, const char *file, uint32_t idx);
        void AppendDirRecord(Inode *dir, DirRecord *rec);
        BlockAddr GetBlockNum(Inode *in, uint32_t num);
        BlockAddr AddBlock(Inode *in);
        void AddBlockToLev1(Inode *in, BlockAddr new_block);
        void AddBlockToLev2(Inode *in, BlockAddr new_block);
        void FreeBlocks(Inode *in);
        void CreateRootDirectory();
        static void CreateFileSystem();
        static const char *PathParsing(const char *path, char *filename);
        static bool CheckPath(const char *path);
};

#endif /* IVFS_HPP_SENTRY */

