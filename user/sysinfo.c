#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/sysinfo.h"
int main(int argc, char *argv[])
{
    struct sysinfo info;
    sysinfo(&info);
    printf("free mem:%d, used process num:%d\n", info.freemem, info.nproc);
    exit(0);
}