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
 * FILE: mm-vm.c
 * MỤC ĐÍCH: Quản lý Virtual Memory Area (VMA) của tiến trình.
 *
 * Đây là lớp "trên" page table — làm việc với vùng nhớ ảo
 * thay vì frame vật lý:
 *
 * CÁC HÀM CHÍNH:
 *   get_vma_by_num         — tra cứu VMA theo ID
 *   get_vm_area_node_at_brk— tạo vùng mới tại sbrk (heap pointer)
 *   validate_overlap_vm_area — kiểm tra không đè lên VMA khác
 *   inc_vma_limit          — mở rộng VMA (giống sbrk() trong Linux)
 *   __mm_swap_page         — helper copy 1 frame RAM → SWAP
 *
 * MÔ HÌNH VMA:
 *   mm->mmap → vma0 → vma1 → vma2 → NULL
 *   Mỗi VMA có:
 *     vm_start / vm_end : phạm vi địa chỉ ảo
 *     sbrk              : "break pointer" — điểm kết thúc hiện tại của heap
 *     vm_freerg_list    : linked list các vùng trống trong VMA
 *
 * HEAP GROWTH (mở rộng heap):
 *   Khi alloc() không tìm được vùng trống trong VMA:
 *     1. inc_vma_limit: tăng vm_end thêm inc_sz
 *     2. Cấp phát frame mới từ RAM cho phần mới
 *     3. Vùng mới được thêm vào freerg_list để dùng
 * ============================================================
 */
#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*
 * get_vma_by_num: Tra cứu vm_area_struct theo vmaid.
 * Duyệt linked list mm->mmap cho đến khi tìm VMA có vm_id == vmaid.
 * Trả về NULL nếu không tìm thấy.
 *
 * Trong bài này thường chỉ có vmaid=0 (1 VMA duy nhất cho user heap).
 *
 * LỖI ĐÃ SỬA: Phiên bản cũ kiểm tra pvma==NULL sau khi advance pvma,
 * dẫn đến NULL deref khi vmaid không tồn tại trong list.
 * Phiên bản mới: kiểm tra pvma!=NULL trước khi đọc vm_id.
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma = mm->mmap;

  if (mm->mmap == NULL)
    return NULL;

  while (pvma != NULL && pvma->vm_id < vmaid)
    pvma = pvma->vm_next;

  /* Trả về VMA nếu đúng vmaid, hoặc NULL nếu không tìm thấy */
  if (pvma != NULL && pvma->vm_id == vmaid)
    return pvma;
  return NULL;
}

/*
 * __mm_swap_page: Copy nội dung 1 frame từ RAM vào SWAP.
 * vicfpn = FPN của frame nạn nhân trong RAM
 * swpfpn = FPN của slot đích trong SWAP
 * Dùng bởi pg_getpage khi RAM đầy và cần chỗ cho page mới.
 */
int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn, addr_t swpfpn)
{
  __swap_cp_page(caller->krnl->mram, vicfpn,
                 caller->krnl->active_mswp, swpfpn);
  return 0;
}

/*
 * get_vm_area_node_at_brk: Tạo vm_rg_struct mới tại vị trí sbrk hiện tại.
 *
 * Khi cần mở rộng heap:
 *   newrg->rg_start = cur_vma->sbrk  (vị trí kế tiếp của heap)
 *   newrg->rg_end   = sbrk + size    (kích thước yêu cầu)
 *
 * "sbrk" = "set break" — giống system call sbrk() trong Linux.
 * alignedsz: kích thước đã làm tròn theo trang (không dùng trong bản này).
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid,
                                              addr_t size, addr_t alignedsz)
{
  (void)alignedsz;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
  /* Bắt đầu từ vị trí sbrk hiện tại */
  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end   = newrg->rg_start + size;
  return newrg;
}

/*
 * validate_overlap_vm_area: Kiểm tra vùng [vmastart, vmaend) không đè lên VMA nào khác.
 *
 * Duyệt tất cả VMA trong mm->mmap:
 *   Nếu vùng mới OVERLAP với VMA khác (không phải chính vmaid) → return -1
 *
 * Dùng trước khi mở rộng VMA để tránh cấp vùng ảo đã được dùng bởi VMA khác.
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid,
                              addr_t vmastart, addr_t vmaend)
{
  if (vmastart >= vmaend)
    return -1;

  struct vm_area_struct *vma = caller->krnl->mm->mmap;
  if (vma == NULL)
    return -1;

  struct vm_area_struct *cur_area = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_area == NULL)
    return -1;

  while (vma != NULL) {
    if (vma != cur_area &&
        OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end))
      return -1; /* có overlap → không hợp lệ */
    vma = vma->vm_next;
  }
  return 0;
}

/*
 * inc_vma_limit: Mở rộng VMA vmaid thêm inc_sz byte.
 *
 * Quy trình (giống sbrk() trong OS thực):
 *   1. Làm tròn inc_sz lên bội của PAGESZ (inc_amt)
 *   2. Tính số trang cần thêm (incnumpage)
 *   3. Gọi get_vm_area_node_at_brk: lấy vùng mới tại sbrk
 *   4. validate_overlap: đảm bảo không đè VMA khác
 *   5. Cập nhật vm_end += inc_amt, sbrk = newrg->rg_end
 *   6. vm_map_ram: cấp phát frame vật lý và ánh xạ vào page table
 *
 * Được gọi bởi __alloc (trong libmem.c) qua syscall 17 (SYSMEM_INC_OP).
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_vma == NULL)
    return -1;

  addr_t inc_amt    = PAGING_PAGE_ALIGNSZ(inc_sz); /* làm tròn lên bội PAGESZ */
  int incnumpage    = inc_amt / PAGING_PAGESZ;      /* số trang cần cấp */
  addr_t old_end    = cur_vma->vm_end;

  /* Tạo vùng mới tại sbrk */
  struct vm_rg_struct *newrg = get_vm_area_node_at_brk(caller, vmaid,
                                                        inc_sz, inc_amt);
  if (newrg == NULL)
    return -1;

  /* Kiểm tra không đè lên VMA khác */
  if (validate_overlap_vm_area(caller, vmaid, old_end, old_end + inc_amt) < 0) {
    free(newrg);
    return -1;
  }

  /* Mở rộng VMA: cập nhật vm_end và sbrk */
  cur_vma->vm_end = old_end + inc_amt;
  cur_vma->sbrk   = newrg->rg_end;

  /* Ánh xạ vùng ảo mới vào RAM (cấp frame vật lý) */
  if (vm_map_ram(caller, newrg->rg_start, newrg->rg_end,
                 old_end, incnumpage, newrg) < 0) {
    free(newrg);
    return -1;
  }

  free(newrg);
  return 0;
}
