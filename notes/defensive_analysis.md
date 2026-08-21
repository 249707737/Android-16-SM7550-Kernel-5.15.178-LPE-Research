# 防御机制分析：为什么堆喷和同对象重用彻底失败？

## 1. Seccomp 系统级拦截
在 Android 16 中，尝试调用 `msgsnd`、`io_uring_setup`、`add_key` 等高危系统调用时，直接触发 `Unknown signal 31`（SIGSYS）。系统在用户态就切断了堆喷原语的入口（即便在 ADB 环境下，系统底层规则也限制某些利用）。

## 2. `RANDOM_KMALLOC_CACHES`
Kernel 5.15 默认开启了此机制。内核将相同大小（如 192 字节）的内存块，随机分配到物理上完全隔离的多个缓存池中。这使得任何基于公共缓存的堆喷（`pipe`、`eventfd`）命中率无限趋近于 0。

## 3. 专用 Slab 缓存池防重用与分配时清零
即使在触发 UAF 后，利用 `timer_create` 尝试重新分配 `k_itimer`（同对象重用），内核分配器依然会因为 `SLAB_FREELIST_HARDENED` 的随机化将新对象分配到缓存的其他位置；即使抢到原地址，内核也会在分配时瞬间清零（`__GFP_ZERO`），导致我们无法在目标内存中写入伪造的 ROP 链或载荷。