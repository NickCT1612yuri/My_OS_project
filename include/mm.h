/*
 * ============================================================
 * FILE: mm.h
 * MỤC ĐÍCH: Khai báo toàn bộ interface của Memory Management.
 *
 * File này định nghĩa:
 *   1. Tham số hệ thống (kích thước trang, RAM, SWAP)
 *   2. Cấu trúc bit của PTE (Page Table Entry)
 *   3. Macro đọc/ghi địa chỉ ảo và PTE
 *   4. Prototype tất cả hàm MM
 *
 * QUAN TRỌNG: Đây là file dùng cho chế độ 32-bit (mm.c).
 * Chế độ 64-bit dùng mm64.h + mm64.c, nhưng VẪN dùng
 * các macro PTE và hằng số định nghĩa ở đây.
 * ============================================================
 */
#ifndef MM_H

#include "common.h"
#include "bitops.h"

/* ── THAM SỐ HỆ THỐNG ───────────────────────────────────────
 * PAGING_CPU_BUS_WIDTH: độ rộng bus địa chỉ = 22 bit → 4MB không gian ảo
 * PAGING_PAGESZ       : kích thước 1 trang = 256 byte
 *                       (nhỏ để dễ test, thực tế thường 4096 byte)
 * PAGING_MEMRAMSZ     : kích thước RAM = 2^21 = 2MB
 * PAGING_MEMSWPSZ     : kích thước SWAP = 2^29 = 512MB
 * PAGING_MAX_PGN      : số trang ảo tối đa = 4MB / 256 = 16384 trang
 * PAGING_PAGE_ALIGNSZ : làm tròn kích thước lên bội số của PAGESZ
 *                       Ví dụ: 300 byte → 512 byte (2 trang × 256)
 */
#define PAGING_CPU_BUS_WIDTH 22
#define PAGING_PAGESZ  256
#define PAGING_MEMRAMSZ BIT(21)
#define PAGING_PAGE_ALIGNSZ(sz) (DIV_ROUND_UP(sz,PAGING_PAGESZ)*PAGING_PAGESZ)
#define PAGING_MEMSWPSZ BIT(29)
#define PAGING_SWPFPN_OFFSET 5
#define PAGING_MAX_PGN  (DIV_ROUND_UP(BIT(PAGING_CPU_BUS_WIDTH),PAGING_PAGESZ))
#define PAGING_SBRK_INIT_SZ PAGING_PAGESZ

/* ── CẤU TRÚC BIT CỦA PTE ───────────────────────────────────
 * PTE (Page Table Entry) là một số 32-bit (addr_t trong 32-bit mode)
 * với từng nhóm bit mang ý nghĩa khác nhau:
 *
 * Bit layout của PTE (32-bit):
 * ┌────┬────┬────┬────┬────────────┬────────────────┬────────────┐
 * │ 31 │ 30 │ 29 │ 28 │  27..15    │    14..13      │   12..0    │
 * ├────┼────┼────┼────┼────────────┼────────────────┼────────────┤
 * │ P  │ S  │ R  │ D  │  USRNUM    │    EMPTY       │    FPN     │
 * └────┴────┴────┴────┴────────────┴────────────────┴────────────┘
 *   P  = PRESENT : 1 nếu trang đang có trong RAM
 *   S  = SWAPPED : 1 nếu trang đang ở SWAP (P và S có thể cùng = 1)
 *   R  = RESERVE : dự phòng
 *   D  = DIRTY   : 1 nếu trang đã bị ghi (cần ghi lại khi swap out)
 *   USRNUM       : số hiệu người dùng (bits 27:15, không dùng nhiều)
 *   EMPTY        : các bit dự phòng
 *   FPN          : Frame Page Number — số hiệu frame vật lý (bits 12:0)
 *                  Khi SWAPPED=1: FPN field không dùng, thay vào đó:
 *                    bits 4:0  = SWPTYP (swap type / swap device ID)
 *                    bits 25:5 = SWPOFF (swap offset / frame trong SWAP)
 */
