#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include "ivfs.hpp"

IVFS::IVFS() : arr_size(8), arr_used(0), dir_fd(-1)
{
        mtx = PTHREAD_MUTEX_INITIALIZER;
        files = new OpenedFile*[arr_size];
        for (size_t i = 0; i < arr_size; i++)
                files[i] = 0;
}

IVFS::~IVFS()
{
        if (dir_fd != -1)
                close(dir_fd);
        for (size_t i = 0; i < arr_size; i++) {
                if (files[i]) {
                        im.WriteInode(&files[i]->in, files[i]->inode_idx);
                        delete files[i];
                }
        }
        delete []files;
}

bool IVFS::Mount(const char *path, bool makefs)
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

void IVFS::Umount()
{

}

File IVFS::Open(const char *path, const char *flags)
{
        FileOpenFlags opf = { false, false, false, false, false };
        if (!ParseOpenFlags(flags, opf))
                return File();
        if (!CheckPath(path)) {
                fprintf(stderr, "Bad path: %s\n", path);
                return File();
        }
        OpenedFile *ofptr = 0;
        pthread_mutex_lock(&mtx);
        int idx = SearchInode(path, opf.c_flag);
        if (idx == -1) {
                fputs("File's inode not found\n", stderr);
                goto unlock_mutex;
        }
        if (IsDirectory(idx)) {
                fputs("Open directory is not permitted\n", stderr);
                goto unlock_mutex;
        }
        ofptr = OpenFile(idx, opf.r_flag, opf.w_flag);
unlock_mutex:
        pthread_mutex_unlock(&mtx);
        if (!ofptr)
                return File();
        if (opf.t_flag && ofptr->in.byte_size > 0) {
                FreeBlocks(&ofptr->in);
                ofptr->in.byte_size = 0;
                ofptr->in.blk_size = 1;
                ofptr->in.block[0] = bm.AllocateBlock();
        }
        char *first_block = (char*)bm.ReadBlock(ofptr->in.block[0]);
        return File(ofptr, first_block);
}

bool IVFS::Remove(const char *path, bool recursive)
{
        char dirname[File::max_name_len];
        char filename[File::max_name_len];
        if (!CheckPath(path)) {
                fprintf(stderr, "Bad path: %s\n", path);
                return false;
        }
        GetDirectory(path, dirname, filename);
        int dir_idx = dirname[0] ? SearchInode(dirname, false) : 0;
        int idx = SearchInode(filename, false);
        if (recursive) {
                RecursiveDeletion(idx);
        } else {
                if (IsDirectory(idx)) {
                        fprintf(stderr, "Use recursive = true\n");
                        return false;
                }
                DeleteFile(idx);
        }
        Inode dir_in;
        im.ReadInode(&dir_in, dir_idx); 
        DeleteDirRecord(&dir_in, filename);
        im.WriteInode(&dir_in, dir_idx);
        return true;
}

void IVFS::RecursiveDeletion(int idx)
{
        Inode in;
        im.ReadInode(&in, idx);
        if (in.is_dir) {
                DirRecordList *ls = ReadDirectory(&in);
                for (DirRecordList *tmp = ls; tmp; tmp = tmp->next)
                        RecursiveDeletion(ls->inode_idx);
                FreeDirRecordList(ls);
        }
        DeleteFile(idx);
}

void IVFS::DeleteFile(int idx)
{
        Inode in;
        im.ReadInode(&in, idx);
        FreeBlocks(&in);
        im.FreeInode(idx);
}

void IVFS::CloseFile(OpenedFile *ofptr)
{
        pthread_mutex_lock(&mtx);
        ofptr->opened--;
        if (ofptr->opened == 0) {
                im.WriteInode(&ofptr->in, ofptr->inode_idx);
                DeleteOpenedFile(ofptr);
        }
        pthread_mutex_unlock(&mtx);
}

bool IVFS::IsDirectory(int idx)
{
        Inode in;
        im.ReadInode(&in, idx);
        return in.is_dir;
}

OpenedFile *IVFS::OpenFile(int idx, bool want_read, bool want_write)
{
        OpenedFile *ofptr = SearchOpenedFile(idx);
        if (ofptr) {
                if (!ofptr->perm_write && !want_write) {
                        ofptr->opened++;
                        return ofptr;
                }
                fputs("Incompatible file open mode\n", stderr);
                return 0;
        }
        ofptr = AddOpenedFile(idx, want_read, want_write);
        return ofptr;
}

OpenedFile *IVFS::AddOpenedFile(int idx, bool want_read, bool want_write)
{
        if (arr_used == arr_size) {
                size_t new_size = arr_size * 2;
                OpenedFile **tmp = new OpenedFile*[new_size];
                for (size_t i = 0; i < new_size; i++)
                        tmp[i] = i < arr_size ? files[i] : 0;
                delete []files;
                files = tmp;
                arr_size = new_size;
        }
        for (size_t i = 0; i < arr_size; i++) {
                if (!files[i]) {
                        files[i] = new OpenedFile;
                        arr_used++;
                        files[i]->opened = 1;
                        files[i]->perm_read = want_read;
                        files[i]->perm_write = want_write;
                        files[i]->inode_idx = idx;
                        files[i]->vfs = this;
                        im.ReadInode(&files[i]->in, idx);
                        return files[i];
                }
        }
        return 0;
}

