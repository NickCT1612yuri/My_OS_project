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
 * FILE: libmem.c
 * MỤC ĐÍCH: "Standard Library" của OS simulator — cung cấp
 *           API cấp cao cho CPU để thực hiện các thao tác:
 *           ALLOC, FREE, READ, WRITE trên bộ nhớ paging.
 *
 * LUỒNG ĐIỀU KHIỂN (flow):
 *
 *   CPU instruction          libmem.c            mm64.c / syscall.c
 *   ─────────────────────────────────────────────────────────────
 *   pgalloc(size, reg)  →  liballoc           →  __alloc
 *   pgfree_data(reg)    →  libfree            →  __free
 *   pgread(src,off,dst) →  libread            →  __read → pg_getval → pg_getpage
 *   pgwrite(dst,off,dat)→  libwrite           →  __write → pg_setval → pg_getpage
 *
 * pg_getpage (trái tim của libmem):
 *   Cho page pgn, đảm bảo nó đang ở RAM.
 *   Nếu không: swap in (hoặc zero-fill nếu chưa dùng).
 *   Trả về FPN để tính địa chỉ vật lý.
 *
 * mmvm_lock:
 *   Mutex bảo vệ toàn bộ thao tác MM khỏi race condition giữa
 *   nhiều CPU thread. Mỗi hàm __alloc/__free/__read/__write đều
 *   lock/unlock trước khi thao tác vào mm_struct.
 *
 * THUẬT TOÁN PAGE REPLACEMENT (khi RAM đầy):
 *   Dùng FIFO (First-In First-Out) qua fifo_pgn list:
 *   - Mỗi page mới được thêm vào ĐẦU list (enlist_pgn_node)
 *   - Victim là page ở CUỐI list (find_victim_page)
 *   → page nào map lâu nhất bị swap ra trước
 * ============================================================
 */
#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

/* Mutex bảo vệ toàn bộ thao tác MM — tránh race condition giữa CPU threads */
static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * enlist_vm_freerg_list: Thêm vùng trống rg_elmt vào đầu vm_freerg_list của VMA0.
 * Được gọi bởi __free khi giải phóng vùng nhớ.
 * Chỉ thêm nếu rg_elmt có kích thước > 0 (rg_end > rg_start).
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;
  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;
  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;
  mm->mmap->vm_freerg_list = rg_elmt;
  return 0;
}

