/*
 * ============================================================
 * FILE: mm64.h
 * MỤC ĐÍCH: Định nghĩa cấu trúc địa chỉ 64-bit theo mô hình
 *           paging 5 cấp của Linux x86-64 (MM64 mode).
 *
 * MÔ HÌNH PHÂN CẤP PAGE TABLE 5 CẤP:
 * ─────────────────────────────────────────────────────────────
 * Địa chỉ ảo 57-bit được chia thành 6 phần:
 *
 *  Bit:  56..48   47..39   38..30   29..21   20..12   11..0
 *       ┌───────┬────────┬────────┬────────┬────────┬────────┐
 *       │  PGD  │  P4D   │  PUD   │  PMD   │  PT    │ OFFSET │
 *       │ 9 bit │ 9 bit  │ 9 bit  │ 9 bit  │ 9 bit  │ 12 bit │
 *       └───────┴────────┴────────┴────────┴────────┴────────┘
 *
 *  PGD (Page Global Directory)   : cấp 1 — mảng mm->pgd[]
 *  P4D (Page 4th-level Directory): cấp 2 — mảng mm->p4d[]
 *  PUD (Page Upper Directory)    : cấp 3 — mảng mm->pud[]
 *  PMD (Page Middle Directory)   : cấp 4 — mảng mm->pmd[]
 *  PT  (Page Table)              : cấp 5 — mảng mm->pt[]
 *  OFFSET                        : 12 bit → 4096 byte/trang
 *
 * MỖI CẤP LÀ 9 BIT → 512 entries/cấp (2^9)
 * Tổng: 9×5 + 12 = 57 bit địa chỉ ảo → 128 PB không gian ảo
 *
 * TRONG SIMULATOR NÀY:
 *   Mỗi mảng (pgd[], p4d[], ..., pt[]) có PAGING64_MAX_PGN entries.
 *   Việc lookup dùng (chỉ số % PAGING64_MAX_PGN) để tránh tràn.
 *   PAGING64_MAX_PGN = ceil(2^21 / 4096) = 512 entries.
 * ============================================================
 */
#ifndef MM64_H
#define MM64_H

#include "mm.h"

/* Độ rộng từ của kiến trúc 64-bit */
#define MM64_BITS_PER_LONG 64

/* PAGING64_CPU_BUS_WIDTH: 57 bit = giới hạn thực tế của x86-64 (không dùng hết 64 bit)
 * PAGING64_PAGESZ       : 4096 byte = kích thước trang tiêu chuẩn Linux/x86 */
#define PAGING64_CPU_BUS_WIDTH 57
#define PAGING64_PAGESZ        4096

/* GENMASK64: tạo mask 64-bit từ bit l (thấp) đến bit h (cao)
 * Ví dụ: GENMASK64(20,12) → bits 20:12 = 0x1FF000 (9 bit) */
#define GENMASK64(h, l) \
    (((~0ULL) << (l)) & (~0ULL >> (MM64_BITS_PER_LONG - (h) - 1)))

/* PAGING64_MAX_PGN: số lượng entry tối đa trong mỗi mảng page directory.
 * = ceil(2^21 / 4096) = 512 — đủ để lưu tất cả page number trong vùng test */
#define PAGING64_MAX_PGN        (DIV_ROUND_UP(BIT_ULL(21), PAGING64_PAGESZ))
/* Làm tròn kích thước lên bội của 4096 */
#define PAGING64_PAGE_ALIGNSZ(sz) (DIV_ROUND_UP(sz, PAGING64_PAGESZ) * PAGING64_PAGESZ)

/* ── VỊ TRÍ BIT CỦA TỪNG CẤP TRONG ĐỊA CHỈ ẢO ────────────────────────────
 * Mỗi cấp chiếm 9 bit liên tiếp trong địa chỉ 57-bit:
 *
 *   OFFSET : bits 11..0  (12 bit) — vị trí trong trang
 *   PT     : bits 20..12 ( 9 bit) — chỉ số trong Page Table
 *   PMD    : bits 29..21 ( 9 bit) — chỉ số trong Page Middle Directory
 *   PUD    : bits 38..30 ( 9 bit) — chỉ số trong Page Upper Directory
 *   P4D    : bits 47..39 ( 9 bit) — chỉ số trong Page 4th-level Dir
 *   PGD    : bits 56..48 ( 9 bit) — chỉ số trong Page Global Directory
 */