#define PAGING_PTE_PRESENT_MASK BIT(31)
#define PAGING_PTE_SWAPPED_MASK BIT(30)
#define PAGING_PTE_RESERVE_MASK BIT(29)
#define PAGING_PTE_DIRTY_MASK   BIT(28)
#define PAGING_PTE_EMPTY01_MASK BIT(14)
#define PAGING_PTE_EMPTY02_MASK BIT(13)

/* Kiểm tra và đặt bit PRESENT */
#define PAGING_PTE_SET_PRESENT(pte) (pte = pte | PAGING_PTE_PRESENT_MASK)
#define PAGING_PAGE_PRESENT(pte)    (pte & PAGING_PTE_PRESENT_MASK)

/* ── VỊ TRÍ CÁC TRƯỜNG TRONG PTE ──────────────────────────
 * _LOBIT: bit thấp nhất của trường
 * _HIBIT: bit cao nhất của trường
 *
 * FPN    : bits 12:0  → 13 bit → tối đa 8192 frames (2^13)
 * USRNUM : bits 27:15 → 13 bit → thông tin user (ít dùng)
 * SWPTYP : bits  4:0  → 5 bit  → tối đa 32 loại swap device
 * SWPOFF : bits 25:5  → 21 bit → offset trong SWAP (tối đa 2^21 frames)
 */
#define PAGING_PTE_USRNUM_LOBIT 15
#define PAGING_PTE_USRNUM_HIBIT 27
#define PAGING_PTE_FPN_LOBIT    0
#define PAGING_PTE_FPN_HIBIT    12
#define PAGING_PTE_SWPTYP_LOBIT 0
#define PAGING_PTE_SWPTYP_HIBIT 4
#define PAGING_PTE_SWPOFF_LOBIT 5
#define PAGING_PTE_SWPOFF_HIBIT 25

/* Mask cho từng trường (dùng GENMASK từ bitops.h) */
#define PAGING_PTE_USRNUM_MASK GENMASK(PAGING_PTE_USRNUM_HIBIT, PAGING_PTE_USRNUM_LOBIT)
#define PAGING_PTE_FPN_MASK    GENMASK(PAGING_PTE_FPN_HIBIT,    PAGING_PTE_FPN_LOBIT)
#define PAGING_PTE_SWPTYP_MASK GENMASK(PAGING_PTE_SWPTYP_HIBIT, PAGING_PTE_SWPTYP_LOBIT)
#define PAGING_PTE_SWPOFF_MASK GENMASK(PAGING_PTE_SWPOFF_HIBIT, PAGING_PTE_SWPOFF_LOBIT)

/* Trích xuất từng trường từ PTE */
#define PAGING_PTE_OFFST(pte) GETVAL(pte, PAGING_OFFST_MASK,      PAGING_ADDR_OFFST_LOBIT)
#define PAGING_PTE_PGN(pte)   GETVAL(pte, PAGING_PGN_MASK,        PAGING_ADDR_PGN_LOBIT)
#define PAGING_PTE_FPN(pte)   GETVAL(pte, PAGING_PTE_FPN_MASK,    PAGING_PTE_FPN_LOBIT)
#define PAGING_PTE_SWP(pte)   GETVAL(pte, PAGING_PTE_SWPOFF_MASK, PAGING_SWPFPN_OFFSET)

/* ── PHÂN TÍCH ĐỊA CHỈ ẢO (Virtual Address Decomposition) ──
 * Địa chỉ ảo 22-bit được chia thành 2 phần:
 *
 *  ┌─────────────────────┬────────────────┐
 *  │   Page Number (PGN) │  Offset        │
 *  │   bits 21..8        │  bits 7..0     │
 *  └─────────────────────┴────────────────┘
 *       14 bits                8 bits
 *
 * PAGING_PAGESZ = 256 = 2^8 → NBITS(256) = 9 → OFFST_HIBIT = 8-1 = 7
 * → offset dùng bits 7:0 (8 bit = 256 giá trị)
 * → PGN dùng bits 21:8 (14 bit = 16384 trang)
 */
