# 平板内核 QEMU 启动实验报告（可复现·压缩版）

> **结论**：能启动。用真机 config + 同源华为 4.19 树编出的 `Image`，在 `qemu-system-aarch64 -M virt,secure=on,virtualization=on` 上可完整启动到 busybox，并挂载 9p 共享目录双向读写。
> **代价**：4 个障碍，缺一不可：① stack canary 走 Android TLS；② 真机 config 无 console/virtio 驱动；③ 厂商 SMC 在无 EL3 的 QEMU 上是未定义指令；④ 厂商 initcall 读高通 SMEM。

---

## 0. 最短复现路径

```sh
# 变量约定
PAD=$HOME/ghostLock/pad
TC=$PAD/toolchain

# ① 编译
cd $PAD/Code_Opensource/kernel
make ARCH=arm64 O=out-pad \
  CC=$TC/clang-r377782d/bin/clang.real \
  CROSS_COMPILE=$TC/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  CLANG_TRIPLE=aarch64-linux-gnu- \
  HOSTCC=gcc -j3 vmlinux Image < /dev/null

# ② 启动
qemu-system-aarch64 -M virt,secure=on,virtualization=on -cpu cortex-a57 -m 1024 \
  -nographic -no-reboot \
  -kernel out-pad/arch/arm64/boot/Image \
  -initrd $PAD/initramfs-pad.cpio.gz \
  -virtfs local,path=$PAD/qshare,mount_tag=host0,security_model=none \
  -append 'console=ttyAMA0,115200 earlycon rdinit=/init nokaslr panic=-1 initcall_blacklist=proc_app_info_init'

# ③ guest 内
mkdir -p /mnt
mount -t 9p -o trans=virtio,version=9p2000.L host0 /mnt
```

预期关键输出：

```
Booting Linux on physical CPU 0x0000000000 [0x411fd070]
Linux version 4.19.157-perf ... #4 SMP PREEMPT ...
Machine model: linux,dummy-virt
earlycon: pl11 at MMIO 0x0000000009000000 (options '')
psci: PSCIv1.1 detected in firmware.
=== [PAD-GUEST] userspace init is running ===
BusyBox v1.36.1 ... built-in shell (ash)
~ #
```

两条硬性前置：`out-pad/.config` 必须含 console/virtio/9p 开关；内核参数必须含 `initcall_blacklist=proc_app_info_init`。

---

## 1. 环境与资产

| 项          | 值                                                 |
| ----------- | -------------------------------------------------- |
| 靶场机      | Ubuntu 24.04.1，6 核 / 3867 MB 内存 / 50G 盘余 12G |
| QEMU        | `qemu-system-aarch64` 8.2.2                        |
| 调试器      | `gdb-multiarch` 15.0.50                            |
| 内核编译器  | AOSP prebuilt clang 10.0.6，`clang-r377782d`       |
| 链接器      | GNU ld 2.27.0.20170315，`binutils-2.27-bd24d23f`   |
| host 编译器 | gcc 13.3.0，仅 `HOSTCC`/Kconfig                    |
| 目标        | 华为平板 arm64，Android 12，内核 `4.19.157-perf+`  |
| 真机连接    | 通过 ADB 抓取，无需 root                           |

目录约定：

```
$PAD/
├── DBY-W09_HarmonyOS4.0.0_opensource.tar.gz   579989887 B
├── Code_Opensource/kernel/                    内核根
├── toolchain/{clang-r377782d, aarch64-linux-android-4.9}/
├── initramfs-pad.cpio.gz                      1006082 B
├── qshare/                                    ↔ guest /mnt
└── backup/
```

源码包实测：顶层仅 `Code_Opensource/`，条目 **159097**；`kernel/Makefile` 为 `4.19.157`；`arch/arm64/configs/vendor/kona-perf_defconfig`，`CONFIG_LOCALVERSION="-perf"`。包内无预编译产物。

