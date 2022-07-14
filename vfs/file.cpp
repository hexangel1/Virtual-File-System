#include <cstdio>
#include <cstring>
#include "file.hpp"
#include "ivfs.hpp"

File::File(OpenedFile *p, char *first_block)
        : cur_pos(0), cur_block(0), block(first_block), master(p) {}

ssize_t File::Read(char *buf, size_t len)
{
        if (!master->perm_read) {
                fputs("File opened in write-only mode", stderr);
                return -1;
        }
        size_t was_read = cur_block * IVFS::block_size + cur_pos;
        size_t rc = 0;
        if (len > master->in.byte_size - was_read)
                len = master->in.byte_size - was_read;
        while (len > 0) {
                size_t can_read = IVFS::block_size - cur_pos;
                if (len < can_read) {
                        memcpy(buf + rc, block + cur_pos, len);
                        cur_pos += len;
                        rc += len;
                        len = 0;
                } else {
                        memcpy(buf + rc, block + cur_pos, can_read);
                        rc += can_read;
                        cur_pos = 0;
                        cur_block++;
                        BlockAddr next = master->vfs->GetBlockNum(&master->in,
                                                         cur_block);
                        master->vfs->bm.UnmapBlock(block);
                        block = (char*)master->vfs->bm.ReadBlock(next);
                        len -= can_read;
                }
        }
        return rc;
}

ssize_t File::Write(const char *buf, size_t len)
{
        if (!master->perm_write) {
                fputs("File opened in read-only mode", stderr);
                return 0;
        }
        size_t wc = 0;
        while (len > 0) {
                size_t can_write = IVFS::block_size - cur_pos;
                if (len < can_write) {
                        memcpy(block + cur_pos, buf + wc, len);
                        cur_pos += len;
                        master->in.byte_size += len;
                        wc += len;
                        len = 0;
                } else {
                        memcpy(block + cur_pos, buf + wc, can_write);
                        master->in.byte_size += can_write;
                        wc += can_write;
                        cur_pos = 0;
                        cur_block++;
                        BlockAddr next = master->vfs->AddBlock(&master->in);
                        master->vfs->bm.UnmapBlock(block);
                        block = (char*)master->vfs->bm.ReadBlock(next);
                        len -= can_write;
                }
        }
        return wc;
}

off_t File::Lseek(off_t offset, int whence)
{
        off_t new_pos, pos = cur_block * IVFS::block_size + cur_pos;
        off_t old_block = cur_block;
        switch (whence) {
        case seek_set:
                new_pos = offset;
                break;
        case seek_cur:
                new_pos = pos + offset;
                break;
        case seek_end:
                new_pos = master->in.byte_size - 1 + offset;
                break;
        default:
                new_pos = pos;
        }
        if (new_pos > master->in.byte_size - 1)
                new_pos = master->in.byte_size - 1;
        if (new_pos < 0)
                new_pos = 0;
        cur_block = new_pos / IVFS::block_size;
        cur_pos = new_pos % IVFS::block_size;
        if (cur_block != old_block) {
                master->vfs->bm.UnmapBlock(block);
                BlockAddr addr = master->vfs->GetBlockNum(&master->in,
                                                          cur_block);
                block = (char*)master->vfs->bm.ReadBlock(addr);
        }
        return new_pos;
}
 
void File::Close()
{
        if (!IsOpened())
                return;
        master->vfs->bm.UnmapBlock(block);
        master->vfs->CloseFile(master);
        master = 0;
}

off_t File::Size() const
{
        return master ? master->in.byte_size : 0;
}

