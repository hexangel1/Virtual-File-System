#ifndef IVFS_HPP_SENTRY
#define IVFS_HPP_SENTRY

#include "inodemanager.hpp"
#include "blockmanager.hpp"

struct DirRecordList {
        const char *filename;
        int32_t inode_idx;
        DirRecordList *next;
};

struct OpenedFile {
        int inode_idx; 
        int opened;
        bool perm_read;
        bool perm_write;
        struct Inode in;
        class IVFS *vfs;
};

struct File {
private:
        off_t cur_pos;
        off_t cur_block;
        char *block;
        OpenedFile *master;
        friend class IVFS;
};

class IVFS {
        struct FileOpenFlags {
                bool r_flag;
                bool w_flag;
                bool a_flag;
                bool c_flag;
                bool t_flag;
        };
        struct DirRecord {
                char name[53];
                char idx[11];
        };
        struct OpenedFileItem {
                OpenedFile *file;
                OpenedFileItem *next;
        };
        InodeManager im;
        BlockManager bm;
        OpenedFileItem *first;
        int dir_fd;
        pthread_mutex_t mtx;
public:
        IVFS();
        ~IVFS();
        bool Mount(const char *path, bool makefs = false);
        void Umount();
        bool Create(const char *path);
        bool Remove(const char *path, bool recursive = false);
        File *Open(const char *path, const char *flags);
        void Close(File *fp);
        ssize_t Read(File *fp, char *buf, size_t len);
        ssize_t Write(File *fp, const char *buf, size_t len);
        off_t Lseek(File *fp, off_t offset, int whence);
private:
        void RecursiveDeletion(int idx);
        OpenedFile *OpenFile(int idx, bool want_read, bool want_write);
        void CloseFile(OpenedFile *ofptr);
        bool IsDirectory(int idx);
        OpenedFile *AddOpenedFile(int idx, bool want_read, bool want_write);
        void DeleteOpenedFile(OpenedFile *ofptr);
        OpenedFile *SearchOpenedFile(int idx) const;
        int SearchInode(const char *path, bool create_perm);
        int SearchFileInDir(int dir_idx, const char *name);
        int CreateFileInDir(int dir_idx, const char *name, bool is_dir);
        DirRecordList *ReadDirectory(Inode *dir);
        void FreeDirRecordList(DirRecordList *ptr) const;
        void CreateDirRecord(int dir_idx, const char *filename, int idx);
        void DeleteDirRecord(int dir_idx, const char *filename);
        void CreateRootDirectory();
        static void CreateFileSystem(int dir_fd);
        static const char *PathParsing(const char *path, char *filename);
        static bool CheckPath(const char *path);
        static bool ParseOpenFlags(const char *flags, FileOpenFlags &opf);
        static void GetDirectory(const char *path, char *dir, char *file);
        static char *Strdup(const char *str);
public:
        static const int max_name_len = 52;
        static const int max_file_amount = 100000; 
        static const uint32_t storage_amount = 4;
        static const uint32_t storage_size = 16384;
        static const off_t block_size = 4096;
        static const off_t dirr_in_block = block_size / sizeof(DirRecord); 
};

#endif /* IVFS_HPP_SENTRY */

