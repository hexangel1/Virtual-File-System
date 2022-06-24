#include <iostream>
#include <cstring>
#include <ctype.h>
#include "ivfs.hpp"

IVFS::IVFS() : arr_size(8), arr_used(0)
{
        files = new OpenedFile*[arr_size];
        for (size_t i = 0; i < arr_size; i++)
                files[i] = 0;
}

IVFS::~IVFS()
{
        for (size_t i = 0; i < arr_size; i++) {
                if (files[i]) {
                        im.WriteInode(&files[i]->in, files[i]->inode_idx);
                        delete files[i];
                }
        }
        delete []files;
}

bool IVFS::Init(bool makefs)
{
        int res;
        if (makefs)
                CreateFileSystem();
        res = im.Init();
        if (!res) {
                std::cerr << "Failed to start InodeManager" << std::endl;
                return false;
        }
        res = bm.Init();
        if (!res) {
                std::cerr << "Failed to start BlockManager" << std::endl;
                return false;
        }
        if (makefs)
                CreateRootDirectory();
        std::cerr << "Virtual File System launched successfully" << std::endl;
        return true;
}

File *IVFS::Open(const char *path)
{
        return NewFile(path, false);
}

File *IVFS::Create(const char *path)
{
        return NewFile(path, true);
}

void IVFS::Close(File *f)
{
        if (!f)
                return;
        bm.UnmapBlock(f->block);
        mtx.lock();
        f->master->opened--;
        if (f->master->opened == 0) {
                im.WriteInode(&f->master->in, f->master->inode_idx);
                DeleteOpenedFile(f->master);
        }
        mtx.unlock();
        delete f;
}

size_t IVFS::Read(File *f, char *buf, size_t len) const
{
        if (!f->master->read_only) {
                std::cerr << "File opened in write-only mode" << std::endl;
                return 0;
        }
        size_t was_read = f->cur_block * block_size + f->cur_pos;
        size_t rc = 0;
        if (len > f->master->in.byte_size - was_read)
                len = f->master->in.byte_size - was_read;
        while (len > 0) {
                size_t can_read = block_size - f->cur_pos;
                if (len < can_read) {
                        memcpy(buf + rc, f->block + f->cur_pos, len);
                        f->cur_pos += len;
                        rc += len;
                        len = 0;
                } else {
                        memcpy(buf + rc, f->block + f->cur_pos, can_read);
                        rc += can_read;
                        f->cur_pos = 0;
                        f->cur_block++;
                        bm.UnmapBlock(f->block);
                        BlockAddr next = GetBlockNum(&f->master->in,
                                                     f->cur_block);
                        f->block = (char*)bm.ReadBlock(next);
                        len -= can_read;
                }
        }
        return rc;
}

size_t IVFS::Write(File *f, const char *buf, size_t len) const
{
        if (f->master->read_only) {
                std::cerr << "File opened in read only mode" << std::endl;
                return 0;
        }
        size_t wc = 0;
        while (len > 0) {
                size_t can_write = block_size - f->cur_pos;
                if (len < can_write) {
                        memcpy(f->block + f->cur_pos, buf + wc, len);
                        f->cur_pos += len;
                        f->master->in.byte_size += len;
                        wc += len;
                        len = 0;
                } else {
                        memcpy(f->block + f->cur_pos, buf + wc, can_write);
                        f->master->in.byte_size += can_write;
                        wc += can_write;
                        f->cur_pos = 0;
                        f->cur_block++;
                        bm.UnmapBlock(f->block);
                        BlockAddr next = AddBlock(&f->master->in);
                        f->block = (char*)bm.ReadBlock(next);
                        len -= can_write;
                }
        }
        return wc;
}

File *IVFS::NewFile(const char *path, bool write_perm)
{
        if (!CheckPath(path)) {
                std::cerr << "Bad path: " << path << std::endl;
                return nullptr;
        }
        mtx.lock();
        OpenedFile *ofptr = OpenFile(path, write_perm);
        mtx.unlock();
        if (!ofptr)
                return nullptr;
        if (write_perm && ofptr->in.byte_size) {
                FreeBlocks(&ofptr->in);
                ofptr->in.byte_size = 0;
                ofptr->in.blk_size = 1;
                ofptr->in.block[0] = bm.AllocateBlock();
        }
        File *f = new File;
        f->master = ofptr;
        f->cur_pos = 0;
        f->cur_block = 0;
        f->block = (char*)bm.ReadBlock(f->master->in.block[0]);
        return f;
}

