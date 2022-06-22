#include <cstdio>
#include <cstring>
#include "ivfs.hpp"
#include "blockmanager.hpp"
#include "inodemanager.hpp"

bool IVFS::Init(bool makefs)
{
        int res;
        if (makefs)
                CreateFileSystem();
        res = im.Init();
        if (!res) {
                fprintf(stderr, "Failed to start InodeManager\n");
                return false;
        } 
        res = bm.Init();
        if (!res) {
                fprintf(stderr, "Failed to start BlockManager\n");
                return false;
        }
        if (makefs)
                CreateRootDirectory();
        fprintf(stderr, "Virtual File System launched successfully\n");
        return true;
}

File *IVFS::Open(const char *name)
{
        OpenedFile *ofptr = OpenFile(name, false);
        if (!ofptr)
                return nullptr;
        File *f = new File;
        f->master = ofptr;
        f->cur_pos = 0;
        f->cur_block = 0;
        f->block = (char*)bm.ReadBlock(f->master->in.block[0]);
        return f;
}

File *IVFS::Create(const char *name)
{
        OpenedFile *ofptr = OpenFile(name, true);
        if (!ofptr)
                return nullptr;
        File *f = new File;
        f->master = ofptr;
        f->cur_pos = 0;
        f->cur_block = 0;
        if (ofptr->in.byte_size) {
                FreeBlocks(&f->master->in);
                f->master->in.byte_size = 0;
                f->master->in.blk_size = 1;
                f->master->in.block[0] = bm.AllocateBlock();
        }
        f->block = (char*)bm.ReadBlock(f->master->in.block[0]);
        return f;
}

void IVFS::Close(File *f)
{
        f->master->opened--;
        if (f->master->opened == 0) {
                im.WriteInode(&f->master->in, f->master->inode_idx);
                CloseFile(f->master);
        }
        bm.UnmapBlock(f->block);
        delete f;
}

