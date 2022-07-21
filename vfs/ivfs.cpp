#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fcntl.h>
#include <unistd.h>
#include "ivfs.hpp"

IVFS::IVFS() : dir_fd(-1), first(0)
{
        pthread_mutex_init(&mtx, 0);
}

IVFS::~IVFS()
{
        pthread_mutex_destroy(&mtx);
        if (dir_fd != -1)
                close(dir_fd);
        OpenedFileItem *tmp;
        while (first) {
                tmp = first;
                first = first->next;
                im.WriteInode(&tmp->file->in, tmp->file->inode_idx);
                delete tmp->file;
                delete tmp;
        }
}

bool IVFS::Boot(const char *path, bool makefs)
{
        int res;
        dir_fd = open(path, O_RDONLY | O_DIRECTORY);
        if (dir_fd == -1) {
                perror("open");
                return false;
        }
        if (makefs)
                CreateFileSystem(dir_fd);
        res = im.Init(dir_fd);
        if (!res) {
                fputs("Failed to start InodeManager\n", stderr);
                return false;
        }
        res = bm.Init(dir_fd);
        if (!res) {
                fputs("Failed to start BlockManager\n", stderr);
                return false;
        }
        if (makefs)
                CreateRootDirectory();
        fputs("Virtual File System started successfully\n", stderr);
        return true;
}

bool IVFS::Create(const char *path, bool is_dir)
{
        pthread_mutex_lock(&mtx);
        SearchInode(path, true, is_dir);
        pthread_mutex_unlock(&mtx);
        return true;
} 

bool IVFS::Remove(const char *path, bool recursive)
{
        char dirname[max_name_len];
        char filename[max_name_len];
        if (!CheckPath(path)) {
                fprintf(stderr, "Invalid path: %s\n", path);
                return false;
        }
        GetDirectory(path, dirname, filename);
        pthread_mutex_lock(&mtx);
        int dir_idx = SearchInode(dirname, false);
        if (dir_idx == -1) {
                fprintf(stderr, "Directory %s not found\n", dirname);
                pthread_mutex_unlock(&mtx);
                return false;
        }
        int idx = SearchFileInDir(dir_idx, filename);
        if (idx == -1) {
                fprintf(stderr, "File %s not found\n", filename);
                pthread_mutex_unlock(&mtx);
                return false;
        }
        if (!recursive && IsDirectory(idx)) {
                fprintf(stderr, "%s is dir, use recursive = true\n", path);
                pthread_mutex_unlock(&mtx);
                return false;
        }
        RecursiveDeletion(idx);
        DeleteDirRecord(dir_idx, filename);
        pthread_mutex_unlock(&mtx);
        return true;
}

bool IVFS::Rename(const char *oldpath, const char *newpath)
{
        char old_dirname[max_name_len], old_filename[max_name_len];
        char new_dirname[max_name_len], new_filename[max_name_len];
        if (!CheckPath(oldpath)) {
                fprintf(stderr, "Invalid path: %s\n", oldpath);
                return false;
        }
        if (!CheckPath(newpath)) {
                fprintf(stderr, "Invalid path: %s\n", newpath);
                return false;
        }
        GetDirectory(oldpath, old_dirname, old_filename);
        GetDirectory(newpath, new_dirname, new_filename);
        pthread_mutex_lock(&mtx);
        int old_dir_idx = SearchInode(old_dirname, false);
        if (old_dir_idx == -1) {
                fprintf(stderr, "Directory %s not found\n", old_dirname);
                pthread_mutex_unlock(&mtx);
                return false;
        }
        int idx = SearchFileInDir(old_dir_idx, old_filename);
        if (idx == -1) {
                fprintf(stderr, "File %s not found\n", old_filename);
                pthread_mutex_unlock(&mtx);
                return false;
        }
        int new_dir_idx = SearchInode(new_dirname, true, true);
        if (!IsDirectory(new_dir_idx)) {
                fprintf(stderr, "Path %s not directory\n", new_dirname);
                pthread_mutex_unlock(&mtx);
                return false;
        }
        if (SearchFileInDir(new_dir_idx, new_filename) != -1) {
                fprintf(stderr, "Path %s already exists\n", newpath);
                pthread_mutex_unlock(&mtx);
                return false;
        }
        DeleteDirRecord(old_dir_idx, old_filename);
        CreateDirRecord(new_dir_idx, new_filename, idx);
        pthread_mutex_unlock(&mtx);
        return true;
}

