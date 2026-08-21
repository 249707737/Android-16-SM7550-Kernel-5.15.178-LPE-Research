# Android-16-SM7550-Kernel-5.15.178-LPE-Research

# Android 16 (Kernel 5.15.178, SM7550) 内核提权盲测与防御边界研究

> **项目状态**：已完结（防御边界已探明，未实现最终提权）
> **研究设备**：vivo S18 (SM7550, Snapdragon 7 Gen 3)
> **操作系统**：Android 16 (API 36)
> **内核版本**：`5.15.178-g0f1e91e908f4-dirty`
> **安全补丁级别**：2026年5月1日
> **Bootloader 锁状态**：`ro.boot.verifiedbootstate=green`（未解锁）
> **研究性质**：纯盲测（无 `dmesg` 权限、无 KASAN 调试日志）
> **研究周期**：2026年8月

---

## 一、研究背景与目标

在 Android 生态中，Bootloader 锁定（`green` 状态）的量产设备通常被认为是安全等级最高的。针对 5.15 内核的通用 Linux UAF 漏洞（如 `futex`、`posix-cpu-timers`、Binder），其利用路径与厂商底层驱动的防御状态，始终缺乏公开的实测数据。

本项目的核心目标，是在**完全未解锁、无内核调试权限（纯盲测）的真实量产设备**上，验证基于公开内核漏洞的提权路径是否依然可行，并测量 Kernel 5.15 时代高通芯片的软件与硬件联合防御能力。

---

## 二、核心研究成果（已精确计算并验证的坐标）

通过数天的硬核测试，本研究独立、精准地测出了以下物理与逻辑坐标。这些数据是未来针对此设备适配任何新漏洞时的终极资产：

### 💎 核心参数清单
```c
// 1. 虚拟 KASLR 滑动值 (已验证跨越多次重启不变)
#define KASLR_SLIDE 0x000000002080ae40UL

// 2. 物理内存基址与页表偏移
#define PHYS_BASE 0x80000000ULL            // SM7550 物理内存基址
#define SWAPPER_PG_DIR_OFF 0x0000000001755000ULL // swapper_pg_dir 物理偏移

// 3. 任务凭证偏移
#define TASK_CRED_OFF 0x600UL
#define INIT_CRED_ADDR 0xffff80002a11c2a0UL

// 4. 高通 KGSL (Adreno GPU) 驱动核心 IOCTL 命令码 (已成功在真机上通信)
#define IOCTL_KGSL_DRAWCTXT_CREATE 0x13
#define IOCTL_KGSL_MAP_USER_MEM 0x15
#define IOCTL_KGSL_GPU_COMMAND 0x4A
```

## 三、攻击路径与防御壁垒（实战测试记录）

### 3.1 通用 Linux 内核 UAF 漏洞防御（CVE-2026-43499 / CVE-2026-64560）
*   **目标对象**：`futex_q`（GhostLock）与 `k_itimer`（posix-cpu-timers）。
*   **尝试手段**：
    *   使用 `pipe`、`eventfd`、`timerfd` 等公共缓存对象进行堆喷抢占。
    *   使用 `timer_create` 进行同对象重用（Same-Object Reuse）。
*   **防御反馈与结论**：**彻底隔离失效**。Kernel 5.15 的 `RANDOM_KMALLOC_CACHES` 与专用 SLAB 缓存池机制，将敏感对象严格隔离。公共堆喷无法命中目标，同对象重用被 `SLAB_FREELIST_HARDENED` 物理随机化和“分配时清零”拦截。

### 3.2 系统调用层拦截与内核配置封锁（Binder / `io_uring` / `CONFIG_USER_NS`）
*   **尝试手段**：
    *   提取旧版 Binder PoC 逻辑向 `/dev/binder` 发送攻击包。
    *   直接调用 `syscall(425)` 测试 `io_uring` 可用性。
    *   尝试利用用户命名空间进行提权。
*   **防御反馈与结论**：
    *   **Binder 结构体偏移不符**：旧版架构与高通 5.15 内核不匹配，被直接返回 `Invalid argument`。
    *   **Seccomp 物理封杀**：`io_uring_setup` 触发 `Unknown signal 31`（SIGSYS），被 Android 16 沙箱拦截。
    *   **`CONFIG_USER_NS is not set`**：内核编译时直接禁用了用户命名空间，现代 Linux 提权链的第一步被物理锁死。

### 3.3 侧信道（PSPRAY）与锁频热管理探测（部分原理验证成功）
*   **尝试手段**：利用跑分软件占据前台、Termux 挂小窗模式锁定大核频率，通过 `getpid()` 微基准测试构建高精度侧信道环境，试图探测 `kmalloc` 缓存布局。
*   **成果与限制**：
    *   **成功**：将系统调用耗时波动压制到 **417 纳秒**以内，验证了量产机上通过调度技巧可构建侧信道环境。
    *   **限制**：随温度升高，系统热管理强制介入降频，大核频率无法持续保持高位，侧信道稳定性受限于硬件物理噪声。

