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
 * FILE: os-cfg.h
 * MỤC ĐÍCH: File cấu hình trung tâm của toàn bộ OS simulator.
 *           Tất cả các tính năng được bật/tắt tại đây bằng
 *           cách comment/uncomment các #define.
 *           File này được include (gián tiếp) bởi hầu hết
 *           các file khác thông qua common.h → os-cfg.h.
 * ============================================================
 */

#ifndef OSCFG_H
#define OSCFG_H

/* ── SCHEDULER ──────────────────────────────────────────────
 * MLQ_SCHED: Bật thuật toán Multi-Level Queue Scheduling.
 *   - Mỗi tiến trình có một mức ưu tiên (priority 0..139).
 *   - Tiến trình ưu tiên cao hơn (số nhỏ hơn) được chạy trước.
 *   - Nếu comment dòng này → dùng FIFO scheduling đơn giản.
 * MAX_PRIO: Số mức ưu tiên tối đa (0 = cao nhất, 139 = thấp nhất).
 */
#define MLQ_SCHED 1
#define MAX_PRIO 140

/* ── MEMORY MANAGEMENT ──────────────────────────────────────
 * MM_PAGING: Bật chế độ quản lý bộ nhớ bằng Paging.
 *   - Nếu tắt → dùng cơ chế cũ (mem.c, phân đoạn đơn giản).
 * MM_FIXED_MEMSZ: (tắt) Dùng kích thước RAM/SWAP cố định
 *   thay vì đọc từ file config. Dùng để tương thích ngược
 *   với format file config cũ.
 */
#define MM_PAGING
//#define MM_FIXED_MEMSZ

/* ── DEBUG FLAGS ─────────────────────────────────────────────
 * VMDBG : (tắt) In thông tin debug về Virtual Memory.
 * MMDBG : (tắt) In thông tin debug về Memory Management.
 * IODUMP: In các thao tác bộ nhớ ra màn hình
 *         (liballoc:178, libfree:218, libread:426, libwrite:502).
 * PAGETBL_DUMP: In nội dung bảng trang sau mỗi thao tác MM.
 *   → Cả hai IODUMP + PAGETBL_DUMP phải được bật để thấy
 *     output dạng "print_pgtbl: PDG=... P4g=... ..."
 */
//#define VMDBG 1
//#define MMDBG 1
#define IODUMP 1
#define PAGETBL_DUMP 1

/* ── ADDRESS MODE ────────────────────────────────────────────
 * MM64: Chọn chế độ địa chỉ 64-bit.
 *   - Bật  → addr_t = uint64_t, dùng mm64.c (bảng trang 5 cấp)
 *   - Tắt  → addr_t = uint32_t, dùng mm.c   (bảng trang đơn giản)
 * Để tắt: comment dòng #define MM64 và bỏ comment //#undef MM64
 */
#define MM64 1
//#undef MM64

#endif
