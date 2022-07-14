#ifndef FILE_HPP_SENTRY
#define FILE_HPP_SENTRY

#include <stddef.h>
#include <sys/types.h>

class IVFS;
class OpenedFile;

class File {
        off_t cur_pos;
        off_t cur_block;
        char *block;
        OpenedFile *master;
        IVFS *fs;
public:
        enum {
                seek_set = 0,
                seek_cur = 1,
                seek_end = 2
        };
        File(OpenedFile *master = 0, IVFS *fsptr = 0, char *first_block = 0);
        ssize_t Read(char *buf, size_t len);
        ssize_t Write(const char *buf, size_t len);
        off_t Lseek(off_t offset, int whence);
        void Close();
        bool IsOpened() const { return master; }
//        off_t Size() const { return master ? master->in.byte_size : 0; } 
//        uint32_t Node() const { return master ? master->in.inode_idx : -1; }
};

#endif /* FILE_HPP_SENTRY */