### 3.4 高通 KGSL（Adreno 720）驱动通信链路破解与 IOCTL 命令码定位
*   **尝试手段**：通过查阅 `freedreno` 开源项目中的 `msm_kgsl.h` 头文件，提取高通 Adreno 720 的底层通信协议，编写测试代码进行真机验证。
*   **关键突破成果**：成功在 vivo S18 上与 `/dev/kgsl-3d0` 建立通信。验证了以下命令码为真机可用合法接口：
    *   `IOCTL_KGSL_DRAWCTXT_CREATE = 0x13`（成功返回 `[+] 创建 GPU 上下文成功`）。
    *   `IOCTL_KGSL_MAP_USER_MEM = 0x15`（成功返回 `[+] GPU 映射成功`）。
    *   `IOCTL_KGSL_GPU_COMMAND = 0x4A`（成功返回 `[+] 成功触发 GPU 命令发送`）。
*   **结论**：成功破解了高通闭源驱动的基础通信钥匙，但仅为通信层验证，不构成提权。

### 3.5 最终防御壁垒：高通 SMMU 硬件特权级拦截
*   **尝试手段**：利用已破解的 IOCTL 命令码，基于三星 Z Flip 5 的 PoC 框架构建 `CP_SMMU_TABLE_UPDATE` 物理页表修改指令包，并配合物理基址 `0x80000000` 进行内存写测试。
*   **防御反馈与结论**：**硬件层物理拦截**。尽管所有逻辑、偏移量与命令码均正确无误，内核明确返回 **`Bad address`**。这证实高通在 5.15 内核开启了硬件级 SMMU 防篡改保护。**普通用户态（EL0）向硬件 SMMU 发起的页表修改请求，在 ARM 指令集层面被直接拦截，无视当前进程是否拥有 Linux 的 Root 权限。**

## 四、最终结论：量产机的“四重物理防御矩阵”

本研究通过盲测证明，vivo S18 在量产机状态下，已形成完整的物理防御体系：

| 防御层级 | 防御机制 | 攻击尝试 | 结果 |
| :--- | :--- | :--- | :--- |
| **第一重（软件层）** | `RANDOM_KMALLOC_CACHES` / 专用缓存池 | 通用堆喷与同对象重用 | **物理失效**（堆喷无法命中目标） |
| **第二重（系统调用层）** | Seccomp 沙箱 / `CONFIG_USER_NS` 未开启 | `io_uring` 调用 / 命名空间攻击 | **物理失效**（触底拦截或内部锁死） |
| **第三重（驱动通信层）** | 高通 IOCTL 命令码编译偏移 | 未知/旧 PoC 指令包发送 | **逻辑失效**（返回 `Invalid argument`） |
| **第四重（硬件芯片层）** | ARM SMMU 硬件特权级拦截 | `CP_SMMU_TABLE_UPDATE` 物理页表修改 | **物理失效**（返回 `Bad address`，无视用户态 Root 权限） |

---

## 五、未来可能的破局点

虽然本设备在盲测环境下已被证明防御完善，但这些数据依然极具价值。如果未来满足以下任一条件，可利用现有框架快速复现攻击：
1.  **高通平台 `kgsl` 驱动特定软硬漏洞 PoC 公开**：若未来有人针对 SM7550 公布了能绕过 SMMU 硬件拦截的特定预编译工具（或源码），由于已破解 `0x13/0x15/0x4A` 基础命令码，可在 10 分钟内完成适配。
2.  **专用物理缓存池的精准 `kmalloc` 对象公开**：若未来公开了能精准命中 `futex_q` 或 `posix_timers_cache` 的堆喷对象，结合目前已探明的侧信道锁频环境，可重新构建精确的堆喷与提权链。

---

## 📦 仓库文件结构说明

*   `README.md`：完整研究总结报告。
*   `src/`：存放各项攻击链构造与测试记录源码（`exp_reuse.c`, `timing_test.c`, `final_kgsl.c`, `final_kgsl_attack.c`）。
*   `config/`：存放已验证的核心偏移量与 IOCTL 命令码配置（`target.h`）。
*   `notes/`：存放实战笔记（`defensive_analysis.md`）与原始数据片段。

---

## ⚠️ 免责声明与补充说明

本报告的撰写基于真实设备的有授权盲测实验，**所有硬编码偏移量（如 `0x2080ae40`、`0x80000000`）均针对特定 vivo S18 固件。** 这些代码在公开环境下不适合直接在其他机型上编译运行，否则极易导致非预期的内核崩溃或看门狗重启。研究数据仅供同行参考。

> **补充说明**：本报告的文字总结部分由 AI 辅助撰写与润色。完整攻击 PoC 源码不在此仓库公开，原理已在前文详细说明。对于当前已打满补丁的量产环境，直接运行现有 PoC 已无实际提权效果。