真机 config 原件：`config-DBY-W09`，201227 B，MD5 `d627e8ac89a2b8d65a2b003b897b7342`，6824 非空行。关键项：`CONFIG_IKCONFIG_PROC=y`、`CONFIG_ARM64_4K_PAGES=y`、`CONFIG_HW_FUTEX_PI=y`、`CONFIG_DEBUG_INFO=y`、`CONFIG_CC_IS_CLANG=y`、`CONFIG_CLANG_VERSION=100007`、`CONFIG_MODULE_SIG_KEY="huawei_signing_key.pem"`。4.19 无 `RANDOMIZE_KSTACK_OFFSET`。

工具链判据：

```sh
$TC/clang-r377782d/bin/clang.real --version
# Android (6364210 based on r377782d) clang version 10.0.6
$TC/aarch64-linux-android-4.9/bin/aarch64-linux-android-ld --version
# GNU ld (binutils-2.27-bd24d23f) 2.27.0.20170315
```

注意：`bin/clang` 是 python2 包装脚本，必须用 `bin/clang.real`。

---

## 2. 构建准备与三个构建坑

```sh
cd $PAD/Code_Opensource/kernel
cp config-DBY-W09 out-pad/.config   # 不要直接改原件
make ARCH=arm64 O=out-pad CC=gcc olddefconfig
```

构建坑：

| #    | 症状                                 | 根因                                               | 修法                                                         |
| ---- | ------------------------------------ | -------------------------------------------------- | ------------------------------------------------------------ |
| 1    | make 像卡死，CPU 时间 0              | 老 kbuild `conf --syncconfig` 等 stdin             | make 命令末尾加 `< /dev/null`                                |
| 2    | `certs` 缺 `extract-cert`            | 真机 `MODULE_SIG_KEY` 指向华为签名工具，源码包裁掉 | `bash scripts/config --file out-pad/.config --set-str MODULE_SIG_KEY certs/signing_key.pem` |
| 3    | `lcd_kit_debug.c` 缺 `lcd_kit_dpd.h` | 源码包缺该头                                       | 补最小 stub 到 lcdkit3.0/kernel/qcom/common/src/             |

调试要点：真错误用 `grep -n -e ": error:" -e "Error 1"` 往前找；不要修 `common/src/` 老 API；禁止 `make mrproper`。

构建成功后 `vmlinux` 含完整 DWARF。用 `readelf --debug-dump=info` 读 `struct rt_mutex_waiter`：`task=0x30`、`lock=0x38`、`major_prio=0x40`、`prio=0x48`、`deadline=0x50`，**sizeof=0x58**。此前“0x40”作废。

产出 QEMU 用 `Image`：

```sh
xxd -s 0x38 -l 4 out-pad/arch/arm64/boot/Image
# 4152 4d64 = ARMd
```

---

## 3. 四个障碍总表（核心）

| #    | 障碍                        | 症状                                                         | 根因                                                         | 修法                                                         | 改代码生成                |
| ---- | --------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ | ------------------------- |
| 1    | stack canary 走 Android TLS | `FAR=0x28`，`ELR=kaslr_early_init+28`，`mrs tpidr_el0` 后读 `[x23,#40]` | `CROSS_COMPILE=aarch64-linux-android-` 推出 `--target=aarch64-linux-android` | 编译加 **`CLANG_TRIPLE=aarch64-linux-gnu-`**                 | 是，改所有函数序言/帧大小 |
| 2    | 无 console/virtio 驱动      | `-nographic` 零输出；9p/virtio 挂不上                        | 真机 config 中 `SERIAL_*`/`VIRTIO*`/`PCI_HOST_GENERIC`/`DEVTMPFS`/`9P*` 全未开 | 用 `scripts/config -e` 打开驱动开关                          | 否                        |
| 3    | 厂商 SMC 无 EL3             | `do_undefinstr ← is_scm_armv8+0x48`，该地址就是 `smc #0x0`   | QEMU 默认无 EL3                                              | ① 关 `CONFIG_QCOM_EARLY_RANDOM`；② QEMU 加 **`-M virt,secure=on,virtualization=on`** | 否                        |
| 4    | 厂商 SMEM 读取              | `app_info_print_smem+0x88` Data Abort，`proc_app_info_init` 触发；`-m 2048` 无效 | 读高通 SMEM，QEMU 未映射                                     | 内核参数 **`initcall_blacklist=proc_app_info_init`**         | 否                        |

