#ifndef IVFS_HPP_SENTRY
#define IVFS_HPP_SENTRY

#include "blockmanager.hpp"
#include "inodemanager.hpp"

struct OpenedFile {
        unsigned int opened;
        bool read_only;
        struct Inode in;
        uint32_t inode_idx;
};

struct OpenedFileList {
        OpenedFile *file;
        OpenedFileList *next;
};

struct File {
        size_t cur_pos;
        size_t cur_block;
        char *block;
        OpenedFile *master;
};

struct DirRecord {
        char filename[28];
        uint32_t inode_idx;
};

struct DirRecordList {
        DirRecord rec;
        DirRecordList *next;
};

class IVFS {
        InodeManager im;
        BlockManager bm;
        OpenedFileList *first;
public:
        static const uint32_t storage_amount = 4;
        static const size_t storage_size = 16384;
        static const size_t block_size = 4096; 
        static const uint32_t max_file_amount = 100000;
        static const uint32_t addr_in_block = block_size / sizeof(BlockAddr);
        static const uint32_t dirr_in_block = block_size / sizeof(DirRecord);
        
        IVFS() : first(nullptr) {}
        bool Init(bool makefs = false);
        File *Open(const char *path);
        File *Create(const char *path);
        void Close(File *f);
        size_t Read(File *f, char *buf, size_t len);
        size_t Write(File *f, const char *buf, size_t len);
private:
        OpenedFile *OpenFile(const char *path, bool w);
        void CloseFile(OpenedFile *ofptr);
        uint32_t SearchInode(const char *path, bool w);
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
        static bool EmptyRecord(DirRecord *rec);
        static const char *GetNextDir(const char *path, char *filename);
};

#endif /* IVFS_HPP_SENTRY */