#define PAGING64_ADDR_OFFST_HIBIT 11
#define PAGING64_ADDR_OFFST_LOBIT 0

#define PAGING64_ADDR_PT_HIBIT  20
#define PAGING64_ADDR_PT_LOBIT  12
#define PAGING64_ADDR_PT_SHIFT  12  /* = LOBIT, dùng để dịch khi extract */

#define PAGING64_ADDR_PMD_HIBIT 29
#define PAGING64_ADDR_PMD_LOBIT 21

#define PAGING64_ADDR_PUD_HIBIT 38
#define PAGING64_ADDR_PUD_LOBIT 30

#define PAGING64_ADDR_P4D_HIBIT 47
#define PAGING64_ADDR_P4D_LOBIT 39

#define PAGING64_ADDR_PGD_HIBIT 56
#define PAGING64_ADDR_PGD_LOBIT 48

/* ── MASK CHO TỪNG CẤP ─────────────────────────────────────────────────────
 * Ví dụ: PAGING64_ADDR_PT_MASK = GENMASK64(20,12) = 0x001FF000
 *   → AND địa chỉ với mask này rồi dịch phải 12 → ra chỉ số PT
 */
#define PAGING64_ADDR_OFFST_MASK GENMASK64(PAGING_ADDR_OFFST_HIBIT, PAGING_ADDR_OFFST_LOBIT)
#define PAGING64_ADDR_PT_MASK    GENMASK64(PAGING64_ADDR_PT_HIBIT,   PAGING64_ADDR_PT_LOBIT)
#define PAGING64_ADDR_PMD_MASK   GENMASK64(PAGING64_ADDR_PMD_HIBIT,  PAGING64_ADDR_PMD_LOBIT)
#define PAGING64_ADDR_PUD_MASK   GENMASK64(PAGING64_ADDR_PUD_HIBIT,  PAGING64_ADDR_PUD_LOBIT)
#define PAGING64_ADDR_P4D_MASK   GENMASK64(PAGING64_ADDR_P4D_HIBIT,  PAGING64_ADDR_P4D_LOBIT)
#define PAGING64_ADDR_PGD_MASK   GENMASK64(PAGING64_ADDR_PGD_HIBIT,  PAGING64_ADDR_PGD_LOBIT)

/* ── MACRO TRÍCH XUẤT TỪNG CẤP TỪ ĐỊA CHỈ ẢO ─────────────────────────────
 * Dùng bitwise AND + shift phải để lấy 9 bit tương ứng.
 * Ví dụ: PAGING64_ADDR_PGD(0x0040000000000000)
 *   = (addr & PAGING64_ADDR_PGD_MASK) >> 48
 *   = (0x0040000000000000 & 0x01FF000000000000) >> 48
 *   = 0x0000000000000002  → PGD index = 2
 */
#define PAGING64_ADDR_OFFST(addr) GETVAL(addr, PAGING_ADDR_OFFST_MASK, PAGING_ADDR_OFFST_LOBIT)
#define PAGING64_ADDR_PT(addr)    ((addr & PAGING64_ADDR_PT_MASK)  >> PAGING64_ADDR_PT_LOBIT)
#define PAGING64_ADDR_PMD(addr)   ((addr & PAGING64_ADDR_PMD_MASK) >> PAGING64_ADDR_PMD_LOBIT)
#define PAGING64_ADDR_PUD(addr)   ((addr & PAGING64_ADDR_PUD_MASK) >> PAGING64_ADDR_PUD_LOBIT)
#define PAGING64_ADDR_P4D(addr)   ((addr & PAGING64_ADDR_P4D_MASK) >> PAGING64_ADDR_P4D_LOBIT)
#define PAGING64_ADDR_PGD(addr)   ((addr & PAGING64_ADDR_PGD_MASK) >> PAGING64_ADDR_PGD_LOBIT)

#endif
