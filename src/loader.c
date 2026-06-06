/*
 * ============================================================
 * FILE: loader.c
 * MỤC ĐÍCH: Đọc file mô tả tiến trình và tạo PCB (Process Control Block).
 *
 * ĐỊNH DẠNG FILE TIẾN TRÌNH (ví dụ input/proc/p0s):
 *   Dòng 1: <priority> <số_lệnh>
 *   Dòng 2..N: <opcode> [tham số...]
 *
 * Ví dụ file p0s:
 *   0 6            ← priority=0, 6 lệnh
 *   alloc 100 0    ← ALLOC 100 bytes, lưu vào reg[0]
 *   write 5 0 0    ← WRITE byte=5 vào reg[0]+offset=0
 *   read  0 0 1    ← READ từ reg[0]+0, lưu vào reg[1]
 *   free  0        ← FREE vùng nhớ ở reg[0]
 *   alloc 50  0    ← ALLOC lại
 *   write 10 0 1   ← WRITE tiếp
 *
 * avail_pid: biến toàn cục cấp PID tăng dần (1, 2, 3, ...)
 *            cho mỗi tiến trình được load.
 * ============================================================
 */
#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* avail_pid: PID tiếp theo sẽ được cấp. Bắt đầu từ 1.
 * Tăng lên 1 mỗi khi load một tiến trình mới. */
static uint32_t avail_pid = 1;

#define OPT_CALC	"calc"
#define OPT_ALLOC	"alloc"
#define OPT_FREE	"free"
#define OPT_READ	"read"
#define OPT_WRITE	"write"
#define OPT_SYSCALL	"syscall"
#define OPT_KMALLOC	            "kmalloc"
#define OPT_KMEM_CACHE_CREATE	"kmem_cache_create"
#define OPT_KMEM_CACHE_ALLOC	"kmem_cache_alloc"
#define OPT_COPY_FROM_USER	    "copy_from_user"
#define OPT_COPY_TO_USER	    "copy_to_user"

/*
 * get_opcode: Chuyển chuỗi opcode trong file tiến trình thành enum ins_opcode_t.
 *
 * Ánh xạ:
 *   "calc"              → CALC             (tiêu tốn 1 cycle, không dùng bộ nhớ)
 *   "alloc"             → ALLOC            (cấp phát vùng nhớ user)
 *   "free"              → FREE             (giải phóng vùng nhớ user)
 *   "read"              → READ             (đọc 1 byte từ vùng nhớ)
 *   "write"             → WRITE            (ghi 1 byte vào vùng nhớ)
 *   "kmalloc"           → KMALLOC          (cấp phát vùng nhớ kernel)
 *   "kmem_cache_create" → KMEM_CACHE_CREATE(tạo cache pool kích thước cố định)
 *   "kmem_cache_alloc"  → KMEM_CACHE_ALLOC (cấp slot từ cache pool)
 *   "copy_from_user"    → COPY_FROM_USER   (kernel đọc dữ liệu từ user space)
 *   "copy_to_user"      → COPY_TO_USER     (kernel ghi dữ liệu vào user space)
 *   "syscall"           → SYSCALL          (gọi trực tiếp kernel syscall)
 *
 * Nếu opcode không nhận ra → in lỗi và thoát (file tiến trình bị hỏng).
 */
static enum ins_opcode_t get_opcode(char *opt)
{
	if      (!strcmp(opt, OPT_CALC))              return CALC;
	else if (!strcmp(opt, OPT_ALLOC))             return ALLOC;
	else if (!strcmp(opt, OPT_FREE))              return FREE;
	else if (!strcmp(opt, OPT_READ))              return READ;
	else if (!strcmp(opt, OPT_WRITE))             return WRITE;
	else if (!strcmp(opt, OPT_KMALLOC))           return KMALLOC;
	else if (!strcmp(opt, OPT_KMEM_CACHE_CREATE)) return KMEM_CACHE_CREATE;
	else if (!strcmp(opt, OPT_KMEM_CACHE_ALLOC))  return KMEM_CACHE_ALLOC;
	else if (!strcmp(opt, OPT_COPY_FROM_USER))    return COPY_FROM_USER;
	else if (!strcmp(opt, OPT_COPY_TO_USER))      return COPY_TO_USER;
	else if (!strcmp(opt, OPT_SYSCALL))           return SYSCALL;
	else {
		printf("[loader] Opcode không nhận ra: '%s'\n", opt);
		exit(1);
	}
}