void IVFS::DeleteOpenedFile(OpenedFile *ofptr)
{
        for (size_t i = 0; i < arr_size; i++) {
                if (files[i] == ofptr) {
                        delete files[i];
                        files[i] = 0;
                        arr_used--;
                        break;
                }
        }
}

OpenedFile *IVFS::SearchOpenedFile(int idx) const
{
        for (size_t i = 0; i < arr_size; i++) {
                if (files[i] && files[i]->inode_idx == idx)
                        return files[i];
        }
        return 0;
}

int IVFS::SearchInode(const char *path, bool create_perm)
{
        int dir_idx = 0, idx;
        char filename[File::max_name_len];
        while (path) {
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
                        idx = CreateFileInDir(dir_idx, filename, path);
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
        Inode in, dir;
        memset(&in, 0, sizeof(in));
        in.is_busy = true;
        in.is_dir = is_dir;
        in.byte_size = 0;
        in.blk_size = 1;
        in.block[0] = bm.AllocateBlock();
        int idx = im.GetInode();
        im.WriteInode(&in, idx);
        im.ReadInode(&dir, dir_idx);
        CreateDirRecord(&dir, name, idx);
        im.WriteInode(&dir, dir_idx);
        if (is_dir) {
                DirRecord *arr = (DirRecord*)bm.ReadBlock(in.block[0]);
                memset(arr, 0, block_size);
                bm.UnmapBlock(arr);
        }
        fprintf(stderr, "Created file: %s [%d]\n", name, idx);
        return idx;
}

DirRecordList *IVFS::ReadDirectory(Inode *dir) const
{
        DirRecordList *retval = 0;
        for (off_t i = 0; i < dir->blk_size; i++) {
                BlockAddr addr = GetBlockNum(dir, i);
                DirRecord *arr = (DirRecord*)bm.ReadBlock(addr);
                for (off_t j = 0; j < dirr_in_block; j++) {
                        if (!arr[j].name[0])
                                continue;
                        DirRecordList *tmp = new DirRecordList;
                        tmp->filename = Strdup(arr[j].name);
                        tmp->inode_idx = atoi(arr[j].idx);
                        tmp->next = retval;
                        retval = tmp;
                }
                bm.UnmapBlock(arr);
        }
        return retval;
}

void IVFS::FreeDirRecordList(DirRecordList *ptr) const
{
        while (ptr) {
                DirRecordList *tmp = ptr;
                ptr = ptr->next;
                delete[] tmp->filename;
                delete tmp;
        }
}

void IVFS::CreateDirRecord(Inode *dir, const char *filename, uint32_t inode_idx)
{
        for (off_t i = 0; i < dir->blk_size; i++) {
                BlockAddr addr = GetBlockNum(dir, i);
                DirRecord *arr = (DirRecord*)bm.ReadBlock(addr);
                for (off_t j = 0; j < dirr_in_block; j++) {
                        if (!arr[j].name[0]) {
                                memset(&arr[j], 0, sizeof(arr[j]));
                                strcpy(arr[j].name, filename);
                                sprintf(arr[j].idx, "%d", inode_idx);
                                bm.UnmapBlock(arr);
                                return;
                        }
                }
                bm.UnmapBlock(arr);
        }
        BlockAddr addr = AddBlock(dir);
        DirRecord *arr = (DirRecord*)bm.ReadBlock(addr);
        memset(arr, 0, block_size);
        strcpy(arr[0].name, filename);
        sprintf(arr[0].idx, "%d", inode_idx);
        bm.UnmapBlock(arr);
}

void IVFS::DeleteDirRecord(Inode *dir, const char *filename)
{
        for (off_t i = 0; i < dir->blk_size; i++) {
                BlockAddr addr = GetBlockNum(dir, i);
                DirRecord *arr = (DirRecord*)bm.ReadBlock(addr);
                for (off_t j = 0; j < dirr_in_block; j++) {
                        if (!strcmp(arr[j].name, filename)) {
                                memset(&arr[j], 0, sizeof(arr[j]));
                                bm.UnmapBlock(arr);
                                return;
                        }
                }
                bm.UnmapBlock(arr);
        }
}

BlockAddr IVFS::GetBlockNum(Inode *in, uint32_t num) const
{
        BlockAddr retval;
        if (num < 8) {
                retval = in->block[num];
        } else if (num >= 8 && num < 8 + addr_in_block) {
                BlockAddr *lev1 = (BlockAddr*)bm.ReadBlock(in->block[8]);
                retval = lev1[num - 8];
                bm.UnmapBlock(lev1);
        } else {
                uint32_t idx1 = (num - 8 - addr_in_block) / addr_in_block;
                uint32_t idx0 = (num - 8 - addr_in_block) % addr_in_block;
                BlockAddr *lev2 = (BlockAddr*)bm.ReadBlock(in->block[9]);
                BlockAddr *lev1 = (BlockAddr*)bm.ReadBlock(lev2[idx1]);
                retval = lev1[idx0];
                bm.UnmapBlock(lev1);
                bm.UnmapBlock(lev2);
        }
        return retval;
}

BlockAddr IVFS::AddBlock(Inode *in)
{
        BlockAddr new_block = bm.AllocateBlock();
        if (in->blk_size < 8)
                in->block[in->blk_size] = new_block;
        else if (in->blk_size >= 8 && in->blk_size < 8 + addr_in_block)
                AddBlockToLev1(in, new_block);
        else if (in->blk_size >= 8 + addr_in_block)
                AddBlockToLev2(in, new_block);
        in->blk_size++;
        return new_block;
}

void IVFS::AddBlockToLev1(Inode *in, BlockAddr new_block)
{
        if (in->blk_size == 8)
                in->block[8] = bm.AllocateBlock();
        BlockAddr *block_lev1 = (BlockAddr*)bm.ReadBlock(in->block[8]);
        block_lev1[in->blk_size - 8] = new_block;
        bm.UnmapBlock(block_lev1);
}

void IVFS::AddBlockToLev2(Inode *in, BlockAddr new_block)
{
        uint32_t lev1_num = (in->blk_size - 8 - addr_in_block) / addr_in_block;
        uint32_t lev0_num = (in->blk_size - 8 - addr_in_block) % addr_in_block;
        if (in->blk_size == 8 + addr_in_block)
                in->block[9] = bm.AllocateBlock();
        BlockAddr *block_lev2 = (BlockAddr*)bm.ReadBlock(in->block[9]);
        if (lev0_num == 0)
                block_lev2[lev1_num] = bm.AllocateBlock();
        BlockAddr *block_lev1 = (BlockAddr*)bm.ReadBlock(block_lev2[lev1_num]);
        block_lev1[lev0_num] = new_block;
        bm.UnmapBlock(block_lev1);
        bm.UnmapBlock(block_lev2);
}

void IVFS::FreeBlocks(Inode *in)
{
        for (off_t i = 0; i < in->blk_size; i++)
                bm.FreeBlock(GetBlockNum(in, i));
        if (in->blk_size > 8) {
                BlockAddr *block_lev1 = (BlockAddr*)bm.ReadBlock(in->block[8]);
                off_t r = in->blk_size - 8;
                if (r > addr_in_block)
                        r = addr_in_block;
                for (off_t i = 0; i < r; i++)
                        bm.FreeBlock(block_lev1[i]);
                bm.UnmapBlock(block_lev1);
                bm.FreeBlock(in->block[8]);
        }
/*        if (in->blk_size > 8 + addr_in_block) {
                BlockAddr *block_lev2 = (BlockAddr*)bm.ReadBlock(in->block[9]);
                off_t r = (in->blk_size - 8 - addr_in_block) / addr_in_block;
                for (off_t i = 0; i < r + 1; i++)
                        bm.FreeBlock(block_lev2[i]);
                bm.UnmapBlock(block_lev2);
                bm.FreeBlock(in->block[9]);
        }*/
        in->byte_size = 0;
        in->blk_size = 0;
}

void IVFS::CreateRootDirectory()
{
        Inode root;
        memset(&root, 0, sizeof(root));
        root.is_busy = true;
        root.is_dir = true;
        root.byte_size = 0;
        root.blk_size = 1;
        root.block[0] = bm.AllocateBlock();
        im.WriteInode(&root, 0);
        fputs("Created root directory '/' [0]\n", stderr);
}

void IVFS::CreateFileSystem(int dir_fd)
{
        InodeManager::CreateInodeSpace(dir_fd);
        BlockManager::CreateBlockSpace(dir_fd);
        BlockManager::CreateFreeBlockArray(dir_fd);
}

const char *IVFS::PathParsing(const char *path, char *filename)
{
        if (*path == '/')
                path++;
        for (; *path && *path != '/'; path++, filename++)
                *filename = *path;
        *filename = 0;
        return *path == '/' ? path + 1 : 0;
}

bool IVFS::CheckPath(const char *path)
{
        if (*path != '/')
                return false;
        int len = 0;
        for (path++; *path; path++) {
                if (*path == '/') {
                        if (len == 0 || len >= File::max_name_len)
                                return false;
                        len = 0;
                        continue;
                }
                if (!isalnum(*path) && *path != '_' && *path != '.')
                        return false;
                len++;
        }
        return len > 0 && len < File::max_name_len;
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

char *IVFS::Strdup(const char *str)
{
        char *copy = new char[strlen(str) + 1];
        strcpy(copy, str);
        return copy;
}

void IVFS::GetDirectory(const char *path, char *dir, char *file)
{
        const char *tmp;
        for (tmp = path; *tmp; tmp++)
                ;
        for (; tmp != path && *tmp != '/'; tmp--)
                ;
        for (; path != tmp; path++, dir++)
                *dir = *path;
        *dir = 0;
        for (path++; *path; path++, file++)
                *file = *path;
        *file = 0;
}

