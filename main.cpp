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
        std::cerr << "WRITE " << file << " TO VFS PATH: " << path << std::endl;
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
        std::cerr << "READ " << file << " ON VFS PATH: " << path << std::endl;
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

int main(void)
{
        const char hello[] = "Hello World";
        char buf[128];
        int res, rc;
        File *f1, *f2, *f3;
        IVFS vfs;
        vfs.Init(true);

        std::cerr << "SIMPLE OPEN/CLOSE TESTS" << std::endl;

        f1 = vfs.Create("/home/file.txt");
        res = vfs.Read(f1, buf, sizeof(buf));
        if (res == 0)
                std::cerr << "READ FAILED: OK!" << std::endl;
        else
                std::cerr << "BUG #1 !!!" << std::endl;
        vfs.Close(f1);

        f1 = vfs.Open("/home/file.txt");
        res = vfs.Write(f1, hello, sizeof(hello));
        if (res == 0)
                std::cerr << "WRITE FAILED: OK!" << std::endl;
        else
                std::cerr << "BUG #2 !!!" << std::endl;
        vfs.Close(f1);

        f1 = vfs.Open("/home/file.txt");
        f2 = vfs.Open("/home/file.txt");
        if (f1 && f2)
                std::cerr << "2 READ-ONLY OPENING SUCCESS" << std::endl;
        else
                std::cerr << "BUG #3 !!!" << std::endl;
                
        f3 = vfs.Create("/home/file.txt");
        if (!f3)
                std::cerr << "NOT OPENED: OK!" << std::endl;
        else
                std::cerr << "BUG #4 !!!" << std::endl;
        vfs.Close(f1);
        vfs.Close(f2);
        vfs.Close(f3);

        f1 = vfs.Create("/home/file.txt");
        f2 = vfs.Create("/home/file.txt");
        if (!f2)
                std::cerr << "NOT OPENED: OK!" << std::endl;
        else
                std::cerr << "BUG #5 !!!" << std::endl;
        vfs.Close(f1);
        vfs.Close(f2);

        f1 = vfs.Open("/home");
        if (!f1)
                std::cerr << "NOT OPENED: OK!" << std::endl;
        else
                std::cerr << "BUG #6 !!!" << std::endl;

        std::cerr << "NOW RUNNING READ/WRITE FILE SYSTEM TESTS" << std::endl;
        
        write_file_to_vfs(vfs, "/usr/local/games/test1", "test/test1");
        f1 = vfs.Open("/usr/local/games/test1");
        f2 = vfs.Create("/usr/local/games/tmp");
        while ((rc = vfs.Read(f1, buf, sizeof(buf))) > 0)
                vfs.Write(f2, buf, rc);
        vfs.Close(f1);
        vfs.Close(f2);
        read_file_from_vfs(vfs, "/usr/local/games/tmp", "test/test1.out");

        write_file_to_vfs(vfs, "/usr/local/games/test2", "test/test2");
        write_file_to_vfs(vfs, "/usr/local/games/test3", "test/test3");
        write_file_to_vfs(vfs, "/usr/local/games/new/test4", "test/test4");
        write_file_to_vfs(vfs, "/usr/local/games/new/test5", "test/test5");
        write_file_to_vfs(vfs, "/usr/local/test6", "test/test6");
        write_file_to_vfs(vfs, "/test7", "test/test7");
        write_file_to_vfs(vfs, "/test8", "test/test8");
        write_file_to_vfs(vfs, "/usr/bin/test9", "test/test9");
        write_file_to_vfs(vfs, "/etc/config/test10", "test/test10");
        write_file_to_vfs(vfs, "/etc/config/test11", "test/test11");
        write_file_to_vfs(vfs, "/home/home/Doc/My/test12", "test/test12");
        write_file_to_vfs(vfs, "/etc/config/test13", "test/test13");
        write_file_to_vfs(vfs, "/etc/config/conf/test14", "test/test14");
        write_file_to_vfs(vfs, "/usr/bin/a/test15", "test/test15");
        write_file_to_vfs(vfs, "/usr/bin/a/b/test16", "test/test16");
        write_file_to_vfs(vfs, "/usr/bin/a/b/c/test17", "test/test17");
        write_file_to_vfs(vfs, "/usr/bin/a/b/c/d/test18", "test/test18");
        write_file_to_vfs(vfs, "/usr/bin/a/b/c/d/e/test19", "test/test19");
        write_file_to_vfs(vfs, "/usr/bin/a/b/c/d/e/f/test20", "test/test20");


        read_file_from_vfs(vfs, "/usr/local/games/test1", "test/test1.out");
        read_file_from_vfs(vfs, "/usr/local/games/test2", "test/test2.out");
        read_file_from_vfs(vfs, "/usr/local/games/test3", "test/test3.out");
        read_file_from_vfs(vfs, "/usr/local/games/new/test4", "test/test4.out");
        read_file_from_vfs(vfs, "/usr/local/games/new/test5", "test/test5.out");
        read_file_from_vfs(vfs, "/usr/local/test6", "test/test6.out");
        read_file_from_vfs(vfs, "/test7", "test/test7.out");
        read_file_from_vfs(vfs, "/test8", "test/test8.out");
        read_file_from_vfs(vfs, "/usr/bin/test9", "test/test9.out");
        read_file_from_vfs(vfs, "/etc/config/test10", "test/test10.out");
        read_file_from_vfs(vfs, "/etc/config/test11", "test/test11.out");
        read_file_from_vfs(vfs, "/home/home/Doc/My/test12", "test/test12.out");
        read_file_from_vfs(vfs, "/etc/config/test13", "test/test13.out");
        read_file_from_vfs(vfs, "/etc/config/conf/test14", "test/test14.out");
        read_file_from_vfs(vfs, "/usr/bin/a/test15", "test/test15.out");
        read_file_from_vfs(vfs, "/usr/bin/a/b/test16", "test/test16.out");
        read_file_from_vfs(vfs, "/usr/bin/a/b/c/test17", "test/test17.out");
        read_file_from_vfs(vfs, "/usr/bin/a/b/c/d/test18", "test/test18.out");
        read_file_from_vfs(vfs, "/usr/bin/a/b/c/d/e/test19", "test/test19.out");
        read_file_from_vfs(vfs, "/usr/bin/a/b/c/d/e/f/test20",
                                "test/test20.out");
        return 0;
}

