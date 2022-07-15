#ifndef FILE_HPP_SENTRY
#define FILE_HPP_SENTRY

#include <stddef.h>
#include <sys/types.h>

struct OpenedFile;

class File {
        off_t cur_pos;
        off_t cur_block;
        char *block;
        OpenedFile *master;
public:
        File(OpenedFile *ofptr = 0, char *first_block = 0);
        ssize_t Read(char *buf, size_t len);
        ssize_t Write(const char *buf, size_t len);
        off_t Lseek(off_t offset, int whence);
        void Close();
        off_t Size() const;
        bool IsOpened() const { return master; }
        enum {
                seek_set = 0,
                seek_cur = 1,
                seek_end = 2
        }; 
        static const int max_name_len = 28;
};

#endif /* FILE_HPP_SENTRY */

