#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include "vfs/ivfs.hpp"

int main(int argc, char **argv)
{
        IVFS B;
        chdir("Storage");
        if (argc != 4) {
                fprintf(stderr, "error\n");
                exit(1);
        }
        B.Init(!strcmp(argv[1], "makefs"));
        File *f;
        char buf[128];
        int rc = 0;
        if (!strcmp(argv[3], "write")) {
                f = B.Create(argv[2]);
                fprintf(stderr, "index = %d\n", f->master->inode_idx);
        
                while ((rc = read(0, buf, sizeof(buf))) > 0) {
                        int res = B.Write(f, buf, rc);
                        if (res != rc) {
                                fprintf(stderr, "error2\n");
                                exit(1);
                        }
                }
                B.Close(f);
        } else {
                f = B.Open(argv[2]);
                fprintf(stderr, "index = %d\n", f->master->inode_idx); 
                while ((rc = B.Read(f, buf, sizeof(buf))) > 0)
                        write(1, buf, rc);
                B.Close(f);
                return 0;
        }
}