/* load: đọc file mô tả tiến trình tại đường dẫn `path`
 *       và trả về một PCB được khởi tạo đầy đủ.
 *
 * Sau khi load xong, PCB có:
 *   - pid được gán (avail_pid++)
 *   - code->text[] chứa toàn bộ lệnh từ file
 *   - pc = 0 (bắt đầu từ lệnh đầu tiên)
 *   - krnl = NULL (sẽ được gán trong ld_routine/os.c)
 *
 * Lưu ý: load() KHÔNG khởi tạo krnl->mm. Việc đó do os.c thực hiện
 * sau khi gọi load(), vì cần truyền mram/mswp vào krnl.
 */
struct pcb_t *load(const char *path)
{
	/* Tạo PCB mới trên heap */
	struct pcb_t *proc = (struct pcb_t *)malloc(sizeof(struct pcb_t));
	proc->pid  = avail_pid++;                              /* cấp PID duy nhất */
	proc->page_table = (struct page_table_t *)malloc(sizeof(struct page_table_t));
	proc->bp   = PAGE_SIZE; /* legacy break pointer (chế độ không paging) */
	proc->pc   = 0;         /* bộ đếm chương trình bắt đầu từ lệnh 0 */

	/* Mở file mô tả tiến trình */
	FILE *file;
	if ((file = fopen(path, "r")) == NULL) {
		printf("[loader] Không tìm thấy file tiến trình: '%s'\n", path);
		exit(1);
	}
	snprintf(proc->path, 2 * sizeof(path) + 1, "%s", path);

	/* Đọc dòng đầu: priority (mức ưu tiên) và số lượng lệnh */
	proc->code = (struct code_seg_t *)malloc(sizeof(struct code_seg_t));
	fscanf(file, "%u %u", &proc->priority, &proc->code->size);

	/* Cấp phát mảng lệnh đủ cho proc->code->size lệnh */
	proc->code->text = (struct inst_t *)malloc(
		sizeof(struct inst_t) * proc->code->size);

	/* Đọc từng lệnh trong file */
	uint32_t i = 0;
	char buf[200];
	char opcode[64];
	for (i = 0; i < proc->code->size; i++) {
		fscanf(file, "%s", opcode);
		proc->code->text[i].opcode = get_opcode(opcode);

		switch (proc->code->text[i].opcode) {

		case CALC:
			/* CALC không có tham số — chỉ tiêu tốn 1 cycle */
			break;

		case KMEM_CACHE_ALLOC:
		case KMALLOC:
		case ALLOC:
			/* 2 tham số: arg_0=size (bytes), arg_1=reg_index (nơi lưu địa chỉ) */
			fscanf(file, "" FORMAT_ARG " " FORMAT_ARG "\n",
			       &proc->code->text[i].arg_0,
			       &proc->code->text[i].arg_1);
			break;

		case FREE:
			/* 1 tham số: arg_0=reg_index (chứa địa chỉ vùng cần giải phóng) */
			fscanf(file, "" FORMAT_ARG "\n", &proc->code->text[i].arg_0);
			break;

		case KMEM_CACHE_CREATE:
		case READ:
		case WRITE:
			/* 3 tham số:
			 *   READ              : arg_0=source_reg, arg_1=offset, arg_2=dest_reg
			 *   WRITE             : arg_0=data,       arg_1=dest_reg, arg_2=offset
			 *   KMEM_CACHE_CREATE : arg_0=size,       arg_1=align,    arg_2=pool_id */
			fscanf(file, "" FORMAT_ARG " " FORMAT_ARG " " FORMAT_ARG "\n",
			       &proc->code->text[i].arg_0,
			       &proc->code->text[i].arg_1,
			       &proc->code->text[i].arg_2);
			break;

		case COPY_FROM_USER:
		case COPY_TO_USER:
		case SYSCALL:
			/* 4 tham số: đọc cả dòng rồi parse (dùng fgets + sscanf để an toàn) */
			fgets(buf, sizeof(buf), file);
			sscanf(buf, "" FORMAT_ARG "" FORMAT_ARG "" FORMAT_ARG "" FORMAT_ARG "",
			       &proc->code->text[i].arg_0,
			       &proc->code->text[i].arg_1,
			       &proc->code->text[i].arg_2,
			       &proc->code->text[i].arg_3);
			break;

		default:
			printf("[loader] Opcode không hợp lệ khi đọc tham số: %s\n", opcode);
			exit(1);
		}
	}

	fclose(file);
	return proc;
}



