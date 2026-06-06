/*
 * ============================================================
 * FILE: common.h
 * MỤC ĐÍCH: Định nghĩa các cấu trúc "toàn cục" được dùng bởi
 *           hầu hết mọi file trong project:
 *             - Tập lệnh của CPU giả lập (inst_t, code_seg_t)
 *             - Process Control Block (pcb_t)
 *             - Kernel structure (krnl_t)
 *
 * THỨ TỰ INCLUDE:
 *   common.h → os-cfg.h (flags) → os-mm.h (addr_t, mm_struct...)
 * ============================================================
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdio.h>

#ifndef OSCFG_H
#include "os-cfg.h"   /* flags: MM64, MLQ_SCHED, MM_PAGING, ... */
#endif

#ifndef OSMM_H
#include "os-mm.h"    /* addr_t, mm_struct, memphy_struct, ... */
#endif

/* ── HẰNG SỐ LEGACY (dùng cho chế độ không-paging cũ) ───────
 * ADDRESS_SIZE : độ rộng địa chỉ (bit) — 20-bit = 4MB
 * OFFSET_LEN   : số bit offset trong trang — 10-bit = 1KB trang
 * FIRST_LV_LEN : số bit chỉ mục cấp 1 — 5-bit = 32 mục
 * SECOND_LV_LEN: số bit chỉ mục cấp 2 — 5-bit = 32 mục
 * NUM_PAGES    : tổng số trang ảo = 2^(20-10) = 1024
 * PAGE_SIZE    : kích thước mỗi trang = 2^10 = 1024 byte
 */
#define ADDRESS_SIZE 20
#define OFFSET_LEN 10
#define FIRST_LV_LEN 5
#define SECOND_LV_LEN 5
#define SEGMENT_LEN FIRST_LV_LEN
#define PAGE_LEN SECOND_LV_LEN
#define NUM_PAGES (1 << (ADDRESS_SIZE - OFFSET_LEN))
#define PAGE_SIZE (1 << OFFSET_LEN)

/* ── KIỂU THAM SỐ LỆNH ──────────────────────────────────────
 * arg_t: kiểu dữ liệu của tham số trong một lệnh (instruction).
 *   MM64 → uint64_t (tham số 64-bit, ví dụ kích thước alloc lớn)
 *   32-bit → uint32_t
 * FORMAT_ARG: format string khi printf tham số (%lu hoặc %u).
 */
#ifdef MM64
#define ARG_TYPE uint64_t
#else
#define ARG_TYPE uint32_t
#endif
typedef ARG_TYPE arg_t;

#ifdef MM64
#define FORMAT_ARG "%lu"
#else
#define FORMAT_ARG "%u"
#endif

/* ── TẬP LỆNH CPU (INSTRUCTION SET) ─────────────────────────
 * Đây là "ISA" của CPU giả lập. Mỗi tiến trình là một dãy
 * các lệnh này, đọc từ file (ví dụ input/proc/p0s).
 *
 *   CALC         : tính toán đơn giản (không dùng bộ nhớ)
 *   ALLOC        : cấp phát vùng nhớ (kích thước, reg_index)
 *   FREE         : giải phóng vùng nhớ theo reg_index
 *   READ         : đọc 1 byte từ vùng nhớ (source_reg, offset, dest_reg)
 *   WRITE        : ghi 1 byte vào vùng nhớ (data, dest_reg, offset)
 *   KMALLOC      : cấp phát bộ nhớ kernel (MM_PAGING)
 *   KMEM_CACHE_CREATE: tạo một kernel cache pool
 *   KMEM_CACHE_ALLOC : cấp phát từ cache pool
 *   COPY_FROM_USER   : copy dữ liệu từ user → kernel space
 *   COPY_TO_USER     : copy dữ liệu từ kernel → user space
 *   SYSCALL      : gọi trực tiếp một system call
 */
enum ins_opcode_t {
	CALC,
	ALLOC,
	FREE,
	READ,
	WRITE,
	KMALLOC,
	KMEM_CACHE_CREATE,
	KMEM_CACHE_ALLOC,
	COPY_FROM_USER,
	COPY_TO_USER,
	SYSCALL,
};

/* inst_t: một lệnh đơn gồm opcode + tối đa 4 tham số (arg_0..arg_3).
 * Ví dụ lệnh ALLOC: opcode=ALLOC, arg_0=size, arg_1=reg_index
 * Ví dụ lệnh WRITE: opcode=WRITE, arg_0=data, arg_1=dest_reg, arg_2=offset
 */
struct inst_t {
	enum ins_opcode_t opcode;
	arg_t arg_0;
	arg_t arg_1;
	arg_t arg_2;
	arg_t arg_3;
};

/* code_seg_t: "đoạn code" của tiến trình.
 *   text: mảng các lệnh (đọc từ file tiến trình)
 *   size: số lệnh trong chương trình
 */
struct code_seg_t {
	struct inst_t *text;
	uint32_t size;
};