必需的 config 开关：

```sh
for o in DEVTMPFS DEVTMPFS_MOUNT SERIAL_CORE SERIAL_CORE_CONSOLE \
         SERIAL_AMBA_PL011 SERIAL_AMBA_PL011_CONSOLE \
         VIRTIO_MENU VIRTIO VIRTIO_PCI VIRTIO_MMIO VIRTIO_BLK VIRTIO_NET \
         PCI_HOST_GENERIC PCI_MSI NET_9P NET_9P_VIRTIO 9P_FS; do
  bash scripts/config --file out-pad/.config -e $o
done
bash scripts/config --file out-pad/.config -d QCOM_EARLY_RANDOM
make ARCH=arm64 O=out-pad CC=... CROSS_COMPILE=... HOSTCC=gcc olddefconfig < /dev/null
```

`bash scripts/config` 必须用 `bash`，dash 下会静默失效。

---

## 4. 最终配方

### 4.1 编译

```sh
cd $PAD/Code_Opensource/kernel
TC=$PAD/toolchain
make ARCH=arm64 O=out-pad \
  CC=$TC/clang-r377782d/bin/clang.real \
  CROSS_COMPILE=$TC/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  CLANG_TRIPLE=aarch64-linux-gnu- \
  HOSTCC=gcc -j3 vmlinux Image < /dev/null
```

- `-j3` 上限；`-j6` 会 OOM。
- `< /dev/null` 不可省。
- 改 `CLANG_TRIPLE` 触发全量重编；改 driver 开关只重编受影响目录。
- 改产物前先备份到 `backup/<name>.orig-YYYYMMDD`。

### 4.2 启动

```sh
qemu-system-aarch64 -M virt,secure=on,virtualization=on -cpu cortex-a57 -m 1024 \
  -nographic -no-reboot \
  -kernel out-pad/arch/arm64/boot/Image \
  -initrd $PAD/initramfs-pad.cpio.gz \
  -virtfs local,path=$PAD/qshare,mount_tag=host0,security_model=none \
  -append 'console=ttyAMA0,115200 earlycon rdinit=/init nokaslr panic=-1 initcall_blacklist=proc_app_info_init'
```

参数理由：

| 参数                                  | 理由                                      |
| ------------------------------------- | ----------------------------------------- |
| `-M virt,secure=on,virtualization=on` | 必须，补 EL3，否则厂商 `smc` 是未定义指令 |
| `-cpu cortex-a57`                     | 提供 EL3，兼容厂商 erratum workaround     |
| `-m 1024`                             | 够用；`-m 2048` 对障碍 4 无效             |
| `-no-reboot`                          | panic 后保住现场                          |
| `-smp 1`                              | 厂商 SMP bring-up 依赖 QCOM，单核最稳     |
| `rdinit=/init`                        | 直接跑 initramfs `/init`                  |
| `nokaslr`                             | `System.map`/gdb 地址直接可用             |
| `panic=-1`                            | panic 不重启                              |
| `earlycon`                            | 真正 console 驱动 probe 前就有输出        |

### 4.3 initramfs 与 9p 验证

aarch64 busybox 静态包制作 initramfs。`/init` 要点：

```sh
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev 2>/dev/null
echo "=== [PAD-GUEST] userspace init is running ==="
cat /proc/version
echo "cmdline: $(cat /proc/cmdline)"
exec sh
```

guest 内验证：

```sh
mkdir -p /mnt
mount -t 9p -o trans=virtio,version=9p2000.L host0 /mnt && echo MOUNT-OK
cat /mnt/hostfile.txt
echo 123 > /mnt/from-guest.txt && echo WRITE-OK
```

宿主侧应看到 `qshare/from-guest.txt` 内容为 `123`。至此串口交互、传入 PoC、取回结果全部就位。

---

## 5. 复现校验点

