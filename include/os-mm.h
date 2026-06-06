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
 * FILE: os-mm.h
 * MỤC ĐÍCH: Định nghĩa tất cả các cấu trúc dữ liệu cốt lõi
 *           của hệ thống quản lý bộ nhớ (Memory Management).
 *
 * CÁC STRUCT CHÍNH:
 *   pgn_t          → node trong linked list lưu page numbers
 *   vm_rg_struct   → một vùng nhớ ảo [rg_start, rg_end)
 *   vm_area_struct → một vùng địa chỉ ảo (VMA) của tiến trình
 *   mm_struct      → toàn bộ trạng thái MM của một tiến trình
 *   framephy_struct→ một frame vật lý trong RAM/SWAP
 *   memphy_struct  → thiết bị bộ nhớ vật lý (RAM hoặc SWAP)
 * ============================================================
 */

#ifndef OSMM_H
#define OSMM_H

#include <stdint.h>

#define MM_PAGING
/* PAGING_MAX_MMSWP: số vùng SWAP tối đa được hỗ trợ (4 vùng). */
#define PAGING_MAX_MMSWP 4
/* PAGING_MAX_SYMTBL_SZ: số biến tối đa một tiến trình có thể alloc.
 * Mỗi lần alloc được gán một reg_index (0..29) trong symrgtbl[]. */
#define PAGING_MAX_SYMTBL_SZ 30

/* ── KIỂU DỮ LIỆU ĐỊA CHỈ ───────────────────────────────────
 * addr_t: kiểu địa chỉ ảo/vật lý dùng trong toàn bộ OS.
 *   MM64 bật  → uint64_t (64-bit, dải 0 .. 2^57)
 *   MM64 tắt  → uint32_t (32-bit, dải 0 .. 2^22 = 4MB)
 * BYTE: kiểu 1 byte dùng để đọc/ghi bộ nhớ vật lý.
 */
#ifdef MM64
#define ADDR_TYPE uint64_t
#else
#define ADDR_TYPE uint32_t
#endif

typedef char BYTE;
typedef ADDR_TYPE addr_t;

/* Format string khi in addr_t:
 *   FORMAT_ADDR  → in dạng số thập phân (%lu hoặc %d)
 *   FORMATX_ADDR → in dạng hex có padding
 */
#ifdef MM64
#define FORMAT_ADDR "%lu"
#define FORMATX_ADDR "%16p"
#else
#define FORMAT_ADDR "%d"
#define FORMATX_ADDR "%08x"
#endif

/* ── pgn_t: NODE DANH SÁCH PAGE NUMBER ───────────────────────
 * Dùng để lưu danh sách các page đang được sử dụng (fifo_pgn).
 * Thuật toán FIFO page replacement: trang nào vào trước, ra trước.
 *
 *   pgn      : số hiệu trang ảo (Virtual Page Number)
 *   pg_next  : con trỏ đến node kế tiếp (linked list đơn)
 */
struct pgn_t {
   addr_t pgn;
   struct pgn_t *pg_next;
};

/* ── vm_rg_struct: VÙNG NHỚ ẢO (REGION) ─────────────────────
 * Mô tả một đoạn địa chỉ ảo liên tục [rg_start, rg_end).
 * Dùng cho hai mục đích:
 *   1. symrgtbl[]: theo dõi vùng nhớ đã alloc cho từng biến
 *   2. vm_freerg_list: danh sách các vùng nhớ đã free (có thể tái sử dụng)
 *
 *   vmaid    : ID của VMA chứa region này (thường = 0)
 *   rg_start : địa chỉ ảo bắt đầu (inclusive)
 *   rg_end   : địa chỉ ảo kết thúc (exclusive)
 *   rg_next  : con trỏ đến region tiếp theo trong free list
 */
struct vm_rg_struct {
   int vmaid;
   addr_t rg_start;
   addr_t rg_end;
   /* Privilege mode bit:
    *   usermode   - mode_bit = 1
    *   kernelmode - mode_bit = 0 */
   unsigned long mode_bit;
   struct vm_rg_struct *rg_next;
};

/* ── vm_area_struct: VÙNG ĐỊA CHỈ ẢO (VMA) ─────────────────
 * Mô tả một "segment" trong không gian địa chỉ ảo của tiến trình.
 * (Tương tự /proc/PID/maps trong Linux thực)
 * OS simulator này chỉ dùng 1 VMA (vm_id = 0) cho mỗi tiến trình.
 *
 *   vm_id          : ID của vùng (0, 1, 2, ...)
 *   vm_start       : địa chỉ ảo bắt đầu của vùng
 *   vm_end         : địa chỉ ảo kết thúc hiện tại (tăng khi alloc)
 *   sbrk           : con trỏ "program break" — vị trí cuối của heap
 *                    (tương tự hàm sbrk() trong Unix)
 *   vm_mm          : con trỏ ngược về mm_struct chứa VMA này
 *   vm_freerg_list : danh sách các region đã free, sẵn sàng tái dùng
 *   vm_next        : VMA tiếp theo (linked list các VMA)
 */