File *IVFS::Open(const char *path, const char *flags)
{
        FileOpenFlags opf = { false, false, false, false, false };
        if (!ParseOpenFlags(flags, opf))
                return 0;
        if (!CheckPath(path)) {
                fprintf(stderr, "Invalid path: %s\n", path);
                return 0;
        }
        pthread_mutex_lock(&mtx);
        int idx = SearchInode(path, opf.c_flag);
        if (idx == -1) {
                fputs("File's inode not found\n", stderr);
                pthread_mutex_unlock(&mtx);
                return 0;
        }
        if (IsDirectory(idx)) {
                fputs("Open directory is not permitted\n", stderr);
                pthread_mutex_unlock(&mtx);
                return 0;
        }
        OpenedFile *ofptr = OpenFile(idx, opf.r_flag, opf.w_flag);
        pthread_mutex_unlock(&mtx);
        if (!ofptr) {
                fputs("Incompatible file open mode\n", stderr); 
                return 0;
        }
        if (opf.w_flag && opf.t_flag && ofptr->in.byte_size > 0) {
                bm.FreeBlocks(&ofptr->in);
                bm.AddBlock(&ofptr->in);
        }
        File *fp = new File;
        fp->cur_pos = 0;
        fp->cur_block = 0;
        fp->block = (char*)bm.ReadBlock(ofptr->in.block[0]);
        fp->master = ofptr;
        return fp;
}

void IVFS::Close(File *fp)
{
        if (!fp)
                return;
        bm.UnmapBlock(fp->block);
        pthread_mutex_lock(&mtx);
        fp->master->opened--;
        if (fp->master->opened == 0) {
                if (fp->master->defer_delete) {
                        bm.FreeBlocks(&fp->master->in);
                } else {
                        im.WriteInode(&fp->master->in, fp->master->inode_idx);
                }
                DeleteOpenedFile(fp->master);
        }
        pthread_mutex_unlock(&mtx);
        delete fp;
}

ssize_t IVFS::Read(File *fp, char *buf, size_t len)
{
        if (!fp->master->perm_read) {
                fputs("File opened in write-only mode", stderr);
                return -1;
        }
        size_t rc = 0;
        size_t was_read = fp->cur_block * bm.BlockSize() + fp->cur_pos;
        if (len > fp->master->in.byte_size - was_read)
                len = fp->master->in.byte_size - was_read;
        while (len > 0) {
                size_t can_read = bm.BlockSize() - fp->cur_pos;
                if (len < can_read) {
                        memcpy(buf + rc, fp->block + fp->cur_pos, len);
                        fp->cur_pos += len;
                        rc += len;
                        len = 0;
                } else {
                        memcpy(buf + rc, fp->block + fp->cur_pos, can_read);
                        rc += can_read;
                        fp->cur_pos = 0;
                        fp->cur_block++;
                        BlockAddr next = bm.GetBlock(&fp->master->in,
                                                     fp->cur_block);
                        bm.UnmapBlock(fp->block);
                        fp->block = (char*)bm.ReadBlock(next);
                        len -= can_read;
                }
        }
        return rc;
}

