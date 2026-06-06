/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * ============================================================
 * FILE: mm64.c
 * MỤC ĐÍCH: Thực thi toàn bộ Memory Management cho chế độ
 *           64-bit 5-level paging (MM64).
 *
 * CÁC NHÓM HÀM:
 *
 * 1. PAGE TABLE OPERATIONS (thao tác trên page table):
 *    init_pte           — khởi tạo 1 PTE với đầy đủ thông tin
 *    get_pd_from_address — phân tích địa chỉ ảo → 5 chỉ số cấp
 *    get_pd_from_pagenum — tương tự, nhưng nhận page number
 *    pte_set_fpn        — ghi FPN vào page table (page ở RAM)
 *    pte_set_swap       — ghi thông tin swap (page ở SWAP)
 *    pte_get_entry      — đọc PTE của một page
 *    pte_set_entry      — ghi trực tiếp PTE
 *
 * 2. VIRTUAL MEMORY MAPPING (ánh xạ vùng ảo vào vật lý):
 *    vmap_pgd_memset    — pattern-fill các page (debug/init)
 *    vmap_page_range    — ánh xạ danh sách frame vào virtual range
 *    alloc_pages_range  — cấp phát N frame từ RAM
 *    vm_map_ram         — ánh xạ virtual range vào RAM (wrapper)
 *    __swap_cp_page     — copy nội dung 1 frame (RAM ↔ SWAP)
 *
 * 3. MM INIT & HELPERS (khởi tạo và tiện ích):
 *    init_mm            — khởi tạo mm_struct cho process mới
 *    init_vm_rg         — tạo 1 vm_rg_struct mới
 *    enlist_vm_rg_node  — thêm vùng vào free region list
 *    enlist_pgn_node    — thêm page number vào FIFO page list
 *
 * 4. PRINT / DEBUG:
 *    print_list_fp, print_list_rg, print_list_vma,
 *    print_list_pgn, print_pgtbl
 *
 * LƯU Ý VỀ THIẾT KẾ:
 *   Mỗi mảng pgd[]/p4d[]/pud[]/pmd[]/pt[] có PAGING64_MAX_PGN
 *   phần tử, được cấp phát phẳng (calloc). Không có cây thực sự;
 *   mỗi cấp chỉ lưu chỉ số của cấp tiếp theo (fpn field = next_idx+1).
 *   Trang cuối cùng (PT) lưu FPN thật của RAM frame.
 * ============================================================
 */
#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if defined(MM64)

/*
 * init_pte: Khởi tạo giá trị PTE cho 1 entry trong page table.
 *
 * Có 2 trường hợp:
 *   A) Page ở RAM (swp=0):
 *      PTE = PRESENT | FPN[12:0]
 *      Ví dụ: page 5 map vào frame 7:
 *        init_pte(&mm->pt[5], 1, 7, 0, 0, 0, 0)
 *        → pte = 0x80000007 (PRESENT + FPN=7)
 *
 *   B) Page ở SWAP (swp=1):
 *      PTE = PRESENT | SWAPPED | SWPTYP[4:0] | SWPOFF[25:5]
 *      Ví dụ: page 5 ở swap device 0, offset 3:
 *        init_pte(&mm->pt[5], 1, 0, 0, 1, 0, 3)
 *        → pte = 0xC0000060 (PRESENT+SWAPPED + swpoff=3<<5)
 *
 * Với intermediate directory entries (pgd/p4d/pud/pmd):
 *   fpn = chỉ số cấp tiếp theo + 1 (để phân biệt với "chưa khởi tạo"=0)
 *   Ví dụ: pgd[0] trỏ đến p4d[2]: init_pte(&pgd[0], 1, 3, 0,0,0,0)
 *           → pgd[0] = 0x80000003 (FPN field = 3 = p4d_idx+1)
 */
