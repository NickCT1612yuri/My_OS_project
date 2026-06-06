/*
 * ============================================================
 * FILE: cpu.c
 * MỤC ĐÍCH: Mô phỏng vòng lặp Fetch-Decode-Execute của CPU.
 *
 * VÒNG LẶP CỦA CPU (trong os.c cpu_routine):
 *   while (còn lệnh) {
 *       run(proc);   ← gọi hàm này mỗi "clock cycle"
 *   }
 *
 * Hàm run() thực hiện một lệnh:
 *   1. Fetch  : lấy lệnh tại proc->code->text[proc->pc]
 *   2. Decode : nhận diện opcode
 *   3. Execute: gọi hàm xử lý tương ứng
 *   4. Update : tăng pc++ (Program Counter)
 *
 * HAI CHẾ ĐỘ (kiểm soát bởi #define MM_PAGING):
 *   MM_PAGING bật  → dùng lib* functions (libmem.c) — Paging MM
 *   MM_PAGING tắt  → dùng alloc_mem/free_mem/read_mem/write_mem (mem.c)
 * ============================================================
 */
#include "cpu.h"
#include "mem.h"
#include "mm.h"
#include "syscall.h"
#include "libmem.h"

/* Số thanh ghi của CPU (= 10, từ proc->regs[10] trong common.h) */
#define PROC_REG_COUNT(proc) (sizeof((proc)->regs) / sizeof((proc)->regs[0]))

/* valid_reg_index: kiểm tra reg_index có hợp lệ không (0..9).
 * Ngăn chặn buffer overflow khi truy cập proc->regs[]. */
static int valid_reg_index(struct pcb_t *proc, uint32_t reg_index)
{
        return reg_index < PROC_REG_COUNT(proc);
}

/* calc: thực thi lệnh CALC — không làm gì thực sự, chỉ tiêu tốn 1 cycle.
 * Dùng để mô phỏng "tiến trình đang tính toán" mà không cần bộ nhớ. */
int calc(struct pcb_t *proc)
{
        return ((unsigned long)proc & 0UL); /* luôn trả về 0 = success */
}

/* ── HAI HÀM SAU CHỈ DÙNG KHI KHÔNG CÓ MM_PAGING (chế độ legacy) ── */

/* alloc: cấp phát bộ nhớ theo cơ chế phân đoạn đơn giản (mem.c).
 *   size      : kích thước cần cấp phát (bytes)
 *   reg_index : chỉ số thanh ghi sẽ lưu địa chỉ được cấp phát
 * Lưu địa chỉ vào proc->regs[reg_index] để dùng sau trong READ/WRITE. */
int alloc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
        addr_t addr = alloc_mem(size, proc);
        if (addr == 0 || !valid_reg_index(proc, reg_index))
                return 1;
        proc->regs[reg_index] = addr;
        return 0;
}

/* free_data: giải phóng vùng nhớ trỏ bởi regs[reg_index]. */
int free_data(struct pcb_t *proc, uint32_t reg_index)
{
        if (!valid_reg_index(proc, reg_index))
                return 1;
        return free_mem(proc->regs[reg_index], proc);
}

/* read: đọc 1 byte từ địa chỉ (regs[source] + offset) vào regs[dest].
 *   source      : reg chứa địa chỉ base của vùng nhớ cần đọc
 *   offset      : độ lệch từ base
 *   destination : reg nhận giá trị đọc được
 * Trả về 0 nếu thành công, 1 nếu lỗi (địa chỉ không hợp lệ). */
int read(struct pcb_t *proc, uint32_t source, uint32_t offset, uint32_t destination)
{
        BYTE data;
        if (!valid_reg_index(proc, source) || !valid_reg_index(proc, destination))
                return 1;
        if (read_mem(proc->regs[source] + offset, proc, &data)) {
                proc->regs[destination] = data;
                return 0;
        }
        return 1;
}

/* write: ghi 1 byte data vào địa chỉ (regs[destination] + offset).
 *   data        : byte cần ghi
 *   destination : reg chứa địa chỉ base
 *   offset      : độ lệch từ base */
int write(struct pcb_t *proc, BYTE data, uint32_t destination, uint32_t offset)
{
        if (!valid_reg_index(proc, destination))
                return 1;
        return write_mem(proc->regs[destination] + offset, proc, data);
}