/*
 * get_symrg_byid: Tra cứu vm_rg_struct từ symbol table theo rgid.
 * symrgtbl[rgid] lưu vùng [rg_start, rg_end) đã được cấp cho "biến" rgid.
 * Ví dụ: lệnh READ(0, offset, 1) → rgid=0 → tra symrgtbl[0] → lấy địa chỉ ảo.
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;
  return &mm->symrgtbl[rgid];
}

/*
 * __alloc: Cấp phát vùng nhớ ảo kích thước size trong VMA vmaid, region rgid.
 *
 * LUỒNG XỬ LÝ:
 *   Bước 1: Tìm vùng trống sẵn có (get_free_vmrg_area):
 *     → Duyệt vm_freerg_list của VMA, tìm vùng >= size
 *     → Nếu tìm được: ghi vào symrgtbl[rgid], *alloc_addr = rg_start, return 0
 *
 *   Bước 2: Không có vùng trống → mở rộng heap qua SYSCALL 17 (SYSMEM_INC_OP):
 *     → Tính inc_sz = số trang cần thêm + 1 (dự phòng)
 *     → _syscall → sys_memmap → inc_vma_limit → cấp frame RAM mới
 *     → Sau syscall: ghi vào symrgtbl[rgid], *alloc_addr = old_sbrk
 *
 * symrgtbl[rgid]: bảng ánh xạ "register/variable ID → vùng nhớ ảo".
 * Khi process sau đó READ(rgid, offset) → tra symrgtbl[rgid] → lấy địa chỉ ảo.
 *
 * mmvm_lock: mutex để tránh race condition khi nhiều CPU thread cùng alloc.
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  int inc_sz = 0;

  /* Bước 1: Thử tìm vùng trống trong free list của VMA */
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0) {
    caller->krnl->mm->symrgtbl[rgid].rg_start  = rgnode.rg_start;
    caller->krnl->mm->symrgtbl[rgid].rg_end    = rgnode.rg_end;
    caller->krnl->mm->symrgtbl[rgid].mode_bit  = 1; /* usermode */
    *alloc_addr = rgnode.rg_start;
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* Bước 2: Không đủ chỗ → yêu cầu mở rộng heap qua syscall */
#ifdef MM64
  inc_sz = (uint32_t)(size / (int)PAGING64_PAGESZ) + 1;
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
  inc_sz = inc_sz + 1; /* dự phòng thêm 1 trang */

  int old_sbrk = cur_vma->sbrk;

  /* Syscall 17 (sys_memmap) với opcode SYSMEM_INC_OP:
   * a1=opcode, a2=vmaid, a3=kích thước cần mở rộng */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
#ifdef MM64
  regs.a3 = size;
#else
  regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif
  _syscall(caller->krnl, caller->pid, 17, &regs);

  /* Sau khi mở rộng: vùng mới bắt đầu tại old_sbrk */
  caller->krnl->mm->symrgtbl[rgid].rg_start  = old_sbrk;
  caller->krnl->mm->symrgtbl[rgid].rg_end    = old_sbrk + size;
  caller->krnl->mm->symrgtbl[rgid].mode_bit  = 1; /* usermode */
  *alloc_addr = old_sbrk;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*
 * __free: Giải phóng vùng nhớ tại region rgid của VMA vmaid.
 *
 * Quy trình:
 *   1. Tra symrgtbl[rgid] để lấy [rg_start, rg_end)
 *   2. Tạo freerg_node mới với thông tin vùng đó
 *   3. Thêm freerg_node vào vm_freerg_list (trả về pool trống)
 *   4. Xóa symrgtbl[rgid] (rg_start = rg_end = 0)
 *
 * Sau khi free:
 *   Vùng nhớ này sẽ được tái sử dụng khi có alloc() tiếp theo
 *   (get_free_vmrg_area sẽ tìm thấy nó trong freerg_list).
 *
 * LƯU Ý: Không giải phóng frame vật lý (FPN trong RAM).
 * Frame chỉ được thu hồi khi process kết thúc (free_pcb_memph).
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  (void)vmaid;
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *rgnode = get_symrg_byid(caller->krnl->mm, rgid);

  /* Kiểm tra vùng có tồn tại không */
  if (rgnode->rg_start == 0 && rgnode->rg_end == 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* Tạo node mới để thêm vào free list */
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end   = rgnode->rg_end;
  freerg_node->rg_next  = NULL;

  /* Xóa entry trong symbol table: reset tất cả trường về 0/NULL.
   * Bao gồm mode_bit = 0, tránh garbage value khi slot này được tái dùng. */
  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->mode_bit = 0;
  rgnode->rg_next  = NULL;

  /* Trả vùng vừa free về danh sách trống */
  enlist_vm_freerg_list(caller->krnl->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*
 * liballoc: Wrapper CPU-level cho __alloc.
 * Gọi từ pgalloc() trong cpu.c khi CPU thực hiện lệnh ALLOC.
 * Luôn dùng vmaid=0 (VMA mặc định của user process).
 * In debug output nếu IODUMP được bật.
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
#ifdef IODUMP
  if (val == 0) printf("liballoc:178\n");
  else          printf("liballoc:178 failed\n");
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif
  return val;
}

/*
 * libfree: Wrapper CPU-level cho __free.
 * Gọi từ pgfree_data() trong cpu.c khi CPU thực hiện lệnh FREE.
 */
int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
#ifdef IODUMP
  if (val == 0) printf("libfree:218\n");
  else          printf("libfree:218 failed\n");
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif
  return val;
}

/*
 * pg_getpage: Đảm bảo page pgn đang ở RAM và trả về FPN của nó.
 * Đây là TRÁI TIM của hệ thống paging — xử lý page fault.
 *
 * LUỒNG XỬ LÝ:
 *
 * CASE 1: Page đã ở RAM (PRESENT=1):
 *   → Lấy FPN từ PTE, return 0.
 *
 * CASE 2: Page chưa có trong RAM (PRESENT=0):
 *   CASE 2a: RAM còn frame trống:
 *     → MEMPHY_get_freefp → tgtfpn
 *     → pte_set_fpn(pgn, tgtfpn)  — map page vào frame mới
 *     → enlist_pgn_node  — thêm vào FIFO tracking list
 *     → Nếu page chưa từng có data (SWAPPED=0): frame đã zero (calloc)
 *
 *   CASE 2b: RAM đầy → cần SWAP OUT victim:
 *     Bước 1: find_victim_page → vicpgn (page cũ nhất trong FIFO)
 *     Bước 2: MEMPHY_get_freefp(SWAP) → swpfpn
 *     Bước 3: __swap_cp_page(RAM[vicfpn] → SWAP[swpfpn])
 *     Bước 4: pte_set_swap(vicpgn) → đánh dấu victim đang ở SWAP
 *     Bước 5: Swap IN page pgn vào frame vicfpn (vừa giải phóng):
 *       - Nếu pgn đang ở SWAP (SWAPPED=1): copy SWAP→RAM, giải phóng slot SWAP cũ
 *       - Nếu pgn chưa từng dùng: zero-fill frame vicfpn
 *     Bước 6: pte_set_fpn(pgn, vicfpn)  — page pgn giờ ở RAM
 *
 * Kết quả: *fpn = FPN của frame đang chứa page pgn trong RAM.
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  (void)mm;
  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte)) {
    /* Page fault: page pgn không có trong RAM */
    addr_t vicpgn, swpfpn;
    addr_t tgtfpn = 0;

    if (MEMPHY_get_freefp(caller->krnl->mram, &tgtfpn) == 0) {
      /* CASE 2a: RAM còn frame trống → cấp thẳng */
      pte_set_fpn(caller, pgn, tgtfpn);
      enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
      pte = pte_get_entry(caller, pgn);
    } else {
      /* CASE 2b: RAM đầy → swap out victim, swap in pgn */
      addr_t vicfpn;
      uint32_t vicpte;

      /* Chọn victim (page cũ nhất theo FIFO) */
      if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
        return -1;

      /* Lấy slot trống trong SWAP để chứa victim */
      if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
        return -1;

      /* Đọc FPN hiện tại của victim */
      vicpte = pte_get_entry(caller, vicpgn);
      vicfpn = PAGING_FPN(vicpte);

      /* Copy victim: RAM[vicfpn] → SWAP[swpfpn] */
      __swap_cp_page(caller->krnl->mram, vicfpn,
                     caller->krnl->active_mswp, swpfpn);
      /* Cập nhật PTE victim: đánh dấu "đang ở SWAP" */
      pte_set_swap(caller, vicpgn, caller->krnl->active_mswp_id, swpfpn);

      /* Swap in page pgn vào frame vicfpn (vừa được giải phóng) */
      if ((pte & PAGING_PTE_SWAPPED_MASK) != 0) {
        /* pgn đang ở SWAP → copy SWAP→RAM */
        addr_t oldswpfpn = PAGING_SWP(pte);
        __swap_cp_page(caller->krnl->active_mswp, oldswpfpn,
                       caller->krnl->mram, vicfpn);
        MEMPHY_put_freefp(caller->krnl->active_mswp, oldswpfpn); /* trả slot SWAP */
      } else {
        /* pgn chưa từng dùng → zero-fill frame */
        int i;
        for (i = 0; i < PAGING_PAGESZ; i++)
          MEMPHY_write(caller->krnl->mram, vicfpn * PAGING_PAGESZ + i, 0);
      }

      /* Ghi mapping mới: pgn → vicfpn */
      pte_set_fpn(caller, pgn, vicfpn);
      enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
      pte = pte_get_entry(caller, pgn);
    }
  }

  *fpn = PAGING_FPN(pte_get_entry(caller, pgn));
  return 0;
}