int init_pte(addr_t *pte,
             int pre,       /* 1 = PRESENT */
             addr_t fpn,    /* frame number (hay next-level index+1) */
             int drt,       /* DIRTY flag */
             int swp,       /* 1 = SWAPPED */
             int swptyp,    /* swap device type */
             addr_t swpoff) /* offset trong swap */
{
  (void)drt; /* dirty chưa được dùng trong bản này */
  if (pre != 0) {
    if (swp == 0) {
      /* Page đang ở RAM — bắt buộc có fpn hợp lệ */
      if (fpn == 0)
        return -1;
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    } else {
      /* Page đang ở SWAP — lưu device type và offset */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }
  return 0;
}


/*
 * get_pd_from_address: Phân tích địa chỉ ảo 57-bit thành 5 chỉ số page directory.
 *
 * Ví dụ với addr = 0x0000_0000_0010_3A00 (một địa chỉ user-space):
 *   Binary: 0000_0000_0000_0000_0000_0000_0001_0000_0011_1010_0000_0000
 *   PGD  = bits 56:48 = 0
 *   P4D  = bits 47:39 = 0
 *   PUD  = bits 38:30 = 0
 *   PMD  = bits 29:21 = 0
 *   PT   = bits 20:12 = (0x0010_3 >> 12) & 0x1FF = 0x10 = 16
 *   OFFSET = bits 11:0 = 0xA00
 *
 * XỬ LÝ KERNEL SPACE:
 *   Nếu bit 56 = 1 (địa chỉ kernel canonical): bật thêm bit cao trong *pgd
 *   để phân biệt kernel space (địa chỉ âm trong 64-bit, bắt đầu từ 0xFF...)
 */
int get_pd_from_address(addr_t addr, addr_t *pgd, addr_t *p4d,
                        addr_t *pud, addr_t *pmd, addr_t *pt)
{
  *pgd = (addr & PAGING64_ADDR_PGD_MASK) >> PAGING64_ADDR_PGD_LOBIT;
  *p4d = (addr & PAGING64_ADDR_P4D_MASK) >> PAGING64_ADDR_P4D_LOBIT;
  *pud = (addr & PAGING64_ADDR_PUD_MASK) >> PAGING64_ADDR_PUD_LOBIT;
  *pmd = (addr & PAGING64_ADDR_PMD_MASK) >> PAGING64_ADDR_PMD_LOBIT;
  *pt  = (addr & PAGING64_ADDR_PT_MASK)  >> PAGING64_ADDR_PT_LOBIT;

  /* Địa chỉ kernel (bit 56 = 1) → đặt thêm bit cao để phân biệt với user */
  if (((addr >> PAGING64_ADDR_PGD_HIBIT) & 0x1) != 0)
    *pgd |= BIT_ULL(PAGING64_ADDR_PGD_HIBIT - PAGING64_ADDR_PGD_LOBIT);

  return 0;
}

/*
 * get_pd_from_pagenum: Phân tích page number (PGN) thành 5 chỉ số.
 *
 * PGN là địa chỉ ảo đã dịch phải bỏ phần offset, tức là:
 *   virtual_addr = pgn << 12 (shift lại để khôi phục địa chỉ ảo)
 *   rồi gọi get_pd_from_address.
 *
 * Ví dụ: pgn=16 (page thứ 16) → addr = 16 << 12 = 0x10000
 *   → PT index = 16, PGD/P4D/PUD/PMD = 0
 */
int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d,
                        addr_t *pud, addr_t *pmd, addr_t *pt)
{
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                             pgd, p4d, pud, pmd, pt);
}


/*
 * pte_set_swap: Đánh dấu page pgn đang bị swap ra ngoài (ở SWAP disk).
 *
 * Khi RAM đầy và cần cấp page mới:
 *   1. Chọn 1 page "nạn nhân" (victim) đang ở RAM
 *   2. Copy nội dung frame từ RAM → SWAP (__swap_cp_page)
 *   3. Gọi pte_set_swap để cập nhật PTE của page nạn nhân:
 *      → PRESENT=1, SWAPPED=1, SWPTYP=swptyp, SWPOFF=swpoff
 *   4. Frame RAM của nạn nhân được giải phóng để cấp cho page mới
 *
 * Khi process đọc/ghi page đã swap (page fault):
 *   → SWAPPED=1 → gọi swap_in để đọc lại từ SWAP về RAM
 *
 * LƯU Ý VỀ THIẾT KẾ: Chỉ init intermediate entries (pgd/p4d/pud/pmd)
 * nếu chúng CHƯA được set (để tránh overwrite khi nhiều trang cùng region).
 * PT entry được index trực tiếp bằng pgn (flat array design).
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  addr_t *pte;
  addr_t pgd = 0, p4d = 0, pud = 0, pmd = 0, pt = 0;

#ifdef MM64
  struct krnl_t *krnl = caller->krnl;
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  /* Chỉ khởi tạo intermediate entry nếu chưa có (PRESENT=0)
   * Tránh overwrite pointer cấp dưới khi nhiều page cùng chia sẻ PGD/P4D/PUD/PMD */
  if (!PAGING_PAGE_PRESENT(krnl->mm->pgd[pgd % PAGING64_MAX_PGN]))
    init_pte(&krnl->mm->pgd[pgd % PAGING64_MAX_PGN], 1, p4d + 1, 0, 0, 0, 0);
  if (!PAGING_PAGE_PRESENT(krnl->mm->p4d[p4d % PAGING64_MAX_PGN]))
    init_pte(&krnl->mm->p4d[p4d % PAGING64_MAX_PGN], 1, pud + 1, 0, 0, 0, 0);
  if (!PAGING_PAGE_PRESENT(krnl->mm->pud[pud % PAGING64_MAX_PGN]))
    init_pte(&krnl->mm->pud[pud % PAGING64_MAX_PGN], 1, pmd + 1, 0, 0, 0, 0);
  if (!PAGING_PAGE_PRESENT(krnl->mm->pmd[pmd % PAGING64_MAX_PGN]))
    init_pte(&krnl->mm->pmd[pmd % PAGING64_MAX_PGN], 1, pt  + 1, 0, 0, 0, 0);

  /* Truy cập PT entry trực tiếp bằng pgn — flat array, 1 slot = 1 page */
  pte = &krnl->mm->pt[pgn % PAGING64_MAX_PGN];
