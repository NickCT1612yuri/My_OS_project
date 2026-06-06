/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * ============================================================
 * FILE: sys_listsyscall.c
 * MỤC ĐÍCH: Triển khai syscall "listsyscall" — liệt kê toàn bộ
 *           các syscall đã đăng ký trong bảng sys_call_table.
 *
 * Dùng chủ yếu để debug: kiểm tra xem kernel đã có những syscall
 * nào, số hiệu và tên của từng syscall.
 *
 * Output mẫu (mỗi dòng một entry):
 *   17-sys_memmap
 *   (số hiệu)-(tên hàm)
 * ============================================================
 */
#include "syscall.h"

/*
 * __sys_listsyscall: In ra danh sách toàn bộ syscall trong bảng.
 *
 * sys_call_table[] và syscall_table_size được khởi tạo tự động
 * từ syscalltbl.lst thông qua kỹ thuật X-macro trong syscall.c.
 *
 * Tham số:
 *   krnl : không dùng (syscall này không cần truy cập kernel state)
 *   pid  : không dùng
 *   reg  : không dùng (không có input/output qua thanh ghi)
 *
 * Trả về 0 (luôn thành công).
 */
int __sys_listsyscall(struct krnl_t *krnl, uint32_t pid, struct sc_regs *reg)
{
	(void)krnl; (void)pid; (void)reg;

	int i;
	for (i = 0; i < syscall_table_size; i++)
		printf("%s\n", sys_call_table[i]);

	return 0;
}