#define PAGING_ADDR_OFFST_LOBIT 0
#define PAGING_ADDR_OFFST_HIBIT (NBITS(PAGING_PAGESZ) - 1)
#define PAGING_ADDR_PGN_LOBIT   NBITS(PAGING_PAGESZ)
#define PAGING_ADDR_PGN_HIBIT   (PAGING_CPU_BUS_WIDTH - 1)

/* Địa chỉ vật lý: FPN + offset (tương tự nhưng trong RAM) */
#define PAGING_ADDR_FPN_LOBIT NBITS(PAGING_PAGESZ)
#define PAGING_ADDR_FPN_HIBIT (NBITS(PAGING_MEMRAMSZ) - 1)

/* Địa chỉ trong SWAP: FPN trong SWAP + offset */
#define PAGING_SWP_LOBIT NBITS(PAGING_PAGESZ)
#define PAGING_SWP_HIBIT (NBITS(PAGING_MEMSWPSZ) - 1)
#define PAGING_SWP(pte)  ((pte & PAGING_PTE_SWPOFF_MASK) >> PAGING_SWPFPN_OFFSET)

/* ── MACRO ĐỌC/GHI GIÁ TRỊ VÀO BIẾN THEO MASK ──────────────
 * SETBIT(v, mask) : bật các bit theo mask trong v
 * CLRBIT(v, mask) : tắt các bit theo mask trong v
 * SETVAL(v, value, mask, offst):
 *   - Xóa vùng mask trong v
 *   - Gán value dịch trái offst bit vào đúng vị trí
 *   Ví dụ: SETVAL(pte, fpn=5, PAGING_PTE_FPN_MASK, 0)
 *          → pte = (pte & ~0x1FFF) | (5 & 0x1FFF)
 * GETVAL(v, mask, offst):
 *   - Trích giá trị trong vùng mask, dịch phải về 0
 *   Ví dụ: GETVAL(pte, PAGING_PTE_FPN_MASK, 0) → lấy FPN
 */
#define SETBIT(v, mask)              (v = v | mask)
#define CLRBIT(v, mask)              (v = v & ~mask)
#define SETVAL(v, value, mask, offst) (v = (v & ~mask) | ((value << offst) & mask))
#define GETVAL(v, mask, offst)        ((v & mask) >> offst)

/* Mask cho toàn bộ trường offset, PGN, FPN, SWP */
#define PAGING_OFFST_MASK GENMASK(PAGING_ADDR_OFFST_HIBIT, PAGING_ADDR_OFFST_LOBIT)
#define PAGING_PGN_MASK   GENMASK(PAGING_ADDR_PGN_HIBIT,   PAGING_ADDR_PGN_LOBIT)
#define PAGING_FPN_MASK   GENMASK(PAGING_ADDR_FPN_HIBIT,   PAGING_ADDR_FPN_LOBIT)
#define PAGING_SWP_MASK   GENMASK(PAGING_SWP_HIBIT,        PAGING_SWP_LOBIT)

/* Macro trích xuất từng phần của địa chỉ ảo/vật lý */
#define PAGING_OFFST(x) GETVAL(x, PAGING_OFFST_MASK, PAGING_ADDR_OFFST_LOBIT)
#define PAGING_PGN(x)   GETVAL(x, PAGING_PGN_MASK,   PAGING_ADDR_PGN_LOBIT)
#define PAGING_FPN(x)   GETVAL(x, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT)

/* ── KIỂM TRA VÙNG NHỚ ──────────────────────────────────────
 * INCLUDE(x1,x2, y1,y2): vùng [y1,y2) nằm HOÀN TOÀN trong [x1,x2)
 * OVERLAP(x1,x2, y1,y2): hai vùng CÓ GIAO NHAU (partial hay total)
 */
#define INCLUDE(x1, x2, y1, y2) ((x1) <= (y1) && (y2) <= (x2))
#define OVERLAP(x1, x2, y1, y2) (!((x2) <= (y1) || (y2) <= (x1)))

