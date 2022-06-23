#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include "vfs/ivfs.hpp"

static void write_file_to_vfs(IVFS &vfs, const char *path, const char *file)
{
        File *f;
        int fd = open(file, O_RDONLY);
        if (fd == -1) {
                perror(file);
                return;
        }
        f = vfs.Create(path);
        if (!f) {
                fprintf(stderr, "%s: not opened\n", path);
                return;
        }
        char buf[4096];
        int rc;
        while ((rc = read(fd, buf, sizeof(buf))) > 0)
                vfs.Write(f, buf, rc);
        close(fd);
        vfs.Close(f);
}

static void read_file_from_vfs(IVFS &vfs, const char *path, const char *file)
{
        File *f;
        int fd = open(file, O_WRONLY | O_CREAT, 0644);
        if (fd == -1) {
                perror(file);
                return;
        }
        f = vfs.Open(path);
        if (!f) {
                fprintf(stderr, "%s: not opened\n", path);
                return;
        }
        char buf[4096];
        int rc;
        while ((rc = vfs.Read(f, buf, sizeof(buf))) > 0)
                write(fd, buf, rc);
        close(fd);
        vfs.Close(f);
}

int main(int argc, char **argv)
{
        IVFS vfs;
        vfs.Init(true);
        write_file_to_vfs(vfs, "/file", "test");
        write_file_to_vfs(vfs, "/file", "test2");
        read_file_from_vfs(vfs, "/file", "test3");
        return 0;
}

