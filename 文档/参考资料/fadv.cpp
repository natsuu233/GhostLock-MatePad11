#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <filesystem>
const char* PATH = "/home/natsu/ghostlock/pad-res/delay_share/erblk";
int main() {
    int ret = std::system("mount | grep -q slow");
    if (ret != 0) {
        std::cout << R"(还没挂载慢速文件系统，请在~/ghostlock/pad-res/执行：
            sudo losetup -f slow_disk.img
            sudo dmsetup create slow_dev --table "0 262144 delay /dev/loop0 0 50"
            sudo mount /dev/mapper/slow_dev delay_share/)" << std::endl;
        return 0;
    }
    std::cout << "开始丢缓存" << std::endl;

    while(1) {
        int fd = open(PATH, O_RDONLY);
        if (fd < 0) {
            if (errno != ENOENT) {
                std::perror("open出错=");
                return 1;
            }
            usleep(200);  // guest 尚未建好文件
            continue;
        }
        int rc = posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        close(fd);
        if (rc != 0) {
            errno = rc;
            std::perror("posix_fadvise出错=");
            return 1;
        }
    }
}