/* ── PROTOTYPE: QUẢN LÝ VÙNG NHỚ ẢO (VMA) ───────────────────────────────── */

/* init_vm_rg      : tạo và trả về một vm_rg_struct mới [rg_start, rg_end) */
struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end);
/* enlist_vm_rg_node: thêm rgnode vào đầu linked list *rglist */
int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode);
/* enlist_pgn_node  : thêm một page number vào linked list *pgnlist */
int enlist_pgn_node(struct pgn_t **pgnlist, addr_t pgn);
/* vmap_pgd_memset  : xóa pgnum trang bắt đầu từ addr (zeroing) */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum);
/* vmap_page_range  : ánh xạ danh sách frame vật lý (frames) vào incpgnum trang
 *                    bắt đầu từ địa chỉ ảo addr, trả về địa chỉ kết thúc */
addr_t vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum,
                       struct framephy_struct *frames, struct vm_rg_struct *ret_rg);
/* vm_map_range     : ánh xạ vùng [astart, aend) vào [mapstart, ...) trong VMA */
addr_t vm_map_range(struct pcb_t *caller, addr_t astart, addr_t aend,
                    addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg);
/* vm_map_kernel    : ánh xạ vùng nhớ cho kernel (tương tự vm_map_range) */
addr_t vm_map_kernel(struct pcb_t *caller, addr_t astart, addr_t aend,
                     addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg);
/* vm_map_ram       : ánh xạ virtual range vào RAM (cấp phát frame vật lý) */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend,
                  addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg);
/* alloc_pages_range: cấp phát incpgnum frame vật lý từ RAM, trả về qua frm_lst */
addr_t alloc_pages_range(struct pcb_t *caller, int incpgnum, struct framephy_struct **frm_lst);
/* __swap_cp_page   : copy 1 frame từ mpsrc:srcfpn sang mpdst:dstfpn
 *                    (dùng khi swap in/out) */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn);

/* ── PROTOTYPE: KMEM (Kernel Memory Allocator) ───────────────────────────── */

/* __kmalloc         : cấp phát vùng nhớ kernel kích thước size trong vmaid/rgid */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr);
/* __kmem_cache_alloc: cấp phát từ cache pool (tối ưu cho object nhỏ cùng kích thước) */
addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid,
                           int cache_pool_id, addr_t *alloc_addr);

/* ── PROTOTYPE: ĐỌC/GHI BỘ NHỚ USER VÀ KERNEL ──────────────────────────── */

/* __read_user_mem  : đọc 1 byte tại offset trong vùng vmaid/rgid của tiến trình */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data);
/* __write_user_mem : ghi 1 byte vào offset trong vùng vmaid/rgid */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value);
/* __read_kernel_mem / __write_kernel_mem: tương tự nhưng cho kernel space */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data);
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value);

/* ── PROTOTYPE: PAGE TABLE (PTE OPERATIONS) ─────────────────────────────── */

/* get_pd_from_address: phân tích địa chỉ ảo addr thành 5 chỉ số page directory
 *   (pgd, p4d, pud, pmd, pt) — dùng cho 64-bit 5-level paging (mm64) */
int get_pd_from_address(addr_t addr, addr_t *pgd, addr_t *p4d,
                        addr_t *pud, addr_t *pmd, addr_t *pt);
/* get_pd_from_pagenum: tương tự nhưng nhận page number (pgn) thay vì địa chỉ */
int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d,
                        addr_t *pud, addr_t *pmd, addr_t *pt);

/* pte_set_fpn  : ghi FPN (frame vật lý) vào page table tại page pgn
 *                → trang pgn hiện đang ở RAM tại frame fpn */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn);
/* pte_set_swap : ghi thông tin swap vào page table tại page pgn
 *                → trang pgn hiện đang ở SWAP device swptyp tại offset swpoff */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff);
/* pte_get_entry: lấy giá trị PTE của page pgn */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn);
/* pte_set_entry: ghi trực tiếp giá trị pte_val vào page table tại pgn */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val);