/* ── HÀM CHÍNH: run() ────────────────────────────────────────
 * Thực thi MỘT lệnh của tiến trình proc (một "clock cycle").
 * Được gọi từ cpu_routine trong os.c.
 *
 * Trả về 0 nếu lệnh thực thi thành công, 1 nếu lỗi.
 *
 * QUY TRÌNH:
 *   1. Kiểm tra pc hợp lệ (chưa hết chương trình)
 *   2. Đọc lệnh từ code->text[pc], tăng pc
 *   3. Switch trên opcode → gọi hàm tương ứng
 *
 * MỖI LỆNH NHẬN THAM SỐ:
 *   ALLOC  : arg_0=size,  arg_1=reg_index
 *   FREE   : arg_0=reg_index
 *   READ   : arg_0=source_reg, arg_1=offset, arg_2=dest_reg
 *   WRITE  : arg_0=data, arg_1=dest_reg, arg_2=offset
 *   KMALLOC: arg_0=size,  arg_1=reg_index
 *   SYSCALL: arg_0..arg_3 tuỳ từng syscall
 */
int run(struct pcb_t *proc)
{
        if (proc->pc >= proc->code->size)
                return 1; /* đã hết lệnh */

        struct inst_t ins = proc->code->text[proc->pc];
        proc->pc++; /* tăng PC trước khi thực thi */
        int stat = 1;

        switch (ins.opcode) {
        case CALC:
                stat = calc(proc);
                break;

        case ALLOC:
#ifdef MM_PAGING
                /* Paging mode: dùng liballoc() trong libmem.c
                 * → __alloc() → sys_memmap → vmap_page_range → pte_set_fpn */
                stat = liballoc(proc, ins.arg_0, ins.arg_1);
#else
                stat = alloc(proc, ins.arg_0, ins.arg_1);
#endif
                break;

#ifdef MM_PAGING
        case KMALLOC:
                /* Cấp phát kernel memory (mm được quản lý ở tầng kernel) */
                stat = libkmem_malloc(proc, ins.arg_0, ins.arg_1);
                break;
        case KMEM_CACHE_CREATE:
                stat = libkmem_cache_pool_create(proc, ins.arg_0, ins.arg_1, ins.arg_2);
                break;
        case KMEM_CACHE_ALLOC:
                stat = libkmem_cache_alloc(proc, ins.arg_0, ins.arg_1);
                break;
        case COPY_FROM_USER:
                stat = libkmem_copy_from_user(proc, ins.arg_0, ins.arg_1, ins.arg_2, ins.arg_3);
                break;
        case COPY_TO_USER:
                stat = libkmem_copy_to_user(proc, ins.arg_0, ins.arg_1, ins.arg_2, ins.arg_3);
                break;
#endif

        case FREE:
#ifdef MM_PAGING
                /* Paging mode: dùng libfree() trong libmem.c
                 * → __free() → enlist_vm_freerg_list (trả vùng về free list) */
                stat = libfree(proc, ins.arg_0);
#else
                stat = free_data(proc, ins.arg_0);
#endif
                break;

        case READ:
#ifdef MM_PAGING
                {
                        /* Paging mode: libread() → pg_getpage() → pg_getval()
                         * Nếu page không online → swap in từ SWAP */
                        uint32_t read_val = 0;
                        stat = libread(proc, ins.arg_0, ins.arg_1, &read_val);
                        if (stat == 0) {
                                if (valid_reg_index(proc, ins.arg_2))
                                        proc->regs[ins.arg_2] = read_val;
                                else
                                        stat = 1;
                        }
                }
#else
                stat = read(proc, ins.arg_0, ins.arg_1, ins.arg_2);
#endif
                break;

        case WRITE:
#ifdef MM_PAGING
                /* Paging mode: libwrite() → pg_setpage() → pg_setval()
                 * Gọi syscall SYSMEM_IO_WRITE để ghi vật lý */
                stat = libwrite(proc, ins.arg_0, ins.arg_1, ins.arg_2);
#else
                stat = write(proc, ins.arg_0, ins.arg_1, ins.arg_2);
#endif
                break;

        case SYSCALL:
                stat = libsyscall(proc, ins.arg_0, ins.arg_1, ins.arg_2, ins.arg_3);
                break;

        default:
                stat = 1;
        }
        return stat;
}
