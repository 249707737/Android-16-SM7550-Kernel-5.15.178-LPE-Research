# 信息泄露面测试：普通用户态下的读取尝试

在未解锁 Bootloader（`green`）的 vivo S18 上，我们通过 `adb shell` 对底层系统接口进行了详尽的“信息泄露”测试，以评估是否存在绕过 KASLR 或获取内核敏感数据的非预期通路。

## 1. 内核启动参数（/proc/cmdline）
**读取结果：成功**

```text
loglevel=6 kpti=0 loop.max_part=7 allow_mismatched_32bit_el0 log_buf_len=3M kernel.panic_on_rcu_stall=1 service_locator.enable=1 msm_rtb.filter=0x237 rcupdate.rcu_expedited=1 rcu_nocbs=0-7 ftrace_dump_on_oops printk.console_no_auto_verbose=1 kasan=off can.stats_timer=0 pcie_ports=compat cpufreq.default_governor=performance cgroup.memory=nokmem,nosocket swiotlb=noforce disable_dma32=on page_poison=on console=null console-at=null console-atcmd=null console-mode=vivo_normal_mode at_lcm_cur=0mA product_state=4 earlycon=null video=vfb:640x400,bpp=32,memsize=3072000 nosoftlockup product.version=PD2323_A_16.2.9.0.W10 fingerprint.abbr=13/TP1A.220624.014 region_ver=W10 product.solution=QCOM bootconfig buildvariant=user  msm_drm.dsi_display0=qcom,mdss_dsi_cpd2333_pd2323_boe_nt37705_fhdplus_cmd::lcm_software_id=0x96:boot_silent=0 rootwait ro init=/init bbk_board_version=001111111101111 bbk_model_version=G:118,119,120,138,139,140,141,142,143,144,145,146,149,150,151,hw_id-001111111101111 fuse_info=01000100 silent_boot.mode=nonsilent bootloader.time=4 firmware_class.path=/vendor/etc/ support_minidp=1 vivolog_flag=0 tier=0 bbk_dp=2 vivo_cpuid=0x457115ABC13 msm_rtb.filter=0x237 printk.devkmsg=on ignore_loglevel vivoboot.normalboot=true pmic_status=NONE dump_display vivoboot.bootreason=HARD_RESET/PS_HOLD/NONE+NONE rtc_reset=0
```

分析：

· kpti=0：未启用页表隔离。
· page_poison=on：内核分配器默认填充，直接解释了公共堆喷无法伪造对象内容的原因。
· buildvariant=user：确认了非 Debug 工程机版本。
· msm_rtb.filter=0x237：高通私有，用于跟踪。

2. 系统属性泄露（getprop）

读取结果：成功

重点关注项：

```text
ro.boot.verifiedbootstate = green
ro.boot.hardware = qcom
ro.build.version.security_patch = 2026-05-01
ro.build.version.release = 16
ro.debuggable = 0
ro.product.platform = SM7550
ro.vivo.dyn.veritystate = enforcing
```

分析：

· 确认了 BL 锁定状态（green）、芯片型号以及系统版本。
· 确认了 SELinux 处于强制模式。
· ro.debuggable=0：禁止了 adb root。

3. 物理内存分配状态（/proc/zoneinfo）

读取结果：成功

```text
Node 0, zone Normal
  nr_free_pages 117688
  nr_inactive_anon 328932
  nr_slab_reclaimable 71130
  nr_slab_unreclaimable 159204
  nr_shadow_call_stack 50488
  ...
```

分析：

· 普通用户态可获取内存池的实时统计数据，能用于监测堆喷时机。
· 但无法看到具体物理地址或缓存对象的 freelist 内部布局。

4. 内核符号表与物理内存设备

读取结果：全部失败

```bash
adb shell cat /proc/kallsyms
# Permission denied

adb shell ls -l /dev/mem /dev/kmem
# No such file or directory
```

5. 内核动态追踪（tracefs）

读取结果：受限

```text
/sys/kernel/tracing/trace:
# tracer: nop
# entries-in-buffer/entries-written: 0/0   #P:8
```

分析：

· 目录可访问，但无追踪器工作，无法通过 kprobe 泄露函数地址。

---

总结

在普通用户态和 ADB 权限下，该设备开放了部分“元信息”（启动参数、系统属性、内存统计），但彻底禁止了所有用于泄露内核敏感地址的通路（kallsyms、dmesg、tracefs、/dev/mem）。任何绕过 KASLR 的尝试必须依赖漏洞本身的黑盒盲算，而非信息泄露。