- [ ] `tar -tzf <包> | wc -l` = **159097**
- [ ] `head -4 kernel/Makefile` = `4 / 19 / 157`
- [ ] `config-DBY-W09` MD5 = **`d627e8ac89a2b8d65a2b003b897b7342`**
- [ ] `clang.real --version` 含 **`clang version 10.0.6`**
- [ ] `aarch64-linux-android-ld --version` = **`GNU ld (binutils-2.27-bd24d23f) 2.27.0.20170315`**
- [ ] `grep CONFIG_CLANG_VERSION out-pad/.config` = **100006**，`CONFIG_CC_IS_CLANG=y`，`CONFIG_GCC_VERSION=0`
- [ ] `# CONFIG_QCOM_EARLY_RANDOM is not set`，且 `CONFIG_SERIAL_AMBA_PL011_CONSOLE=y`、`CONFIG_PCI_HOST_GENERIC=y`、`CONFIG_9P_FS=y`
- [ ] `kaslr_early_init` 序言是 `adrp x8, __stack_chk_guard; ldr x8,[x8,#…]`，不是 `mrs …, tpidr_el0`
- [ ] `xxd -s 0x38 -l 4 out-pad/arch/arm64/boot/Image` = **`4152 4d64`**
- [ ] 最终产物：`vmlinux` 568606456 B，MD5 `e604943a0c10418dff2a4d95f536f23f`；`System.map` 7315112 B，MD5 `f864178c8e7f41bacaea79ce69ac7a0f`；`Image` 57427984 B，MD5 `8bf589cb07c40fc63eeaa82f55a5bcf3`；`initramfs-pad.cpio.gz` 1006082 B，MD5 `7e63fc2e540c082acd2dec6358f1c2db`
- [ ] 启动第一行 = `Booting Linux on physical CPU 0x0000000000 [0x411fd070]`
- [ ] 版本串 = `4.19.157-perf ... #4 SMP PREEMPT Sat Oct 3 13:50:45 CST 2026`
- [ ] 出现 `=== [PAD-GUEST] userspace init is running ===` 与 `BusyBox v1.36.1`
- [ ] guest 内 `mount -t 9p …` → `MOUNT-OK`；`echo 123 > /mnt/from-guest.txt` → `WRITE-OK`，宿主侧文件内容为 `123`

关键符号地址：

```
ffffff800815bb68 T __rt_mutex_start_proxy_lock
ffffff800815bdb8 t task_blocks_on_rt_mutex
ffffff800815bfd4 T rt_mutex_start_proxy_lock
ffffff800815c074 t remove_waiter
ffffff800819e600 t futex_requeue
ffffff800837af88 T app_info_print_smem
ffffff8008728e0c T is_scm_armv8            (smc 在 +0x48)
ffffff800ae04cf8 W init_random_pool
ffffff800ae2b0c8 t proc_app_info_init
ffffff800ae44dc8 t scm_mem_protection_init
```

---

## 6. 与真机偏差与作废数值

| 类别     | 偏差                                                 | 影响                                  |
| -------- | ---------------------------------------------------- | ------------------------------------- |
| 工具链   | clang 10.0.6 vs 真机 10.0.7；AOSP 无 10.0.7          | 唯一影响 `off_T` 的偏差               |
| 编译选项 | 新增 `CLANG_TRIPLE=aarch64-linux-gnu-`               | 反而更接近真机                        |
| config   | `MODULE_SIG_KEY="certs/signing_key.pem"`             | 仅模块签名密钥来源                    |
| config   | `# CONFIG_QCOM_EARLY_RANDOM is not set`              | 仅早期熵源，不参与 rtmutex/futex/调度 |
| config   | 新增 driver + `NET_9P*`/`9P_FS`                      | 纯 driver 层                          |
| 源码     | 补 stub `lcd_kit_dpd.h`                              | 仅 LCD 调试驱动                       |
| 运行时   | `secure=on`、`initcall_blacklist=proc_app_info_init` | 不改内核代码                          |
| 结构体   | `struct rt_mutex_waiter` = **0x58**                  | 由源码 + config 决定，与编译器无关    |

依然权威的真机事实：`THREAD_SIZE=0x4000`、无 KASAN、`DEBUG_RT_MUTEXES=n`、`HW_FUTEX_PI=y`、无 `RANDOMIZE_KSTACK_OFFSET`、`CHECKPOINT_RESTORE=n`。  
作废的靶场数值：waiter 0x70、`off=0x3a8`、KASAN 栈膨胀 ~0x50、`THREAD_SIZE=0x8000`、lane 0 可用。