/*
 * pg_getval: Đọc 1 byte từ địa chỉ ảo addr.
 *
 * Cách dịch địa chỉ:
 *   virtual addr → PGN + OFFSET
 *   pg_getpage(pgn) → FPN (đảm bảo page ở RAM)
 *   physical_addr = FPN * PAGESZ + OFFSET
 *                 = (FPN << PAGING_ADDR_FPN_LOBIT) + offset
 *
 * Đọc thực tế qua syscall 17 (SYSMEM_IO_READ):
 *   Không gọi MEMPHY_read trực tiếp mà đi qua syscall layer
 *   để có thể kiểm tra quyền truy cập và log.
 *   Kết quả trả về trong regs.a3.
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);   /* lấy page number từ địa chỉ ảo */
  int fpn;

  /* Đảm bảo page ở RAM (swap in nếu cần) */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1;

  /* Tính địa chỉ vật lý */
  int off     = PAGING_OFFST(addr);
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  /* Gọi syscall 17 để đọc byte tại địa chỉ vật lý */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;
  regs.a3 = 0;
  _syscall(caller->krnl, caller->pid, 17, &regs);
  *data = (BYTE)regs.a3;

  return 0;
}

/*
 * pg_setval: Ghi 1 byte value vào địa chỉ ảo addr.
 *
 * Giống pg_getval nhưng ghi (SYSMEM_IO_WRITE).
 * pg_getpage được gọi trước để đảm bảo page đang ở RAM
 * (nếu đang ở SWAP thì swap in trước).
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1;

  int off     = PAGING_OFFST(addr);
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = phyaddr;
  regs.a3 = value;
  _syscall(caller->krnl, caller->pid, 17, &regs);

  return 0;
}

/*
 * __read: Đọc 1 byte tại (vmaid, rgid, offset) từ bộ nhớ ảo.
 *
 * Luồng:
 *   symrgtbl[rgid] → vùng ảo [rg_start, rg_end)
 *   virtual_addr   = rg_start + offset
 *   pg_getval(virtual_addr) → đọc byte → *data
 *
 * Kiểm tra bounds: offset phải < (rg_end - rg_start).
 * mmvm_lock: bảo vệ truy cập đồng thời vào mm_struct.
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg   = get_symrg_byid(caller->krnl->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* Privilege check: READ can only access userspace regions (mode_bit == 1).
   * Only enforce when the region is actively allocated (rg_end > rg_start). */
  if (currg->rg_end > currg->rg_start && currg->mode_bit != 1) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* Kiểm tra bounds (chỉ cho vùng đã alloc, kích thước > 0) */
  if (currg->rg_end > currg->rg_start &&
      currg->rg_start + offset >= currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller) != 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*
 * libread: Wrapper CPU-level cho __read.
 * Gọi từ pgread() trong cpu.c khi CPU thực hiện lệnh READ.
 * source: rgid (register chứa ID vùng nhớ)
 * offset: byte offset trong vùng đó
 * *destination: nhận kết quả đọc
 */
