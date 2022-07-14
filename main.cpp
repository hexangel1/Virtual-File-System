#include <stdio.h>
#include "vfs/ivfs.hpp"

static const char msg[] = "Hello, world\n";

int main()
{
        IVFS vfs;
        File f;
        vfs.Mount("./work_dir/", true);
        f = vfs.Open("/home/user/my_file.txt", "wc");
        f.Write(msg, sizeof(msg) - 1);
        f.Close();        
        vfs.Umount();
        return 0;
}