#else
  struct krnl_t *krnl = caller->krnl;
  pte = &krnl->mm->pgd[pgn];
#endif

  /* Ghi thông tin swap vào PTE */
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_fpn: Ghi FPN vào page table — đánh dấu page pgn đang ở RAM tại frame fpn.
 *
 * Quá trình cấp phát trang (alloc):
 *   1. alloc_pages_range → lấy N frame trống từ MEMPHY_get_freefp
 *   2. vmap_page_range   → với mỗi page, gọi pte_set_fpn(caller, pgn_i, fpn_i)
 *   3. Sau đó: đọc/ghi page sẽ tra PTE → lấy FPN → tính địa chỉ vật lý
 *
 * Quá trình swap-in (đọc page đang ở SWAP về RAM):
 *   1. Lấy frame trống (có thể phải swap out page khác trước)
 *   2. Copy dữ liệu từ SWAP vào RAM frame (__swap_cp_page)
 *   3. Gọi pte_set_fpn để cập nhật PTE: PRESENT=1, SWAPPED=0, FPN=fpn_mới
 *
 * LƯU Ý: Intermediate entries chỉ init khi chưa có (để tránh overwrite).
 *         PT entry dùng pgn trực tiếp (flat array).
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  addr_t *pte;
  addr_t pgd = 0, p4d = 0, pud = 0, pmd = 0, pt = 0;
  struct krnl_t *krnl = caller->krnl;

#ifdef MM64
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  /* Chỉ init intermediate entry nếu chưa có */
  if (!PAGING_PAGE_PRESENT(krnl->mm->pgd[pgd % PAGING64_MAX_PGN]))
    init_pte(&krnl->mm->pgd[pgd % PAGING64_MAX_PGN], 1, p4d + 1, 0, 0, 0, 0);
  if (!PAGING_PAGE_PRESENT(krnl->mm->p4d[p4d % PAGING64_MAX_PGN]))
    init_pte(&krnl->mm->p4d[p4d % PAGING64_MAX_PGN], 1, pud + 1, 0, 0, 0, 0);
  if (!PAGING_PAGE_PRESENT(krnl->mm->pud[pud % PAGING64_MAX_PGN]))
    init_pte(&krnl->mm->pud[pud % PAGING64_MAX_PGN], 1, pmd + 1, 0, 0, 0, 0);
  if (!PAGING_PAGE_PRESENT(krnl->mm->pmd[pmd % PAGING64_MAX_PGN]))
    init_pte(&krnl->mm->pmd[pmd % PAGING64_MAX_PGN], 1, pt  + 1, 0, 0, 0, 0);

  /* PT entry: truy cập phẳng theo pgn (mỗi pgn = 1 slot trong pt[]) */
  pte = &krnl->mm->pt[pgn % PAGING64_MAX_PGN];
#else
  pte = &krnl->mm->pgd[pgn];
#endif

  /* Ghi FPN và đánh dấu page đang ở RAM */
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}