/* init_pte: khởi tạo 1 PTE với đầy đủ thông tin:
 *   pre    : 1 = PRESENT (trang có trong RAM)
 *   fpn    : frame number hoặc chỉ số directory cấp dưới
 *   drt    : 1 = DIRTY (trang đã được ghi)
 *   swp    : 1 = SWAPPED (trang đang ở SWAP)
 *   swptyp : loại swap device
 *   swpoff : offset trong SWAP */
int init_pte(addr_t *pte, int pre, addr_t fpn, int drt,
             int swp, int swptyp, addr_t swpoff);

/* ── PROTOTYPE: SYSCALL-LEVEL MM (dùng bởi libmem.c) ───────────────────── */

/* __alloc: cấp phát vùng nhớ kích thước size trong VMA vmaid, region rgid
 *          Trả về địa chỉ bắt đầu qua alloc_addr */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr);
/* __free : giải phóng vùng nhớ tại region rgid của VMA vmaid */
int __free(struct pcb_t *caller, int vmaid, int rgid);
/* __read : đọc 1 byte tại (vmaid, rgid, offset) */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data);
/* __write: ghi 1 byte tại (vmaid, rgid, offset) */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value);
/* init_mm: khởi tạo mm_struct cho tiến trình caller (tạo VMA đầu tiên, ...) */
int init_mm(struct mm_struct *mm, struct pcb_t *caller);

/* ── PROTOTYPE: GIAO DIỆN CPU-LEVEL (gọi từ cpu.c) ─────────────────────── */

/* pgalloc    : lệnh ALLOC từ CPU → gọi __alloc với vmaid=0 */
int pgalloc(struct pcb_t *proc, uint32_t size, uint32_t reg_index);
/* pgfree_data: lệnh FREE từ CPU */
int pgfree_data(struct pcb_t *proc, uint32_t reg_index);
/* pgread     : lệnh READ từ CPU — đọc dữ liệu từ [source] + offset → destination reg */
int pgread(struct pcb_t *proc, uint32_t source, addr_t offset, uint32_t destination);
/* pgwrite    : lệnh WRITE từ CPU — ghi data vào [destination] + offset */
int pgwrite(struct pcb_t *proc, BYTE data, uint32_t destination, addr_t offset);

/* ── PROTOTYPE: HÀM NỘI BỘ VM ──────────────────────────────────────────── */

/* get_symrg_byid        : lấy vm_rg_struct của region rgid trong mm */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid);
/* validate_overlap_vm_area: kiểm tra [vmastart, vmaend) có đè lên VMA khác không */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid,
                              addr_t vmastart, addr_t vmaend);
/* get_free_vmrg_area    : tìm vùng trống kích thước size trong VMA vmaid */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size,
                        struct vm_rg_struct *newrg);
/* inc_vma_limit         : mở rộng VMA vmaid thêm inc_sz byte (giống sbrk) */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz);
/* find_victim_page      : chọn 1 trang để swap out (chiến lược đuổi trang) */
int find_victim_page(struct mm_struct *mm, addr_t *pgn);
/* get_vma_by_num        : lấy VMA theo vmaid */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid);

/* ── PROTOTYPE: THIẾT BỊ VẬT LÝ (mm-memphy.c) ─────────────────────────── */
int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *fpn);
int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn);
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value);
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data);
int MEMPHY_dump(struct memphy_struct *mp);
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg);

/* ── PROTOTYPE: IN DANH SÁCH (DEBUG) ───────────────────────────────────── */
int print_list_fp(struct framephy_struct *fp);  /* in free frame list */
int print_list_rg(struct vm_rg_struct *rg);     /* in free region list */
int print_list_vma(struct vm_area_struct *rg);  /* in danh sách VMA */
int print_list_pgn(struct pgn_t *ip);           /* in danh sách page number */
int print_pgtbl(struct pcb_t *ip, addr_t start, addr_t end); /* in page table */

#endif