struct vm_area_struct {
   unsigned long vm_id;
   addr_t vm_start;
   addr_t vm_end;
   addr_t sbrk;
   struct mm_struct *vm_mm;
   struct vm_rg_struct *vm_freerg_list;
   struct vm_area_struct *vm_next;
};

/* ── kcache_pool_struct: KERNEL CACHE POOL ───────────────────
 * Hỗ trợ cấp phát theo "slab allocator" style trong kernel.
 * Mỗi pool có các slot có cùng kích thước (size), căn chỉnh (align).
 *
 *   size    : kích thước của mỗi slot trong pool
 *   align   : alignment yêu cầu
 *   storage : địa chỉ ảo nơi pool được lưu
 */
struct kcache_pool_struct {
   int size;
   int align;
#ifdef MM64
   addr_t storage;
#else
   uint32_t storage;
#endif
};

/* ── mm_struct: TRẠNG THÁI MM CỦA MỘT TIẾN TRÌNH ────────────
 * Đây là struct trung tâm của memory management.
 * Mỗi tiến trình có một mm_struct riêng (tạo trong ld_routine/os.c).
 *
 * [Chế độ MM64] Bảng trang 5 cấp (kiến trúc x86-64 style):
 *   pgd[512]: Page Global Directory   — cấp 1 (bits 56:48)
 *   p4d[512]: Page 4th-level Dir      — cấp 2 (bits 47:39)
 *   pud[512]: Page Upper Directory    — cấp 3 (bits 38:30)
 *   pmd[512]: Page Middle Directory   — cấp 4 (bits 29:21)
 *   pt [512]: Page Table              — cấp 5 (bits 20:12)
 *   → Mỗi entry là addr_t (8 bytes), tổng mỗi cấp = 512 * 8 = 4KB
 *
 * [Chế độ 32-bit] Chỉ dùng pgd (bảng trang 1 cấp đơn giản).
 *
 *   mmap       : danh sách các VMA của tiến trình
 *   symrgtbl[] : symbol table — mảng 30 entry, mỗi entry là một
 *                vm_rg_struct lưu thông tin vùng nhớ của một biến.
 *                reg_index trong lệnh ALLOC/FREE/READ/WRITE là
 *                chỉ số vào mảng này.
 *   fifo_pgn   : linked list các page đang dùng (cho FIFO replacement)
 *   kcpooltbl  : bảng các kernel cache pool (cho kmalloc/kcache)
 */
struct mm_struct {
#ifdef MM64
   addr_t *pgd;  /* Page Global Directory */
   addr_t *p4d;  /* Page 4th-level Directory */
   addr_t *pud;  /* Page Upper Directory */
   addr_t *pmd;  /* Page Middle Directory */
   addr_t *pt;   /* Page Table (lưu FPN thực sự) */
#else
   uint32_t *pgd;
#endif
   struct vm_area_struct *mmap;
   struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];
   struct pgn_t *fifo_pgn;
   struct kcache_pool_struct *kcpooltbl;
};

/* ── framephy_struct: FRAME VẬT LÝ ──────────────────────────
 * Mô tả một frame (khung trang) trong bộ nhớ vật lý.
 * Được dùng để quản lý danh sách frame trống/đang dùng.
 *
 *   fpn     : Frame Page Number — số hiệu frame trong RAM/SWAP
 *             (địa chỉ vật lý = fpn * PAGING_PAGESZ)
 *   fp_next : con trỏ đến frame tiếp theo trong linked list
 *   owner   : tiến trình đang sở hữu frame này
 */
struct framephy_struct {
   addr_t fpn;
   struct framephy_struct *fp_next;
   struct mm_struct *owner;
};

/* ── memphy_struct: THIẾT BỊ BỘ NHỚ VẬT LÝ ─────────────────
 * Mô phỏng một thiết bị bộ nhớ (RAM hoặc SWAP disk).
 * Dùng mảng byte (storage) để lưu dữ liệu thực sự.
 *
 *   storage      : mảng byte lưu nội dung bộ nhớ
 *                  (cấp phát trong init_memphy bằng malloc)
 *   maxsz        : kích thước tối đa (bytes), ví dụ 256MB cho RAM
 *   rdmflg       : 1 = random access (RAM), 0 = sequential (SWAP disk)
 *   cursor       : vị trí đọc/ghi hiện tại (chỉ dùng khi sequential)
 *   free_fp_list : danh sách các frame đang trống (sẵn để cấp phát)
 *   used_fp_list : danh sách các frame đang được dùng
 */
struct memphy_struct {
   BYTE *storage;
   int maxsz;
   int rdmflg;
   int cursor;
   struct framephy_struct *free_fp_list;
   struct framephy_struct *used_fp_list;
};

#endif