/*
 * pte_get_entry: Tra cứu PTE của page pgn từ page table.
 *
 * Hàm này được gọi khi cần dịch địa chỉ ảo sang vật lý:
 *   virtual_addr = pgn * PAGESZ + offset
 *   pte = pte_get_entry(caller, pgn)
 *   if SWAPPED → cần swap in trước
 *   if PRESENT → fpn = PAGING_FPN(pte)
 *   physical_addr = fpn * PAGESZ + offset
 *
 * THIẾT KẾ FLAT ARRAY:
 *   Vì mỗi page có 1 slot riêng trong pt[] (index = pgn),
 *   ta truy cập trực tiếp pt[pgn % PAGING64_MAX_PGN].
 *   Không cần walk qua pgd/p4d/pud/pmd vì flat array đảm bảo
 *   không bị collision giữa các page.
 *
 *   (Nếu dùng hierarchical walk, khi nhiều page cùng PMD region
 *    thì pmd entry bị overwrite bởi page map sau cùng → bug.)
 */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  struct krnl_t *krnl = caller->krnl;
  /* Truy cập phẳng: mỗi pgn có đúng 1 slot trong pt[] */
  return (uint32_t)krnl->mm->pt[pgn % PAGING64_MAX_PGN];
}

/*
 * pte_set_entry: Ghi trực tiếp giá trị pte_val vào page table tại pgn.
 * Dùng khi cần overwrite toàn bộ PTE (ví dụ: xóa mapping khi free page).
 */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  struct krnl_t *krnl = caller->krnl;
#ifdef MM64
  krnl->mm->pt[pgn % PAGING64_MAX_PGN] = (addr_t)pte_val;
#else
  krnl->mm->pgd[pgn] = pte_val;
#endif
  return 0;
}


/*
 * vmap_pgd_memset: Điền pattern 0xDEADBEEF vào pgnum trang bắt đầu từ addr.
 * Dùng để debug — đánh dấu các trang "chưa được map thực sự"
 * hoặc để phát hiện lỗi truy cập vùng nhớ chưa khởi tạo.
 */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
  int pgit;
  addr_t pgn     = (addr >> PAGING64_ADDR_PT_SHIFT);
  addr_t pattern = 0xdeadbeefUL;

  for (pgit = 0; pgit < pgnum; pgit++)
    caller->krnl->mm->pt[(pgn + pgit) % PAGING64_MAX_PGN] = pattern;

  return 0;
}

/*
 * vmap_page_range: Ánh xạ danh sách frame vật lý (frames) vào pgnum trang ảo
 *                  bắt đầu từ địa chỉ addr.
 *
 * Ví dụ: alloc 3 trang tại addr=0x1000, frames=[FPN2, FPN5, FPN8]
 *   → pgn=1: pte_set_fpn(caller, 1, FPN2)  — trang 1 → frame 2
 *   → pgn=2: pte_set_fpn(caller, 2, FPN5)  — trang 2 → frame 5
 *   → pgn=3: pte_set_fpn(caller, 3, FPN8)  — trang 3 → frame 8
 *   → ret_rg = {rg_start=0x1000, rg_end=0x4000}
 *
 * Mỗi page được thêm vào fifo_pgn (FIFO list) để sau này dùng
 * cho thuật toán chọn victim (page replacement).
 *
 * Trả về 0 nếu thành công, -1 nếu frames là NULL.
 */
addr_t vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum,
                       struct framephy_struct *frames, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *fpit = frames;
  int pgit = 0;
  addr_t pgn = (addr >> PAGING64_ADDR_PT_SHIFT); /* page number của addr */

  /* Ghi lại vùng được map vào ret_rg */
  ret_rg->rg_start = addr;
  ret_rg->rg_end   = addr + (pgnum * PAGING_PAGESZ);
  ret_rg->vmaid    = 0;

  while (fpit != NULL && pgit < pgnum) {
    pte_set_fpn(caller, pgn + pgit, fpit->fpn);
    /* Thêm page vào FIFO list để tracking page replacement */
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn + pgit);
    fpit = fpit->fp_next;
    pgit++;
  }

  return (pgit == 0) ? -1 : 0;
}

/*
 * alloc_pages_range: Cấp phát req_pgnum frame vật lý từ RAM.
 *
 * Xây dựng linked list framephy_struct và trả về qua frm_lst.
 * Trả về 0 nếu thành công, -3000 nếu RAM không đủ frame.
 *
 * XỬ LÝ THIẾU FRAME (RAM đầy):
 *   Nếu giữa chừng MEMPHY_get_freefp thất bại:
 *   → Rollback: trả tất cả frame đã lấy trở lại free list
 *   → *frm_lst = NULL, return -3000
 *   Caller (__alloc) sẽ nhận -3000 và thực hiện swap-out
 *   để giải phóng frame, sau đó thử lại.
 *
 * Ví dụ: req_pgnum=3, RAM có frame [7, 12, 15]:
 *   → frm_lst = [fpn=7] → [fpn=12] → [fpn=15]
 *   → vmap_page_range sẽ dùng list này ánh xạ vào page table
 */
addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn;
  int pgit;
  struct framephy_struct *head = NULL;
  struct framephy_struct *tail = NULL;

  for (pgit = 0; pgit < req_pgnum; pgit++) {
    struct framephy_struct *newfp_str;

    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0) {
      /* RAM hết frame — rollback tất cả frame đã lấy */
      struct framephy_struct *it = head;
      while (it != NULL) {
        struct framephy_struct *next = it->fp_next;
        MEMPHY_put_freefp(caller->krnl->mram, it->fpn);
        free(it);
        it = next;
      }
      *frm_lst = NULL;
      return -3000; /* out-of-memory signal */
    }

    newfp_str = malloc(sizeof(struct framephy_struct));
    newfp_str->fpn     = fpn;
    newfp_str->fp_next = NULL;

    if (head == NULL)
      head = newfp_str;
    else
      tail->fp_next = newfp_str;
    tail = newfp_str;
  }

  *frm_lst = head;
  return 0;
}

/*
 * vm_map_ram: Wrapper ánh xạ virtual range [astart, aend) vào RAM.
 *
 * Hiện tại hàm này CHƯA HOÀN THIỆN (alloc_pages_range bị comment out).
 * Trong thiết kế đầy đủ:
 *   1. alloc_pages_range: lấy incpgnum frame từ RAM
 *   2. vmap_page_range  : ánh xạ frame list vào virtual addresses
 *
 * Cảnh báo của tác giả: Nếu yêu cầu nhiều frame hơn RAM có,
 * sẽ bị vòng lặp vô hạn (swap không có cơ chế kiểm soát trùng lặp).
 * → Trong bài tập này, KHÔNG test trường hợp RAM quá nhỏ.
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend,
                  addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
  (void)astart; (void)aend;

  /* alloc_pages_range chưa được gọi (bị comment) — hàm này stub */
  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;
  if (ret_alloc == -3000)
    return -1;

  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
  return 0;
}

/*
 * __swap_cp_page: Copy toàn bộ nội dung 1 frame giữa hai thiết bị nhớ.
 *
 * Dùng trong 2 tình huống:
 *   Swap OUT: mpsrc=RAM, srcfpn=frame nạn nhân, mpdst=SWAP, dstfpn=slot SWAP
 *             → copy RAM→SWAP trước khi giải phóng frame RAM
 *   Swap IN : mpsrc=SWAP, srcfpn=slot SWAP, mpdst=RAM, dstfpn=frame RAM mới
 *             → copy SWAP→RAM để tiến trình có thể truy cập lại
 *
 * Copy byte-by-byte: PAGING_PAGESZ lần (= 256 lần trong config này).
 * Tính địa chỉ vật lý: fpn * PAGING_PAGESZ + byte_offset
 */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++) {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;
    BYTE data;
    MEMPHY_read(mpsrc,  addrsrc, &data);
    MEMPHY_write(mpdst, addrdst,  data);
  }
  return 0;
}

