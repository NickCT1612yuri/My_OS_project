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
 * FILE: sys_mem.c
 * MỤC ĐÍCH: Triển khai syscall 17 — __sys_memmap.
 *
 * __sys_memmap là syscall duy nhất xử lý TẤT CẢ các thao tác MM
 * từ user space, được phân biệt bởi opcode trong regs->a1:
 *
 *   SYSMEM_MAP_OP  : vmap_pgd_memset   — pattern-fill các page (debug)
 *   SYSMEM_INC_OP  : inc_vma_limit     — mở rộng heap (sbrk)
 *   SYSMEM_SWP_OP  : __mm_swap_page    — swap 1 frame RAM→SWAP
 *   SYSMEM_IO_READ : MEMPHY_read       — đọc 1 byte từ RAM vật lý
 *   SYSMEM_IO_WRITE: MEMPHY_write      — ghi 1 byte vào RAM vật lý
 *
 * TÌM CALLER PCB:
 *   Syscall nhận krnl+pid nhưng không có con trỏ trực tiếp đến PCB.
 *   Phải tìm PCB có pid khớp trong running_list và ready_queue.
 *   Đây là thiết kế "kernel space isolation" — user process không
 *   thể tự truyền con trỏ PCB vào kernel (bảo mật).
 *
 * Quy trình:
 *   1. Tìm caller trong running_list (đang chạy trên CPU)
 *   2. Nếu không thấy → tìm trong mlq_ready_queue/ready_queue
 *   3. Nếu vẫn không thấy → return -1 (syscall thất bại)
 *   4. Dispatch memop đến hàm xử lý tương ứng
 * ============================================================
 */
#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

/* Expose queue_lock from sched.c for safe queue traversal */
extern void sched_queue_lock(void);
extern void sched_queue_unlock(void);

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
  int memop = regs->a1; /* opcode của memory operation */
  BYTE value;

  /* Bước 1: Tìm PCB của tiến trình gọi syscall trong running_list */
  struct pcb_t *caller = NULL;
  sched_queue_lock();
  if (krnl->running_list != NULL) {
    int i;
    for (i = 0; i < krnl->running_list->size; i++) {
      if (krnl->running_list->proc[i] != NULL &&
          krnl->running_list->proc[i]->pid == pid) {
        caller = krnl->running_list->proc[i];
        break;
      }
    }
  }

  /* Bước 2: Nếu không thấy trong running_list → tìm trong ready queue */
  if (caller == NULL) {
#ifdef MLQ_SCHED
    if (krnl->mlq_ready_queue != NULL) {
      int prio, i;
      for (prio = 0; prio < MAX_PRIO && caller == NULL; prio++) {
        struct queue_t *q = &krnl->mlq_ready_queue[prio];
        for (i = 0; i < q->size; i++) {
          if (q->proc[i] != NULL && q->proc[i]->pid == pid) {
            caller = q->proc[i];
            break;
          }
        }
      }
    }
#else
    if (krnl->ready_queue != NULL) {
      int i;
      for (i = 0; i < krnl->ready_queue->size; i++) {
        if (krnl->ready_queue->proc[i] != NULL &&
            krnl->ready_queue->proc[i]->pid == pid) {
          caller = krnl->ready_queue->proc[i];
          break;
        }
      }
    }
#endif
  }
  sched_queue_unlock();

  if (caller == NULL)
    return -1; /* không tìm thấy caller → syscall thất bại */

  /* Bước 3: Dispatch memory operation */
  switch (memop) {
  case SYSMEM_MAP_OP:
    /* Debug: điền pattern 0xDEADBEEF vào pgnum trang từ địa chỉ a2 */
    vmap_pgd_memset(caller, regs->a2, regs->a3);
    break;

  case SYSMEM_INC_OP:
    /* Mở rộng heap VMA[a2] thêm a3 bytes (giống sbrk) */
    inc_vma_limit(caller, regs->a2, regs->a3);
    break;

  case SYSMEM_SWP_OP:
    /* Swap out: copy frame RAM[a2] sang SWAP[a3] */
    __mm_swap_page(caller, regs->a2, regs->a3);
    break;

  case SYSMEM_IO_READ:
    /* Đọc 1 byte từ địa chỉ vật lý a2 trong RAM */
    MEMPHY_read(krnl->mram, regs->a2, &value);
    regs->a3 = value; /* trả về qua regs */
    break;

  case SYSMEM_IO_WRITE:
    /* Ghi 1 byte (a3) vào địa chỉ vật lý a2 trong RAM */
    MEMPHY_write(krnl->mram, regs->a2, regs->a3);
    break;

  default:
    printf("Memop code: %d\n", memop);
    break;
  }

  return 0;
}