/* ── CẤU TRÚC BẢNG TRANG LEGACY (chế độ không-paging) ───────
 * trans_table_t / page_table_t: bảng trang 2 cấp đơn giản.
 * Chỉ dùng khi MM_PAGING không được bật. Không liên quan đến
 * mm64.c (bảng trang 5 cấp).
 */
struct trans_table_t {
	struct {
		addr_t v_index; /* chỉ mục địa chỉ ảo */
		addr_t p_index; /* chỉ mục địa chỉ vật lý */
	} table[1 << SECOND_LV_LEN];
	int size;
};

struct page_table_t {
	struct {
		addr_t v_index;
		struct trans_table_t *next_lv;
	} table[1 << FIRST_LV_LEN];
	int size;
};

/* ── PCB: PROCESS CONTROL BLOCK ─────────────────────────────
 * Lưu toàn bộ thông tin về một tiến trình đang chạy.
 * Đây là struct quan trọng nhất ở tầng tiến trình.
 *
 *   pid        : Process ID, tăng dần từ 1 (avail_pid trong loader.c)
 *   priority   : ưu tiên mặc định, đọc từ file tiến trình (FIXED)
 *   path[100]  : đường dẫn file code của tiến trình
 *   code       : con trỏ đến đoạn code (mảng lệnh)
 *   regs[10]   : 10 thanh ghi của CPU — mỗi thanh ghi lưu địa chỉ
 *                bắt đầu của một vùng nhớ đã alloc.
 *                reg_index trong lệnh ALLOC/FREE/READ/WRITE là
 *                chỉ số vào mảng regs[] này.
 *   pc         : Program Counter — chỉ vào lệnh tiếp theo cần thực thi
 *   prio       : ưu tiên hiện tại (có thể thay đổi khi chạy) — chỉ có
 *                khi MLQ_SCHED bật. Dùng cho MLQ scheduler.
 *   krnl       : con trỏ đến kernel của tiến trình này.
 *                Mỗi tiến trình có krnl riêng (copy từ os global),
 *                nhưng chia sẻ mram/mswp với nhau.
 *   page_table : bảng trang legacy (chỉ dùng khi không có MM_PAGING)
 *   bp         : break pointer (legacy, = PAGE_SIZE)
 */
struct pcb_t {
	uint32_t pid;
	uint32_t priority;
	char path[100];
	struct code_seg_t *code;
	addr_t regs[10];
	uint32_t pc;
#ifdef MLQ_SCHED
	uint32_t prio; /* ưu tiên thực tế, dùng cho MLQ */
#endif
	struct krnl_t *krnl;
	struct page_table_t *page_table; /* legacy */
	uint32_t bp;                     /* legacy */
};

/* ── krnl_t: KERNEL STRUCTURE ───────────────────────────────
 * Mô phỏng một "kernel context" — chứa tất cả tài nguyên
 * kernel mà một tiến trình có thể truy cập.
 *
 * Trong os.c, có một biến global `os` kiểu krnl_t.
 * Khi load mỗi tiến trình, os.c tạo một bản copy krnl riêng
 * cho tiến trình đó (kế thừa queues + thiết bị nhớ từ os global).
 *
 *   ready_queue    : hàng đợi các tiến trình sẵn sàng (FIFO simple)
 *   running_list   : danh sách các tiến trình đang chạy
 *   mlq_ready_queue: mảng 140 hàng đợi (một hàng/mức ưu tiên) — MLQ
 *
 *   mm             : MM struct riêng của tiến trình (bảng trang + VMA)
 *   mram           : RAM vật lý (dùng chung giữa các tiến trình)
 *   mswp           : mảng các vùng SWAP (dùng chung)
 *   active_mswp    : SWAP đang được dùng hiện tại
 *   active_mswp_id : ID của SWAP đang active
 *
 *   krnl_pgd/.../krnl_pt: bảng trang riêng của KERNEL
 *                          (cho kernel memory management)
 */
struct krnl_t {
	struct queue_t *ready_queue;
	struct queue_t *running_list;
#ifdef MLQ_SCHED
	struct queue_t *mlq_ready_queue;
#endif
#ifdef MM_PAGING
	struct mm_struct *mm;          /* mm riêng của tiến trình */
	struct memphy_struct *mram;    /* RAM vật lý (shared) */
	struct memphy_struct **mswp;   /* mảng SWAP (shared) */
	struct memphy_struct *active_mswp;
	uint32_t active_mswp_id;
#ifdef MM64
	addr_t *krnl_pgd;  /* bảng trang kernel — cấp PGD */
	addr_t *krnl_p4d;  /* bảng trang kernel — cấp P4D */
	addr_t *krnl_pud;  /* bảng trang kernel — cấp PUD */
	addr_t *krnl_pmd;  /* bảng trang kernel — cấp PMD */
	addr_t *krnl_pt;   /* bảng trang kernel — cấp PT  */
#else
	uint32_t *krnl_pgd;
#endif
#endif
};

#endif