int libread(struct pcb_t *proc, uint32_t source, addr_t offset, uint32_t *destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);
  if (val == 0)
    *destination = data;
#ifdef IODUMP
  if (val == 0) printf("libread:426\n");
  else          printf("libread:426 failed\n");
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif
  return val;
}

/*
 * __write: Ghi 1 byte value vào (vmaid, rgid, offset).
 * Tương tự __read nhưng gọi pg_setval thay vì pg_getval.
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg   = get_symrg_byid(caller->krnl->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* Privilege check: WRITE can only access userspace regions (mode_bit == 1).
   * Only enforce when the region is actively allocated (rg_end > rg_start). */
  if (currg->rg_end > currg->rg_start && currg->mode_bit != 1) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* Bounds check: offset phải nằm trong vùng đã cấp phát.
   * Cùng logic với __read để đảm bảo nhất quán giữa đọc và ghi. */
  if (currg->rg_end > currg->rg_start &&
      currg->rg_start + offset >= currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller) != 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*
 * libwrite: Wrapper CPU-level cho __write.
 * Gọi từ pgwrite() trong cpu.c khi CPU thực hiện lệnh WRITE.
 * destination: rgid (register chứa ID vùng nhớ đích)
 * data: byte cần ghi
 * offset: byte offset trong vùng đó
 */