OpenedFile *IVFS::OpenFile(const char *path, bool write_perm)
{
        uint32_t idx = SearchInode(path, write_perm);
        if (idx == -1U) {
                std::cerr << "File's inode not found" << std::endl;
                return nullptr;
        }
        Inode in;
        im.ReadInode(&in, idx);
        if (in.is_dir) {
                std::cerr << "Open directory is not permitted" << std::endl;
                return nullptr;
        }
        OpenedFile *ofptr = SearchOpenedFile(idx);
        if (ofptr) {
                if (ofptr->read_only && !write_perm) {
                        ofptr->opened++;
                        return ofptr;
                }
                std::cerr << "Incompatible file open mode" << std::endl;
                return nullptr;
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
                        tmp[i] = i < arr_size ? files[i] : nullptr;
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
        return nullptr;
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
        return nullptr;
}

uint32_t IVFS::SearchInode(const char *path, bool write_perm)
{
        uint32_t dir_idx = 0, idx;
        char filename[MAX_FILENAME_LEN + 1];
        Inode dir;
        do {
                path = PathParsing(path, filename);
                std::cerr << "Searching for file <" << filename <<
                             "> in directory " << dir_idx << "..." << std::endl;
                im.ReadInode(&dir, dir_idx);
                if (!dir.is_dir) {
                        std::cerr << dir_idx << " not directory" << std::endl;
                        return -1;
                }
                idx = SearchFileInDir(&dir, filename);
                if (idx == 0xFFFFFFFF) {
                        std::cerr << "File <" << filename << "> not found\n";
                        if (!write_perm) {
                                std::cerr << "Creation is not permitted\n";
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
        std::cerr << "Files in current directory:" << std::endl;
        for (DirRecordList *tmp = ptr; tmp; tmp = tmp->next) {
                std::cerr << tmp->rec.filename << " [";
                std::cerr << tmp->rec.inode_idx << "]" << std::endl;
        }
        for (DirRecordList *tmp = ptr; tmp; tmp = tmp->next) {
                if (!strcmp(tmp->rec.filename, name)) {
                        retval = tmp->rec.inode_idx;
                        std::cerr << "Found: <" << tmp->rec.filename << "> [";
                        std::cerr << tmp->rec.inode_idx << "]" << std::endl;
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
        std::cerr << "Created file: " << name << " ["<< idx << "]" << std::endl;
        return idx;
}

DirRecordList *IVFS::ReadDirectory(Inode *dir) const
{
        DirRecordList *tmp, *ptr = 0;
        DirRecord *arr;
        size_t records_amount = dir->byte_size / sizeof(DirRecord);
        size_t viewed = 0;
        for (size_t i = 0; i < dir->blk_size; i++) {
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

void IVFS::MakeDirRecord(Inode *dir, const char *name, uint32_t idx) const
{
        DirRecord rec;
        strncpy(rec.filename, name, sizeof(rec.filename));
        rec.inode_idx = idx;
        AppendDirRecord(dir, &rec);
}

void IVFS::AppendDirRecord(Inode *dir, DirRecord *rec) const
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

BlockAddr IVFS::AddBlock(Inode *in) const
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

void IVFS::AddBlockToLev1(Inode *in, BlockAddr new_block) const
{
        if (in->blk_size == 8)
                in->block[8] = bm.AllocateBlock();
        BlockAddr *block_lev1 = (BlockAddr*)bm.ReadBlock(in->block[8]);
        block_lev1[in->blk_size - 8] = new_block;
        bm.UnmapBlock(block_lev1);
}

void IVFS::AddBlockToLev2(Inode *in, BlockAddr new_block) const
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

void IVFS::FreeBlocks(Inode *in) const
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

void IVFS::CreateRootDirectory() const
{
        Inode root;
        memset(&root, 0, sizeof(root));
        root.is_busy = true;
        root.is_dir = true;
        root.byte_size = 0;
        root.blk_size = 1;
        root.block[0] = bm.AllocateBlock();
        im.WriteInode(&root, 0);
        std::cerr << "Created root directory '/' [0]" << std::endl;
}

void IVFS::CreateFileSystem()
{
        InodeManager::CreateInodeSpace();
        BlockManager::CreateBlockSpace();
        BlockManager::CreateFreeBlockArray();
}

const char *IVFS::PathParsing(const char *path, char *filename)
{
        if (*path == '/')
                path++;
        for (; *path && *path != '/'; path++, filename++)
                *filename = *path;
        *filename = 0;
        return *path == '/' ? path + 1 : nullptr;
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