ssize_t IVFS::Write(File *fp, const char *buf, size_t len)
{
        if (!fp->master->perm_write) {
                fputs("File opened in read-only mode", stderr);
                return 0;
        }
        size_t wc = 0;
        while (len > 0) {
                size_t can_write = bm.BlockSize() - fp->cur_pos;
                if (len < can_write) {
                        memcpy(fp->block + fp->cur_pos, buf + wc, len);
                        fp->cur_pos += len;
                        fp->master->in.byte_size += len;
                        wc += len;
                        len = 0;
                } else {
                        memcpy(fp->block + fp->cur_pos, buf + wc, can_write);
                        fp->master->in.byte_size += can_write;
                        wc += can_write;
                        fp->cur_pos = 0;
                        fp->cur_block++;
                        BlockAddr next = bm.AddBlock(&fp->master->in);
                        bm.UnmapBlock(fp->block);
                        fp->block = (char*)bm.ReadBlock(next);
                        len -= can_write;
                }
        }
        return wc;
}

off_t IVFS::Lseek(File *fp, off_t offset, int whence)
{
        off_t new_pos, pos = fp->cur_block * bm.BlockSize() + fp->cur_pos;
        off_t end_pos = fp->master->in.blk_size - 1;
        off_t old_block = fp->cur_block;
        switch (whence) {
        case 0:
                new_pos = offset;
                break;
        case 1:
                new_pos = pos + offset;
                break;
        case 2:
                new_pos = end_pos + offset;
                break;
        default:
                new_pos = pos;
        }
        if (new_pos > end_pos)
                new_pos = end_pos;
        if (new_pos < 0)
                new_pos = 0;
        fp->cur_block = new_pos / bm.BlockSize();
        fp->cur_pos = new_pos % bm.BlockSize();
        if (fp->cur_block != old_block) {
                bm.UnmapBlock(fp->block);
                BlockAddr addr = bm.GetBlock(&fp->master->in, fp->cur_block);
                fp->block = (char*)bm.ReadBlock(addr);
        }
        return new_pos;
}

void IVFS::RecursiveDeletion(int idx)
{
        Inode in;
        im.ReadInode(&in, idx);
        if (in.is_dir) {
                DirRecordList *ls = ReadDirectory(&in);
                for (DirRecordList *tmp = ls; tmp; tmp = tmp->next)
                        RecursiveDeletion(tmp->inode_idx);
                FreeDirRecordList(ls);
        }
        OpenedFile *ofptr = SearchOpenedFile(idx);
        if (ofptr) {
                ofptr->defer_delete = true;
        } else {
                bm.FreeBlocks(&in);
        }
        im.FreeInode(idx);
}

OpenedFile *IVFS::OpenFile(int idx, bool want_read, bool want_write)
{
        OpenedFile *ofptr = SearchOpenedFile(idx);
        if (ofptr) {
                if (!ofptr->perm_write && !want_write) {
                        ofptr->opened++;
                        return ofptr;
                }
                return 0;
        }
        ofptr = AddOpenedFile(idx, want_read, want_write);
        return ofptr;
}

OpenedFile *IVFS::AddOpenedFile(int idx, bool want_read, bool want_write)
{
        OpenedFileItem *tmp = new OpenedFileItem;
        tmp->file = new OpenedFile;
        tmp->file->opened = 1;
        tmp->file->perm_read = want_read;
        tmp->file->perm_write = want_write;
        tmp->file->defer_delete = false;
        tmp->file->inode_idx = idx;
        im.ReadInode(&tmp->file->in, idx);
        tmp->next = first;
        first = tmp;
        return tmp->file;
}

OpenedFile *IVFS::SearchOpenedFile(int idx) const
{
        OpenedFileItem *tmp;
        for (tmp = first; tmp; tmp = tmp->next) {
                if (tmp->file->inode_idx == idx)
                        return tmp->file;
        }
        return 0;
}

void IVFS::DeleteOpenedFile(OpenedFile *ofptr)
{
        OpenedFileItem **ptr = &first;
        while (*ptr) {
                if ((*ptr)->file == ofptr) {
                        OpenedFileItem *tmp = *ptr;
                        *ptr = (*ptr)->next;
                        delete tmp->file;
                        delete tmp;
                } else {
                        ptr = &(*ptr)->next;
                }
        }
}

