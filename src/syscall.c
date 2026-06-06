/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * ============================================================
 * FILE: syscall.c
 * MỤC ĐÍCH: Bảng syscall và dispatcher — cổng vào kernel từ user space.
 *
 * THIẾT KẾ "X-MACRO":
 *   File syscalltbl.lst định nghĩa tất cả syscall theo mẫu:
 *     __SYSCALL(nr, sym)   ← nr=số hiệu, sym=tên hàm
 *
 *   Bằng cách redefine macro __SYSCALL trước mỗi lần #include, ta tạo ra:
 *     1. extern int __sym(...): khai báo hàm thực thi (trong sys_mem.c)
 *     2. sys_call_table[]: mảng string debug "nr-sym" cho mỗi syscall
 *     3. switch(nr) case nr: __sym(...): dispatch từ số hiệu → hàm
 *
 *   Kỹ thuật này cho phép thêm syscall mới chỉ bằng 1 dòng trong .lst
 *   mà không cần sửa syscall.c.
 *
 * LUỒNG SYSCALL:
 *   User code (libmem.c) → _syscall(krnl, pid, 17, &regs)
 *                       → switch(17) → case 17 → __sys_memmap(krnl, pid, regs)
 *                       → (sys_mem.c) xử lý memory operation
 *
 * sc_regs: struct truyền tham số syscall (tương tự register a0-a5 trong RISC-V):
 *   a1 = opcode (SYSMEM_INC_OP, SYSMEM_IO_READ, ...)
 *   a2 = argument 1
 *   a3 = argument 2 / return value
 * ============================================================
 */
#include "syscall.h"
#include "common.h"

/* Bước 1: Khai báo extern cho tất cả hàm syscall trong syscalltbl.lst */
#define __SYSCALL(nr, sym) extern int __##sym(struct krnl_t *, uint32_t, struct sc_regs *);
#include "syscalltbl.lst"
#undef __SYSCALL

/* Bước 2: Tạo mảng debug string — mỗi entry là "nr-sym" */
#define __SYSCALL(nr, sym) #nr "-" #sym,
const char *sys_call_table[] = {
#include "syscalltbl.lst"
};
#undef __SYSCALL
const int syscall_table_size = sizeof(sys_call_table) / sizeof(char *);

/* __sys_ni_syscall: Handler cho syscall chưa được implement (Not Implemented).
 * Giống linux's sys_ni_syscall — trả về 0 mà không làm gì. */
int __sys_ni_syscall(struct krnl_t *krnl, struct sc_regs *regs)
{
  (void)krnl; (void)regs;
  return 0;
}

/*
 * _syscall: Dispatcher — phân phối syscall số hiệu nr tới hàm xử lý.
 *
 * Bước 3: Tạo switch-case từ syscalltbl.lst.
 * Mỗi case nr: gọi __sym(krnl, pid, regs) tương ứng.
 * Nếu nr không có trong bảng: gọi __sys_ni_syscall (dummy).
 */
#define __SYSCALL(nr, sym) case nr: return __##sym(krnl, pid, regs);
int _syscall(struct krnl_t *krnl, uint32_t pid, uint32_t nr, struct sc_regs *regs)
{
  switch (nr) {
#include "syscalltbl.lst"
  default: return __sys_ni_syscall(krnl, regs);
  }
}
#undef __SYSCALL

