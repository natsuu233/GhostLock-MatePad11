#pragma once
#include "const.h"
void notify_child(ShareMem* sh, int cmd);
void clean_child(pid_t child_pid, unsigned long child_task, ShareMem *sh);