int IVFS::SearchInode(const char *path, bool create_perm, bool mkdr)
{
        int dir_idx = 0, idx = 0;
        char filename[max_name_len];
        while (*path) {
                path = PathParsing(path, filename);
                fprintf(stderr, "Searching for file <%s> ", filename);
                fprintf(stderr, "in directory %d...\n", dir_idx);
                idx = SearchFileInDir(dir_idx, filename);
                if (idx == -1) {
                        fprintf(stderr, "File <%s> not found\n", filename);
                        if (!create_perm) {
                                fputs("Creation is not permitted\n", stderr);
                                return -1;
                        }
                        idx = CreateFileInDir(dir_idx, filename, *path || mkdr);
                        fprintf(stderr, "Created: %s [%d]\n", filename, idx); 
                }
                dir_idx = idx;
        }
        return idx;
}

int IVFS::SearchFileInDir(int dir_idx, const char *name)
{
        int retval = -1;
        Inode dir;
        im.ReadInode(&dir, dir_idx);
        if (!dir.is_dir) {
                fprintf(stderr, "%d not directory\n", dir_idx);
                return -1;
        }
        DirRecordList *ptr = ReadDirectory(&dir);
        fputs("Files in current directory:\n", stderr);
        for (DirRecordList *tmp = ptr; tmp; tmp = tmp->next) {
                fprintf(stderr, "%s [%d]\n", tmp->filename, tmp->inode_idx);
        }
        for (DirRecordList *tmp = ptr; tmp; tmp = tmp->next) {
                if (!strcmp(tmp->filename, name)) {
                        retval = tmp->inode_idx;
                        fprintf(stderr, "Found: <%s> [%d]\n",
                                tmp->filename, tmp->inode_idx);
                        break;
                }
        }
        FreeDirRecordList(ptr);
        return retval;
}

int IVFS::CreateFileInDir(int dir_idx, const char *name, bool is_dir)
{
        Inode in;
        memset(&in, 0, sizeof(in));
        in.is_busy = true;
        in.is_dir = is_dir;
        in.byte_size = 0;
        in.blk_size = 0;
        bm.AddBlock(&in);
        int idx = im.GetInode();
        im.WriteInode(&in, idx);
        CreateDirRecord(dir_idx, name, idx);
        if (is_dir) {
                DirRecord *arr = (DirRecord*)bm.ReadBlock(in.block[0]);
                memset(arr, 0, bm.BlockSize());
                bm.UnmapBlock(arr);
        }
        return idx;
}

void IVFS::CreateDirRecord(int dir_idx, const char *filename, int inode_idx)
{
        Inode dir;
        im.ReadInode(&dir, dir_idx);
        for (off_t i = 0; i < dir.blk_size; i++) {
                BlockAddr addr = bm.GetBlock(&dir, i);
                DirRecord *arr = (DirRecord*)bm.ReadBlock(addr);
                for (size_t j = 0; j < bm.BlockSize() / sizeof(*arr); j++) {
                        if (!arr[j].name[0]) {
                                strcpy(arr[j].name, filename);
                                sprintf(arr[j].idx, "%d", inode_idx);
                                bm.UnmapBlock(arr);
                                return;
                        }
                }
                bm.UnmapBlock(arr);
        }
        BlockAddr addr = bm.AddBlock(&dir);
        DirRecord *arr = (DirRecord*)bm.ReadBlock(addr);
        memset(arr, 0, bm.BlockSize());
        strcpy(arr[0].name, filename);
        sprintf(arr[0].idx, "%d", inode_idx);
        bm.UnmapBlock(arr);
        im.WriteInode(&dir, dir_idx);
}