int libwrite(struct pcb_t *proc, BYTE data, uint32_t destination, addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
#ifdef IODUMP
  if (val == 0) printf("libwrite:502\n");
  else          printf("libwrite:502 failed\n");
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif
  return val;
}


/*
 * libkmem_malloc: Cấp phát vùng nhớ kernel (KMALLOC instruction từ CPU).
 * Hiện tại delegate về __alloc với vmaid=0 (chưa có kernel VM riêng).
 * Địa chỉ được ghi vào caller->regs[reg_index].
 */
int libkmem_malloc(struct pcb_t *caller, uint32_t size, uint32_t reg_index)
{
  addr_t addr = 0;
  int val = (int)__kmalloc(caller, -1, reg_index, size, &addr);
  if (val < 0)
    return -1;
  caller->regs[reg_index] = addr;
  return 0;
}

/*
 * __kmalloc: Kernel-level malloc — hiện dùng chung page table với user.
 * Trong kernel thực: sẽ có krnl_pgd riêng cho kernel space.
 * Ở đây: đơn giản gọi __alloc với vmaid=0.
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  (void)vmaid;
  if (__alloc(caller, 0, rgid, size, alloc_addr) < 0)
    return (addr_t)-1;
  /* Override mode_bit: kernel allocation → kernelmode = 0 */
  caller->krnl->mm->symrgtbl[rgid].mode_bit = 0;
  return *alloc_addr;
}

/*
 * libkmem_cache_pool_create: Khởi tạo cache pool cho kernel memory.
 * Cache pool là tập hợp các slot có kích thước CỐ ĐỊNH (size bytes mỗi slot).
 * Dùng cho các object nhỏ cùng kích thước (ví dụ: PCB, inode).
 * Nhanh hơn malloc/free vì không cần tìm kiếm vùng trống.
 *
 * cache_pool_id: ID pool (0..PAGING_MAX_SYMTBL_SZ-1)
 * size, align: kích thước và alignment của mỗi slot
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size,
                               uint32_t align, uint32_t cache_pool_id)
{
  if (caller->krnl->mm->kcpooltbl == NULL)
    caller->krnl->mm->kcpooltbl = calloc(PAGING_MAX_SYMTBL_SZ,
                                          sizeof(struct kcache_pool_struct));
  if (cache_pool_id >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  caller->krnl->mm->kcpooltbl[cache_pool_id].size    = size;
  caller->krnl->mm->kcpooltbl[cache_pool_id].align   = align;
  caller->krnl->mm->kcpooltbl[cache_pool_id].storage = 0;
  return 0;
}

/*
 * libkmem_cache_alloc: Cấp phát 1 slot từ cache pool cache_pool_id.
 * Kích thước = pool[cache_pool_id].size (đã đăng ký trước).
 * Kết quả ghi vào proc->regs[reg_index].
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t reg_index, uint32_t cache_pool_id)
{
  addr_t addr = 0;
  if (__kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr) == (addr_t)-1)
    return -1;
  proc->regs[reg_index] = addr;
  return 0;
}

/*
 * __kmem_cache_alloc: Backend cho cache alloc.
 * Tra kcpooltbl[cache_pool_id].size rồi gọi __kmalloc với kích thước đó.
 */
addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid,
                           int cache_pool_id, addr_t *alloc_addr)
{
  if (caller->krnl->mm->kcpooltbl == NULL)
    return (addr_t)-1;
  if (cache_pool_id < 0 || cache_pool_id >= PAGING_MAX_SYMTBL_SZ)
    return (addr_t)-1;

  if (__kmalloc(caller, vmaid, rgid,
                caller->krnl->mm->kcpooltbl[cache_pool_id].size,
                alloc_addr) == (addr_t)-1)
    return (addr_t)-1;

  return *alloc_addr;
}

/*
 * libkmem_copy_from_user: Copy size bytes từ user memory vào kernel memory.
 * source     : rgid trong user space
 * destination: rgid trong kernel space
 * offset     : byte offset trong user region
 * Dùng cho syscall — kernel đọc dữ liệu do user cung cấp.
 */
int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source,
                            uint32_t destination, uint32_t offset, uint32_t size)
{
  uint32_t i;
  BYTE data;
  for (i = 0; i < size; i++) {
    if (__read_user_mem(caller,   0, source,      offset + i, &data) < 0) return -1;
    if (__write_kernel_mem(caller, 0, destination, i,           data) < 0) return -1;
  }
  return 0;
}

/*
 * libkmem_copy_to_user: Copy size bytes từ kernel memory sang user memory.
 * Ngược lại với copy_from_user — kernel trả kết quả về user space.
 */
int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source,
                          uint32_t destination, uint32_t offset, uint32_t size)
{
  uint32_t i;
  BYTE data;
  for (i = 0; i < size; i++) {
    if (__read_kernel_mem(caller,  0, source,      i,           &data) < 0) return -1;
    if (__write_user_mem(caller,   0, destination, offset + i,  data)  < 0) return -1;
  }
  return 0;
}


/* __read_kernel_mem / __write_kernel_mem: Đọc/ghi kernel memory.
 * Hiện tại không phân biệt user/kernel space (dùng chung page table).
 * Trong kernel thực: sẽ phải kiểm tra quyền ring 0. */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  return __read(caller, vmaid, rgid, offset, data);
}
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  return __write(caller, vmaid, rgid, offset, value);
}

/* __read_user_mem / __write_user_mem: Đọc/ghi user memory.
 * Trong kernel thực: phải kiểm tra con trỏ user hợp lệ trước khi truy cập.
 * Ở đây: delegate trực tiếp về __read/__write. */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  return __read(caller, vmaid, rgid, offset, data);
}
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  return __write(caller, vmaid, rgid, offset, value);
}

/*
 * free_pcb_memph: Giải phóng toàn bộ frame vật lý của process khi kết thúc.
 *
 * Duyệt pgd[0..PAGING_MAX_PGN-1]:
 *   Nếu PRESENT=1: frame ở RAM → trả lại MEMPHY free list của RAM
 *   Nếu PRESENT=0: page có thể ở SWAP → trả lại MEMPHY free list của SWAP
 *
 * Sau khi gọi hàm này, tất cả frame của process đều được thu hồi
 * và sẵn sàng cấp cho process mới.
 *
 * LƯU Ý: Hàm này dùng pgd[] (32-bit flat page table) thay vì pt[].
 * Trong MM64 nên dùng pt[] — đây là điểm cần cải thiện trong tương lai.
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

#ifdef MM64
  /* In MM64 mode, leaf page table entries are in pt[] */
  for (pagenum = 0; pagenum < PAGING64_MAX_PGN; pagenum++) {
    pte = (uint32_t)caller->krnl->mm->pt[pagenum];
    if (pte == 0)
      continue; /* unmapped page, skip */
    if (PAGING_PAGE_PRESENT(pte)) {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);         /* return RAM frame */
    } else {
      fpn = PAGING_SWP(pte);
      if (fpn != 0)
        MEMPHY_put_freefp(caller->krnl->active_mswp, fpn); /* return SWAP slot */
    }
  }
