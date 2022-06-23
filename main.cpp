#include <iostream>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
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
                std::cerr << "VFS: file not opened: " << path << std::endl;
                return;
        }
        char buf[139];
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
                std::cerr << "VFS: file not opened: " << path << std::endl; 
                return;
        }
        char buf[333];
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
        write_file_to_vfs(vfs, "/usr/local/games/data1", "test/test1");
        write_file_to_vfs(vfs, "/usr/local/games/data2", "test/test2");
        write_file_to_vfs(vfs, "/usr/local/games/data3", "test/test3");
        write_file_to_vfs(vfs, "/data4", "test/test4");
        write_file_to_vfs(vfs, "/etc/config/data5", "test/test5");
        write_file_to_vfs(vfs, "/data6", "test/test6");
        write_file_to_vfs(vfs, "/usr/bin/data6", "test/test7");
        write_file_to_vfs(vfs, "/usr/local/games/new/data8", "test/test8");
        write_file_to_vfs(vfs, "/usr/local/include/data9", "test/test9");
        write_file_to_vfs(vfs, "/usr/games/data10", "test/test10");

        read_file_from_vfs(vfs, "/usr/local/games/data1", "test/test1.out");
        read_file_from_vfs(vfs, "/usr/local/games/data2", "test/test2.out");
        read_file_from_vfs(vfs, "/usr/local/games/data3", "test/test3.out");
        read_file_from_vfs(vfs, "/data4", "test/test4.out");
        read_file_from_vfs(vfs, "/etc/config/data5", "test/test5.out");
        read_file_from_vfs(vfs, "/data6", "test/test6.out");
        read_file_from_vfs(vfs, "/usr/bin/data6", "test/test7.out");
        read_file_from_vfs(vfs, "/usr/local/games/new/data8", "test/test8.out");
        read_file_from_vfs(vfs, "/usr/local/include/data9", "test/test9.out");
        read_file_from_vfs(vfs, "/usr/games/data10", "test/test10.out");
        return 0;
}

