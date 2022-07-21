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
        bool defer_delete;
        struct Inode in;
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
        static const int max_name_len = 52;
        struct FileOpenFlags {
                bool r_flag;
                bool w_flag;
                bool a_flag;
                bool c_flag;
                bool t_flag;
        };
        struct DirRecord {
                char name[max_name_len + 1];
                char idx[63 - max_name_len];
        };
        struct OpenedFileItem {
                OpenedFile *file;
                OpenedFileItem *next;
        };
        int dir_fd;
        OpenedFileItem *first;
        InodeManager im;
        BlockManager bm;
        pthread_mutex_t mtx;
public:
        IVFS();
        ~IVFS();
        bool Boot(const char *path, bool makefs = false);
        bool Create(const char *path, bool directory = false);
        bool Remove(const char *path, bool recursive = false);
        bool Rename(const char *oldpath, const char *newpath);
        File *Open(const char *path, const char *flags);
        void Close(File *fp);
        ssize_t Read(File *fp, char *buf, size_t len);
        ssize_t Write(File *fp, const char *buf, size_t len);
        off_t Lseek(File *fp, off_t offset, int whence);
        off_t Size(File *fp) const { return fp->master->in.byte_size; }
private:
        void RecursiveDeletion(int idx);
        OpenedFile *OpenFile(int idx, bool want_read, bool want_write);
        OpenedFile *AddOpenedFile(int idx, bool want_read, bool want_write);
        OpenedFile *SearchOpenedFile(int idx) const;
        void DeleteOpenedFile(OpenedFile *ofptr);
        int SearchInode(const char *path, bool create_perm, bool mkdr = false);
        int SearchFileInDir(int dir_idx, const char *name);
        int CreateFileInDir(int dir_idx, const char *name, bool is_dir);
        void CreateDirRecord(int dir_idx, const char *filename, int idx);
        void DeleteDirRecord(int dir_idx, const char *filename);
        DirRecordList *ReadDirectory(Inode *dir);
        void CreateRootDirectory();
        bool IsDirectory(int idx);
        static void FreeDirRecordList(DirRecordList *ptr);
        static void CreateFileSystem(int dir_fd);
        static const char *PathParsing(const char *path, char *file);
        static void GetDirectory(const char *path, char *dir, char *file);
        static bool CheckPath(const char *path);
        static bool ParseOpenFlags(const char *flags, FileOpenFlags &opf);
};

#endif /* IVFS_HPP_SENTRY */