#else
  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++) {
    pte = caller->krnl->mm->pgd[pagenum];
    if (PAGING_PAGE_PRESENT(pte)) {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);         /* trả frame RAM */
    } else {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);  /* trả slot SWAP */
    }
  }
#endif

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*
 * find_victim_page: Chọn victim page để swap out theo thuật toán FIFO.
 *
 * fifo_pgn là stack-like list: page MỚI được thêm vào ĐẦU (enlist_pgn_node).
 * Victim là page ở CUỐI list → page được map LÂU NHẤT (oldest = FIFO victim).
 *
 * Ví dụ: fifo_pgn = [pgn=5] → [pgn=3] → [pgn=1] → NULL
 *   (5 được map sau cùng, 1 được map đầu tiên)
 *   find_victim_page → *retpgn = 1 (page cũ nhất)
 *   Sau đó: fifo_pgn = [pgn=5] → [pgn=3] → NULL
 *
 * Trả về 0 thành công, -1 nếu list rỗng.
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;
  if (!pg)
    return -1;

  /* Chỉ có 1 phần tử */
  if (pg->pg_next == NULL) {
    *retpgn  = pg->pgn;
    mm->fifo_pgn = NULL;
    free(pg);
    return 0;
  }

  /* Duyệt đến phần tử CUỐI (oldest) */
  struct pgn_t *prev = NULL;
  while (pg->pg_next) {
    prev = pg;
    pg   = pg->pg_next;
  }
  *retpgn       = pg->pgn;
  prev->pg_next = NULL;
  free(pg);
  return 0;
}

/*
 * get_free_vmrg_area: Tìm vùng trống kích thước >= size trong VMA vmaid.
 *
 * Thuật toán FIRST FIT: duyệt vm_freerg_list, lấy vùng đầu tiên đủ lớn.
 *
 * Ví dụ freerg_list: [10,50) → [100,120) → [200,300) → NULL
 *   Yêu cầu size=15:
 *   - [10,50): kích thước=40 >= 15 → chọn [10,25), cập nhật còn [25,50)
 *   - newrg = {rg_start=10, rg_end=25}
 *
 * Xử lý khi dùng hết vùng được chọn (rg_start+size == rg_end):
 *   Thay vì xóa node (phức tạp khi ở giữa linked list), clone node tiếp theo
 *   vào chính node đó và giải phóng node tiếp theo.
 *   Nếu không có node tiếp: tạo "dummy" node rỗng (rg_start = rg_end).
 *
 * Trả về 0 nếu tìm được, -1 nếu không đủ chỗ.
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  newrg->rg_start = newrg->rg_end = -1; /* chưa tìm thấy */

  while (rgit != NULL) {
    if (rgit->rg_start + size <= rgit->rg_end) {
      /* Tìm được vùng đủ lớn — lấy [rg_start, rg_start+size) */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end   = rgit->rg_start + size;

      if (rgit->rg_start + size < rgit->rg_end) {
        /* Vùng còn dư → thu nhỏ phần đầu */
        rgit->rg_start = rgit->rg_start + size;
      } else {
        /* Dùng hết vùng → "xóa" node bằng cách clone node tiếp */
        struct vm_rg_struct *nextrg = rgit->rg_next;
        if (nextrg != NULL) {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end   = nextrg->rg_end;
          rgit->rg_next  = nextrg->rg_next;
          free(nextrg);
        } else {
          /* Cuối list: để lại node dummy rỗng */
          rgit->rg_start = rgit->rg_end;
          rgit->rg_next  = NULL;
        }
      }
      break;
    }
    rgit = rgit->rg_next;
  }

  if (newrg->rg_start == -1)
    return -1; /* không tìm được vùng phù hợp */

  return 0;
}