/*
 * init_mm: Khởi tạo mm_struct — "bản đồ bộ nhớ" của 1 tiến trình mới.
 *
 * Được gọi từ loader.c khi tạo tiến trình (PCB mới):
 *   init_mm(caller->krnl->mm, caller)
 *
 * Sau khi init_mm:
 *   mm->pgd/p4d/pud/pmd/pt : 5 mảng page directory (512 entry mỗi cấp)
 *                              tất cả = 0 (calloc → zero-initialized)
 *   mm->mmap                : VMA đầu tiên (vma0), bắt đầu từ addr 0
 *   mm->symrgtbl            : bảng ánh xạ register→region, khởi tạo rỗng
 *   mm->fifo_pgn            : FIFO page list cho page replacement, rỗng
 *
 * VMA0 (vma0):
 *   vm_start = vm_end = sbrk = 0  → vùng trống, chưa có dữ liệu
 *   vm_freerg_list = [rg(0,0)]    → 1 vùng trống kích thước 0
 *   (sẽ được mở rộng bằng inc_vma_limit khi tiến trình alloc)
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  (void)caller;
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* Cấp phát 5 mảng page directory, mỗi mảng PAGING64_MAX_PGN entry, init về 0 */
  mm->pgd = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
  mm->p4d = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
  mm->pud = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
  mm->pmd = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
  mm->pt  = calloc(PAGING64_MAX_PGN, sizeof(addr_t));

  /* VMA0: vùng nhớ ảo đầu tiên, bắt đầu rỗng tại địa chỉ 0 */
  vma0->vm_id         = 0;
  vma0->vm_start      = 0;
  vma0->vm_end        = 0;
  vma0->sbrk          = 0;
  vma0->vm_freerg_list = NULL;
  vma0->vm_next       = NULL;
  vma0->vm_mm         = mm;

  /* Thêm 1 free region rỗng vào VMA0 */
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* mm->mmap trỏ đến VMA đầu tiên */
  mm->mmap     = vma0;
  mm->fifo_pgn = NULL;
  mm->kcpooltbl = NULL;

  /* Khởi tạo symbol region table (register → vùng nhớ) về rỗng.
   * mm_struct được cấp phát bằng malloc (không phải calloc) nên PHẢI
   * khởi tạo tường minh tất cả trường — kể cả mode_bit — để tránh
   * garbage value gây lỗi privilege check trong __read/__write. */
  int symidx;
  for (symidx = 0; symidx < PAGING_MAX_SYMTBL_SZ; symidx++) {
    mm->symrgtbl[symidx].vmaid    = 0;
    mm->symrgtbl[symidx].rg_start = 0;
    mm->symrgtbl[symidx].rg_end   = 0;
    mm->symrgtbl[symidx].mode_bit = 0; /* kernelmode mặc định cho slot chưa dùng */
    mm->symrgtbl[symidx].rg_next  = NULL;
  }

  return 0;
}

/* init_vm_rg: Tạo vm_rg_struct mới với [rg_start, rg_end).
 * Được dùng để thêm vùng trống mới vào vm_freerg_list của VMA. */
struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));
  rgnode->rg_start = rg_start;
  rgnode->rg_end   = rg_end;
  rgnode->rg_next  = NULL;
  return rgnode;
}

/* enlist_vm_rg_node: Thêm rgnode vào ĐẦU linked list *rglist (O(1)).
 * Dùng khi: thêm free region sau khi free page, hoặc init VMA. */
int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;
  return 0;
}

/* enlist_pgn_node: Thêm page number pgn vào ĐẦU FIFO list *plist (O(1)).
 * FIFO list được dùng để chọn victim page (trang cũ nhất → swap ra trước).
 * Mỗi khi map 1 trang mới, gọi enlist_pgn_node để theo dõi. */
int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));
  pnode->pgn     = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;
  return 0;
}

/* ── HÀM DEBUG: in danh sách frame trống ─────────────────── */
int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;
  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (fp != NULL) {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

/* ── HÀM DEBUG: in danh sách free region ─────────────────── */
int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;
  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL) {
    printf("rg[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

/* ── HÀM DEBUG: in danh sách VMA ────────────────────────── */
int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;
  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL) {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

/* ── HÀM DEBUG: in FIFO page number list ──────────────────── */
int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL) {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("\n");
  return 0;
}

/*
 * print_pgtbl: In địa chỉ heap của các mảng page directory.
 *
 * Hàm này in ra con trỏ (địa chỉ trên heap) của từng mảng
 * pgd[], p4d[], pud[], pmd[] — tức là nơi các mảng đó được
 * cấp phát bởi calloc() trong init_mm().
 *
 * Ý nghĩa output:
 *   PDG=7f3a001020b0 : địa chỉ heap của mảng mm->pgd (do calloc cấp)
 *   P4g, PUD, PMD   : tương tự cho p4d[], pud[], pmd[]
 *
 * LƯU Ý: Giá trị này thay đổi mỗi lần chạy (ASLR + heap randomization)
 * nên output sẽ khác nhau giữa các lần. Đây là hành vi mong đợi khi
 * bật chế độ in địa chỉ heap (thay vì PTE values).
 */
int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  (void)start;
  (void)end;

  printf("print_pgtbl:\n");
  /* In địa chỉ heap (uintptr_t) của từng mảng page directory */
  printf(" PDG=%llx P4g=%llx PUD=%llx PMD=%llx\n",
    (unsigned long long)(uintptr_t)caller->krnl->mm->pgd,
    (unsigned long long)(uintptr_t)caller->krnl->mm->p4d,
    (unsigned long long)(uintptr_t)caller->krnl->mm->pud,
    (unsigned long long)(uintptr_t)caller->krnl->mm->pmd);

  return 0;
}

#endif  /* MM64 */
