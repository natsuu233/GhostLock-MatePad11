#pragma once
#include <iostream>
#include <utility>
#include <vector>
//仿python并自动加前缀
class Print {
public:
    Print(std::string name) : m_name(std::move(name)) {}

    template <class... Args>
    void operator()(const Args&... args) const {
        std::cout << "[" << m_name << "] ";
        ((std::cout << args), ...);
        std::cout << std::endl;
    }
private:
    std::string m_name;
};

//计时器，测窗口
unsigned long rdcnt();
unsigned long rdcntfrq();
//信号处理函数用
void pstr(const char *s);
//杂项
long my_tid();
void mysleep(int ms);
void leak_slide();
void ptime();
void init_addrs();
void setup_altstack();
void selfCheck();
void update_waiter_layout();
std::string ultohex(unsigned long v, bool showbase = false);
// 读 addr（运行时地址）处 8 字节。layout=1，寄生写落 addr+8
unsigned long read_1_rt(unsigned long addr);
// 写 addr = val（运行时地址）。layout=1，寄生写落 val+8
void write_1_rt(unsigned long addr, unsigned long val);
//只是粗筛内核指针，对于骁龙865/Linux4.19够用
inline int is_kptr(unsigned long v) {
    return v > 0xffff'0000'0000'0000ULL && v < 0xffff'ffff'ffff'0000ULL;
}
//传入当前task_struct（运行时地址），读取comm前8字节和tasks.prev，寄生写落点是sched_info.pcount，风险低
void read_comm_prev(unsigned long task, unsigned long &comm, unsigned long &prev_list_head);