#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <csignal>
#include <unistd.h>
#include <bits/types/stack_t.h>
#include <sys/mman.h>
#include "../tools.h"
#include "../const.h"
#include <sys/stat.h>
static Print print("setup_sig");
void setup_store() {
    store_fd = open(STORE_PATH, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (store_fd < 0) {
        print("阻塞文件建立失败：",strerror(errno));
        exit(1);
    }
    if (ftruncate(store_fd, STORE_FILE_SIZE) != 0) {
        print("ftruncate失败: ",strerror(errno));
        exit(1);
    }
    auto buf = std::make_shared<char[]>(STORE_FILE_SIZE);//自动执行0初始化
    auto n = pwrite(store_fd, buf.get(), STORE_FILE_SIZE, 0);
    if (n != STORE_FILE_SIZE) {
        print("阻塞文件没写满或者出错了 ",strerror(errno));
        exit(1);
    }
    if (fsync(store_fd) != 0) {
        print("fsync失败 ",strerror(errno));
        exit(1);
    }
}

void setup_altstack() {
    if (getenv("PAD")) {
        STORE_PATH = "/data/local/tmp/erblk";
        system("mkdir -p /data/local/tmp");
    }
    setup_store(); //建阻塞源
    void *A = mmap(nullptr, MAP_FILE_LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE, store_fd, 0);
    if (A == MAP_FAILED) {
        print("文件映射建立失败");
        exit(1);
    }
    long size = SS_SIZE;
    for (int i = 0; i < 10; i++) {
        stack_t ss{}; ss.ss_sp = A; ss.ss_size = size; ss.ss_flags = 0;
        if (sigaltstack(&ss, nullptr) != 0) {
            print("sigaltstack失败：",strerror(errno));
            exit(1);
        }
        check_mode = true;
        syscall(SYS_tgkill, getpid(), my_tid(), SIGUSR1);
        while (sigframe == 0) { //注意信号handler是异步的
            asm("yield");
        }
        check_mode = false;
        unsigned int pgoff = sigframe & 0xFFFUL;
        if (pgoff != WANT_PGOFF) {
            long d = (long)pgoff - (long)WANT_PGOFF;
            if (d >  0x800) d -= 0x1000;
            if (d < -0x800) d += 0x1000;
            size -= d;
            if (size+0x1000 < SS_SIZE || size > SS_SIZE+0x1000) {
                print("ss_size 越界，放弃");
                exit(1);
            }
        }
        else return;
    }
    print("ss_size重试次数超限");
    exit(1);
}