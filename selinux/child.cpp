#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "const.h"
#include "../const.h"
#include <fstream>
void child_wait_cmd(ShareMem *sh) {
    //警告：父进程是多线程程序，根据POSIX标准，exec()前只能调用异步信号安全函数
    //syscall()本身不是异步信号安全的标准保证，应直接用POSIX函数
    int fd = -1; // /sys/fs/selinux/load 的 open() fd
    while (true) {
        // 未下达命令则睡眠
        if (sh->seq == sh->ack) {
            timespec ts{};
            ts.tv_nsec = V_SPIN_US * 1000;
            nanosleep(&ts, nullptr);
            continue;
        }

        errno = 0;
        sh->err = 0;
        const int cmd = sh->cmd;
        if (cmd == CMD_GETUID) {
            sh->rc = getuid();
        } else if (cmd == CMD_OPEN_LOAD) {
            sh->rc = open(PATH_LOAD, O_WRONLY);
            if (sh->rc > 0) fd = sh->rc;
            else sh->err = errno;
        } else if (cmd == CMD_GENERATE_AVC) {
            auto buf = new char[PROBE_LEN];
            pwrite(fd, buf, PROBE_LEN, 0);
            delete[] buf;
            sh->err = errno; // 期望 EFBIG
        } else if (cmd == CMD_WRITE_LOAD) {
            // 为确保异步信号安全，不用C++库
            char* p = g_blob.data();
            auto len = g_blob.size();
            while (len > 0) {
                auto rc = write(fd, p, len);
                if (rc < 0) {
                    if (errno == EINTR) continue;
                    else {
                        sh->err = errno;
                        break;
                    }
                }
                len -= rc;
                p += rc;
            }
        } else if (cmd == CMD_CLOSE) {
            if (fd > 0) close(fd);
        }
        ++sh->ack;
    }
}