void IVFS::DeleteDirRecord(int dir_idx, const char *filename)
{
        Inode dir;
        im.ReadInode(&dir, dir_idx);
        for (off_t i = 0; i < dir.blk_size; i++) {
                BlockAddr addr = bm.GetBlock(&dir, i);
                DirRecord *arr = (DirRecord*)bm.ReadBlock(addr);
                for (size_t j = 0; j < bm.BlockSize() / sizeof(*arr); j++) {
                        if (!strcmp(arr[j].name, filename)) {
                                memset(&arr[j], 0, sizeof(arr[j]));
                                bm.UnmapBlock(arr);
                                return;
                        }
                }
                bm.UnmapBlock(arr);
        }
        im.WriteInode(&dir, dir_idx);
}

DirRecordList *IVFS::ReadDirectory(Inode *dir)
{
        DirRecordList *retval = 0;
        for (off_t i = 0; i < dir->blk_size; i++) {
                BlockAddr addr = bm.GetBlock(dir, i);
                DirRecord *arr = (DirRecord*)bm.ReadBlock(addr);
                for (size_t j = 0; j < bm.BlockSize() / sizeof(*arr); j++) {
                        if (!arr[j].name[0])
                                continue;
                        DirRecordList *tmp = new DirRecordList;
                        tmp->filename = strdup(arr[j].name);
                        tmp->inode_idx = atoi(arr[j].idx);
                        tmp->next = retval;
                        retval = tmp;
                }
                bm.UnmapBlock(arr);
        }
        return retval;
}

void IVFS::CreateRootDirectory()
{
        Inode root;
        memset(&root, 0, sizeof(root));
        root.is_busy = true;
        root.is_dir = true;
        root.byte_size = 0;
        root.blk_size = 0;
        bm.AddBlock(&root);
        im.WriteInode(&root, 0);
}

bool IVFS::IsDirectory(int idx)
{
        Inode in;
        im.ReadInode(&in, idx);
        return in.is_dir;
}

void IVFS::FreeDirRecordList(DirRecordList *ptr)
{
        while (ptr) {
                DirRecordList *tmp = ptr;
                ptr = ptr->next;
                free((void*)tmp->filename);
                delete tmp;
        }
}

void IVFS::CreateFileSystem(int dir_fd)
{
        InodeManager::CreateInodeSpace(dir_fd);
        BlockManager::CreateBlockSpace(dir_fd);
        BlockManager::CreateFreeBlockArray(dir_fd);
}

const char *IVFS::PathParsing(const char *path, char *file)
{
        if (*path == '/')
                path++;
        for (; *path && *path != '/'; path++, file++)
                *file = *path;
        *file = 0;
        return path;
}

void IVFS::GetDirectory(const char *path, char *dir, char *file)
{
        const char *s;
        for (s = strrchr(path, '/'); path != s; path++, dir++)
                *dir = *path;
        *dir = 0;
        for (path++; *path; path++, file++)
                *file = *path;
        *file = 0;
}

bool IVFS::CheckPath(const char *path)
{
        if (*path != '/')
                return false;
        int len = 0;
        for (path++; *path; path++) {
                if (*path == '/') {
                        if (len == 0 || len > max_name_len)
                                return false;
                        len = 0;
                        continue;
                }
                if (!isalnum(*path) && *path != '_' && *path != '.')
                        return false;
                len++;
        }
        return len > 0 && len <= max_name_len;
}

bool IVFS::ParseOpenFlags(const char *flag, FileOpenFlags &opf)
{
        for (; *flag; flag++) {
                switch (*flag) {
                case 'r':
                        opf.r_flag = true;
                        break;
                case 'w':
                        opf.w_flag = true;
                        break;
                case 'a':
                        opf.a_flag = true;
                        break;
                case 'c':
                        opf.c_flag = true;
                        break;
                case 't':
                        opf.t_flag = true;
                        break;
                default:
                        fprintf(stderr, "Unknown flag: %c\n", *flag);
                        return false;
                }
        }
        return true;
}

