#include <cstdio>
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

File IVFS::Open(const char *path, const char *perm)
{
        return NewFile(path, perm[0] == 'w');
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

File IVFS::NewFile(const char *path, bool write_perm)
{
        if (!CheckPath(path)) {
                fprintf(stderr, "Bad path: %s\n", path);
                return File();
        }
        pthread_mutex_lock(&mtx);
        OpenedFile *ofptr = OpenFile(path, write_perm);
        pthread_mutex_unlock(&mtx);
        if (!ofptr)
                return File();
        if (write_perm && ofptr->in.byte_size) {
                FreeBlocks(&ofptr->in);
                ofptr->in.byte_size = 0;
                ofptr->in.blk_size = 1;
                ofptr->in.block[0] = bm.AllocateBlock();
        }
        char *first_block = (char*)bm.ReadBlock(ofptr->in.block[0]);
        return File(ofptr, this, first_block);
}

OpenedFile *IVFS::OpenFile(const char *path, bool write_perm)
{
        uint32_t idx = SearchInode(path, write_perm);
        if (idx == -1U) {
                fputs("File's inode not found\n", stderr);
                return 0;
        }
        Inode in;
        im.ReadInode(&in, idx);
        if (in.is_dir) {
                fputs("Open directory is not permitted\n", stderr);
                return 0;
        }
        OpenedFile *ofptr = SearchOpenedFile(idx);
        if (ofptr) {
                if (ofptr->read_only && !write_perm) {
                        ofptr->opened++;
                        return ofptr;
                }
                fputs("Incompatible file open mode\n", stderr);
                return 0;
        }
        ofptr = AddOpenedFile(idx, write_perm);
        return ofptr;
}

OpenedFile *IVFS::AddOpenedFile(uint32_t idx, bool write_perm)
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
                        files[i]->read_only = !write_perm;
                        files[i]->inode_idx = idx;
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

OpenedFile *IVFS::SearchOpenedFile(uint32_t idx) const
{
        for (size_t i = 0; i < arr_size; i++) {
                if (files[i] && files[i]->inode_idx == idx)
                        return files[i];
        }
        return 0;
}

uint32_t IVFS::SearchInode(const char *path, bool write_perm)
{
        uint32_t dir_idx = 0, idx;
        char filename[MAX_FILENAME_LEN + 1];
        Inode dir;
        do {
                path = PathParsing(path, filename);
                fprintf(stderr, "Searching for file <%s> in directory %d...\n",
                        filename, dir_idx);
                im.ReadInode(&dir, dir_idx);
                if (!dir.is_dir) {
                        fprintf(stderr, "%d not directory\n", dir_idx);
                        return -1;
                }
                idx = SearchFileInDir(&dir, filename);
                if (idx == 0xFFFFFFFF) {
                        fprintf(stderr, "File <%s> not found\n", filename);
                        if (!write_perm) {
                                fputs("Creation is not permitted\n", stderr);
                                return -1;
                        }
                        idx = CreateFileInDir(&dir, filename, path);
                        im.WriteInode(&dir, dir_idx);
                }
                dir_idx = idx;
        } while (path);
        return idx;
}

uint32_t IVFS::SearchFileInDir(Inode *dir, const char *name) const
{
        uint32_t retval = -1;
        DirRecordList *ptr = ReadDirectory(dir);
        fputs("Files in current directory:\n", stderr);
        for (DirRecordList *tmp = ptr; tmp; tmp = tmp->next) {
                fprintf(stderr, "%s [%d]\n",
                        tmp->rec.filename, tmp->rec.inode_idx);
        }
        for (DirRecordList *tmp = ptr; tmp; tmp = tmp->next) {
                if (!strcmp(tmp->rec.filename, name)) {
                        retval = tmp->rec.inode_idx;
                        fprintf(stderr, "Found: <%s> [%d]\n",
                                tmp->rec.filename, tmp->rec.inode_idx);
                        break;
                }
        }
        FreeDirRecordList(ptr);
        return retval;
}

uint32_t IVFS::CreateFileInDir(Inode *dir, const char *name, bool is_dir)
{
        Inode in;
        memset(&in, 0, sizeof(in));
        in.is_busy = true;
        in.is_dir = is_dir;
        in.byte_size = 0;
        in.blk_size = 1;
        in.block[0] = bm.AllocateBlock();
        uint32_t idx = im.GetInode();
        im.WriteInode(&in, idx);
        MakeDirRecord(dir, name, idx);
        fprintf(stderr, "Created file: %s [%d]\n", name, idx);
        return idx;
}

DirRecordList *IVFS::ReadDirectory(Inode *dir) const
{
        DirRecordList *tmp, *ptr = 0;
        DirRecord *arr;
        size_t records_amount = dir->byte_size / sizeof(DirRecord);
        size_t viewed = 0;
        for (off_t i = 0; i < dir->blk_size; i++) {
                BlockAddr addr = GetBlockNum(dir, i);
                arr = (DirRecord*)bm.ReadBlock(addr);
                for (uint32_t j = 0; j < dirr_in_block; j++, viewed++) {
                        if (viewed == records_amount)
                                break;
                        tmp = new DirRecordList;
                        tmp->rec = arr[j];
                        tmp->next = ptr;
                        ptr = tmp;
                }
                bm.UnmapBlock(arr);
        }
        return ptr;
}

void IVFS::FreeDirRecordList(DirRecordList *ptr) const
{
        while (ptr) {
                DirRecordList *tmp = ptr;
                ptr = ptr->next;
                delete tmp;
        }
}

void IVFS::MakeDirRecord(Inode *dir, const char *name, uint32_t idx)
{
        DirRecord rec;
        strncpy(rec.filename, name, sizeof(rec.filename));
        rec.inode_idx = idx;
        AppendDirRecord(dir, &rec);
}

void IVFS::AppendDirRecord(Inode *dir, DirRecord *rec)
{
        BlockAddr addr = GetBlockNum(dir, dir->blk_size - 1);
        DirRecord *arr = (DirRecord*)bm.ReadBlock(addr);
        size_t last_rec = (dir->byte_size % block_size) / sizeof(DirRecord);
        arr[last_rec] = *rec;
        dir->byte_size += sizeof(DirRecord);
        bm.UnmapBlock(arr);
        if (last_rec == dirr_in_block - 1)
                AddBlock(dir);
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
        for (uint32_t i = 0; i < in->blk_size; i++)
                bm.FreeBlock(GetBlockNum(in, i));
        if (in->blk_size > 8)
                bm.FreeBlock(in->block[8]);
        if (in->blk_size > 8 + addr_in_block) {
                BlockAddr *block_lev2 = (BlockAddr*)bm.ReadBlock(in->block[9]);
                uint32_t r = (in->blk_size - 8 - addr_in_block) / addr_in_block;
                for (uint32_t i = 0; i < r + 1; i++)
                        bm.FreeBlock(block_lev2[i]);
                bm.UnmapBlock(block_lev2);
                bm.FreeBlock(in->block[9]);
        }
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
                        if (len == 0 || len > MAX_FILENAME_LEN)
                                return false;
                        len = 0;
                        continue;
                }
                if (!isalnum(*path) && *path != '_' && *path != '.')
                        return false;
                len++;
        }
        return len > 0 && len <= MAX_FILENAME_LEN;
}