---

## 7. 无害噪音

| 现象                                  | 位置                         | 说明                                   |
| ------------------------------------- | ---------------------------- | -------------------------------------- |
| `update_cluster_topology` WARNING     | `kernel/sched/walt.c`        | 华为 WALT 取不到 cluster 拓扑          |
| `socinfo_init` WARNING                | `drivers/soc/qcom/socinfo.c` | 读不到高通 socinfo                     |
| `cpufreq_cpu_get` WARNING             | `drivers/cpufreq/cpufreq.c`  | 无 QCOM cpufreq 平台驱动               |
| `device_create_file` WARNING ×2       | `drivers/base/core.c`        | 缺 sysfs 属性对应设备                  |
| `Tainted: G W`                        | 全日志                       | 上述 WARNING 导致                      |
| `[usb_update] restart_reason is null` | 厂商线程                     | 读不到 bootloader restart reason，无害 |
| `bbox: g_rb_header is null`           | 华为黑匣子                   | QEMU 下拿不到 header                   |
| 一批 `Cannot find ... from dts`       | 各厂商驱动                   | `linux,dummy-virt` DTB 无高通节点      |
| `find: 'arch/arm64/boot/dts/'`        | 构建末尾                     | 这棵树无 dts 目录                      |
| `'unix_gc_lock' exported twice` 等    | 构建末尾                     | 华为源码包自身重复导出                 |

---

## 8. 坑与约束

1. 远程操作走 ssh2 脚本，禁止命令行 `ssh`；远程 `/tmp` 会清理，脚本要重传。
2. PowerShell 调原生 exe 时，带空格/引号参数用单引号包，或写成脚本上传。
3. `bash scripts/config` 必须用 `bash`。
4. make 必须 `< /dev/null`。
5. 不要把真机 config 原件当 `KCONFIG_CONFIG=` 目标，会被原地重写。
6. 用 `bin/clang.real`，不要用 `bin/clang`。
7. 用 `gdb-multiarch`；系统寄存器名全大写且只读；`pstate` 无效，用 `cpsr`。
8. `dump binary memory` 不要加 `(char *)` 强转。
9. `-no-reboot` + `panic=-1` 是 panic 后保住现场的唯一前提。
10. 定位 initcall 崩溃用 `initcall_debug ignore_loglevel`。
11. QEMU stdin 接 FIFO 时首批字节会被吞，先发裸换行预热；不要用 banner 串做同步。
12. 解析 guest 输出前先 `tr -d '\r'`。
13. 任意操作遇 3 次沙箱阻挡/权限不足，立即请用户临时提权，不要绕。

---

## 9. 证据与脚本索引（抽象）

远程关键产物：`out-pad/{vmlinux, System.map, arch/arm64/boot/Image, .config}`、`initramfs-pad.cpio.gz`、`qshare/`、`backup/*.orig-20261003`。

关键脚本：`pad-qemu-setup.sh`（备份 + 开 console/virtio/9p）、`pad-fix-earlyrandom.sh`（关 `QCOM_EARLY_RANDOM` + 重编）、`pad-mkinitramfs.sh`、`padboot-probe21.sh`（最终成功验证）、`padboot-logdump.sh`（无 console 时 dump 内核 log buffer）、`pad-qemu-triple-test.sh`（验证 `CLANG_TRIPLE`）。

关键日志：构建失败/成功日志；`padlog*.bin`（log buffer dump）；`qemu-int.log`（障碍 1 异常）；`pad-int17.log`（障碍 3 的 `smc` 异常）；`pad-q18.log`（`secure=on` 后定位 `proc_app_info_init`）；`pad-q19.log`（`-m 2048` 无效对照）；`pad-q20.log`（首次进用户态）；`pad-q21.log`（最终成功 + 9p 双向）。

*压缩版。所有数值来自实测；推断处已标明。已移除设备序列号、个人主机名/用户名、ADB 与本机绝对路径等敏感环境信息。*
