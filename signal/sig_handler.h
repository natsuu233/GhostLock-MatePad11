#pragma once
/* 手写汇编入口 l11_tramp 的实体在 sig_handler.cpp（不能放头文件：每个包含者都会定义一次 ⇒ 链接冲突）。
 * 进入时（内核 setup_return 设好）：x0=sig x1=siginfo x2=ucontext(=frame+0x80) sp=frame。 */
extern "C" void l11_tramp(void);
