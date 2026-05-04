#include "cpu.h"
#include "mem.h"
#include "mm.h"
#include "syscall.h"
#include "libmem.h"

#define PROC_REG_COUNT(proc) (sizeof((proc)->regs) / sizeof((proc)->regs[0]))

static int valid_reg_index(struct pcb_t *proc, uint32_t reg_index)
{
        return reg_index < PROC_REG_COUNT(proc);
}

int calc(struct pcb_t *proc)
{
        return ((unsigned long)proc & 0UL);
}

int alloc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
        addr_t addr = alloc_mem(size, proc);
        if (addr == 0 || !valid_reg_index(proc, reg_index))
        {
                return 1;
        }
        else
        {
                proc->regs[reg_index] = addr;
                return 0;
        }
}

int free_data(struct pcb_t *proc, uint32_t reg_index)
{
        if (!valid_reg_index(proc, reg_index))
                return 1;
        return free_mem(proc->regs[reg_index], proc);
}

int read(
        struct pcb_t *proc, // Process executing the instruction
        uint32_t source,    // Index of source register
        uint32_t offset,    // Source address = [source] + [offset]
        uint32_t destination)
{ // Index of destination register

        BYTE data;
        if (!valid_reg_index(proc, source) ||
            !valid_reg_index(proc, destination))
        {
                return 1;
        }

        if (read_mem(proc->regs[source] + offset, proc, &data))
        {
                proc->regs[destination] = data;
                return 0;
        }
        else
        {
                return 1;
        }
}

int write(
        struct pcb_t *proc, // Process executing the instruction
        BYTE data,          // Data to be wrttien into memory
        uint32_t destination, // Index of destination register
        uint32_t offset)
{ // Destination address =
        // [destination] + [offset]
        if (!valid_reg_index(proc, destination))
                return 1;
        return write_mem(proc->regs[destination] + offset, proc, data);
}

int run(struct pcb_t *proc)
{
        /* Check if Program Counter point to the proper instruction */
        if (proc->pc >= proc->code->size)
        {
                return 1;
        }

        struct inst_t ins = proc->code->text[proc->pc];
        proc->pc++;
        int stat = 1;
        switch (ins.opcode)
        {
        case CALC:
                stat = calc(proc);
                break;
        case ALLOC:
#ifdef MM_PAGING
                stat = liballoc(proc, ins.arg_0, ins.arg_1);
#else
                stat = alloc(proc, ins.arg_0, ins.arg_1);
#endif
                break;
#ifdef MM_PAGING
        case KMALLOC:
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
                stat = libfree(proc, ins.arg_0);
#else
                stat = free_data(proc, ins.arg_0);
#endif
                break;
        case READ:
#ifdef MM_PAGING
                {
                        uint32_t read_val = 0;
                        stat = libread(proc, ins.arg_0, ins.arg_1, &read_val);
                        if (stat == 0)
                        {
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