size_t IVFS::Read(File *f, char *buf, size_t len)
{
        size_t was_read = f->cur_block * block_size + f->cur_pos;
        size_t rc = 0;
        if (len > f->master->in.byte_size - was_read)
                len = f->master->in.byte_size - was_read;
        while (len > 0) {
                size_t can_read = block_size - f->cur_pos;
                if (len < can_read) {
                        memcpy(buf, f->block + f->cur_pos, len);
                        f->cur_pos += len;
                        rc += len;
                        len = 0;
                } else {
                        memcpy(buf, f->block + f->cur_pos, can_read);
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

size_t IVFS::Write(File *f, const char *buf, size_t len)
{
        size_t wc = 0;
        while (len > 0) {
                size_t can_write = block_size - f->cur_pos;
                if (len < can_write) {
                        memcpy(f->block + f->cur_pos, buf, len);
                        f->cur_pos += len;
                        f->master->in.byte_size += len;
                        wc += len;
                        len = 0;
                } else {
                        memcpy(f->block + f->cur_pos, buf, can_write);
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

OpenedFile *IVFS::OpenFile(const char *path, bool w)
{
        uint32_t idx = SearchInode(path, w);
        if (idx == -1U)
                return 0;
        OpenedFile *fptr = 0;
        for (OpenedFileList *tmp = first; tmp; tmp = tmp->next) {
                if (tmp->file->inode_idx == idx) {
                        if (!tmp->file->read_only || w)
                                return 0;
                        fptr = tmp->file;
                        break;
                }
        }
        if (!fptr) {
                OpenedFileList *tmp = new OpenedFileList;
                tmp->file = new OpenedFile;
                tmp->file->opened = 1;
                tmp->file->read_only = !w;
                tmp->file->inode_idx = idx;
                im.ReadInode(&tmp->file->in, idx);
                tmp->next = first;
                first = tmp;
                fptr = tmp->file;
        }
        return fptr;
}

void IVFS::CloseFile(OpenedFile *ofptr)
{
        OpenedFileList **ptr = &first;
        while (*ptr) {
                if ((*ptr)->file == ofptr) {
                        OpenedFileList *tmp = *ptr;
                        *ptr = (*ptr)->next;
                        delete tmp->file;
                        delete tmp;
                } else {
                        ptr = &(*ptr)->next;
                }
        }
}

uint32_t IVFS::SearchInode(const char *path, bool w)
{
        char filename[32];
        Inode dir;
        uint32_t dir_idx = 0, idx;
        do {
                path = GetNextDir(path, filename);
                fprintf(stderr, "searching for %s in dir %d\n", 
                        filename, dir_idx);
                im.ReadInode(&dir, dir_idx);
                if (!dir.is_dir) {
                        fprintf(stderr, "Not a directory\n");
                        return -1;
                }
                idx = SearchFileInDir(&dir, filename);
                if (idx == -1U) {
                        if (!w) {
                                fprintf(stderr, "File %s not found\n",
                                        filename);
                                return -1;
                        }
                        idx = CreateFileInDir(&dir, filename, path);
                        im.WriteInode(&dir, dir_idx);
                }
                fprintf(stderr, "found: %s %d\n", filename, idx);
                dir_idx = idx;
        } while (path);
        return idx;
}

uint32_t IVFS::SearchFileInDir(Inode *dir, const char *name)
{
        uint32_t retval = -1;
        DirRecordList *ptr = ReadDirectory(dir);
        fprintf(stderr, "files in directory:\n");
        for (DirRecordList *tmp = ptr; tmp; tmp = tmp->next)
                fprintf(stderr, "%s\n", tmp->rec.filename); 
        for (DirRecordList *tmp = ptr; tmp; tmp = tmp->next) {
                if (!strcmp(tmp->rec.filename, name)) {
                        retval = tmp->rec.inode_idx;
                        break;
                }
        }
        FreeDirRecordList(ptr);
        return retval;
}

uint32_t IVFS::CreateFileInDir(Inode *dir, const char *name, bool is_dir)
{
        Inode in = {
                .is_busy = true,
                .is_dir = is_dir,
                .byte_size = 0,
                .blk_size = 1
        };
        memset(&in.block, 0, sizeof(in.block)); 
        in.block[0] = bm.AllocateBlock();
        uint32_t idx = im.GetInode();
        im.WriteInode(&in, idx);
        MakeDirRecord(dir, name, idx); 
        fprintf(stderr, "created %s : %d\n", name, idx);
        return idx;
}

DirRecordList *IVFS::ReadDirectory(Inode *dir)
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
                        if (!EmptyRecord(&arr[j])) {
                                tmp = new DirRecordList;
                                tmp->rec = arr[j];
                                tmp->next = ptr;
                                ptr = tmp;
                        }
                }
                bm.UnmapBlock(arr);
        }
        return ptr;
}

void IVFS::FreeDirRecordList(DirRecordList *ptr)
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
/*        bool record_made = false;
        size_t records_amount = dir->byte_size / sizeof(DirRecord); 
        size_t viewed = 0;
        for (size_t i = 0; i < dir->blk_size; i++) {
                BlockAddr addr = GetBlockNum(dir, i);
                DirRecord *arr = (DirRecord*)sm.ReadBlock(addr);
                for (size_t j = 0; j < dirr_in_block; j++, viewed++) {
                        if (viewed == records_amount)
                                break;
                        if (EmptyRecord(&arr[j])) {
                                arr[j] = rec;
                                record_made = true;
                                break;
                        }
                }
                sm.FreeBlock(arr);
                if (record_made || viewed == records_amount)
                        break;
        }
*/
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

BlockAddr IVFS::GetBlockNum(Inode *in, uint32_t num)
{
        BlockAddr retval;
        if (num >= 0 && num < 8) {
                retval = in->block[num];
        } else if (num >= 8 && num < 8 + addr_in_block) {
                BlockAddr *lev1 = (BlockAddr*)bm.ReadBlock(in->block[8]);
                retval = lev1[num - 8];
                bm.UnmapBlock(lev1);
        } else {
                size_t idx1 = (num - 8 - addr_in_block) / addr_in_block;
                size_t idx0 = (num - 8 - addr_in_block) % addr_in_block; 
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
        if (in->blk_size >= 0 && in->blk_size < 8)
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
        size_t lev1_num = (in->blk_size - 8 - addr_in_block) / addr_in_block;
        size_t lev0_num = (in->blk_size - 8 - addr_in_block) % addr_in_block;
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
        for (size_t i = 0; i < in->blk_size; i++)
                bm.FreeBlock(GetBlockNum(in, i));
        in->byte_size = 0;
        in->blk_size = 0;
}

void IVFS::CreateRootDirectory()
{
        Inode root = {
                .is_busy = true,
                .is_dir = true,
                .byte_size = 0,
                .blk_size = 1
        };
        memset(&root.block, 0, sizeof(root.block));
        root.block[0] = bm.AllocateBlock();
        fprintf(stderr, "Created '/' %d\n", 0);
        im.WriteInode(&root, 0);
}

void IVFS::CreateFileSystem()
{
        InodeManager::CreateInodeSpace();
        BlockManager::CreateBlockSpace();
        BlockManager::CreateFreeBlockArray();
}

bool IVFS::EmptyRecord(DirRecord *rec)
{
        return rec->filename[0] == 0;
}

const char *IVFS::GetNextDir(const char *path, char *filename)
{
        for (path++; *path && *path != '/'; path++, filename++)
                *filename = *path;
        *filename = 0;
        return *path ? path : 0;
}

