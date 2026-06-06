
/*
 * ============================================================
 * FILE: os.c
 * MỤC ĐÍCH: Điểm khởi đầu (main) và điều phối toàn bộ OS simulator.
 *
 * KIẾN TRÚC HỆ THỐNG:
 *
 *   main()
 *    ├── read_config()      — đọc file cấu hình (input/os_0_mlq_paging)
 *    ├── init_memphy()      — khởi tạo RAM và SWAP
 *    ├── init_scheduler()   — khởi tạo hàng đợi MLQ
 *    ├── start_timer()      — khởi động timer thread
 *    ├── pthread_create(ld) — loader thread
 *    └── pthread_create(cpu[0..N-1]) — N CPU threads
 *
 * CÁC THREAD:
 *
 *   cpu_routine (N threads):
 *     Vòng lặp: lấy process từ ready queue → chạy time_slot instruction
 *     → nếu hết time slice: đưa lại queue
 *     → nếu process hoàn thành: giải phóng
 *     Đồng bộ với timer qua next_slot().
 *
 *   ld_routine (1 thread):
 *     Đọc danh sách process từ file config → load từng PCB tại đúng thời điểm
 *     → khởi tạo mm_struct → add_proc() vào scheduler
 *     Kết thúc: set done=1 để CPU biết không còn process nào.
 *
 *   timer_routine (internal):
 *     Đánh tick thời gian, báo hiệu cho CPU và loader tiến sang slot tiếp.
 *
 * CẤU TRÚC DỮ LIỆU QUAN TRỌNG:
 *   os         : krnl_t — kernel context chung (queues, RAM, SWAP)
 *   ld_processes: mảng path/start_time/prio của tất cả process
 *   time_slot  : số instruction mỗi CPU chạy trong 1 lần dispatch
 *   num_cpus   : số CPU ảo (threads)
 *   done       : flag báo loader đã load xong, CPU nên dừng nếu queue trống
 *
 * FILE CẤU HÌNH (ví dụ input/os_0_mlq_paging):
 *   2 1 3          ← time_slot=2, num_cpus=1, num_processes=3
 *   1024 512 0 0 0 ← memramsz=1024 byte, memswpsz[0]=512, ...
 *   0 p0 10        ← process p0 bắt đầu tại time=0, priority=10
 *   1 p1 5         ← process p1 bắt đầu tại time=1, priority=5
 *   2 p2 1         ← process p2 bắt đầu tại time=2, priority=1
 * ============================================================
 */
#include "cpu.h"
#include "timer.h"
#include "sched.h"
#include "loader.h"
#include "mm.h"
#include "libmem.h"
/* sched.h mistakenly reuses QUEUE_H as its include guard; undef it so
 * queue.h (which legitimately owns QUEUE_H) is not silently skipped. */
#undef QUEUE_H
#include "queue.h"
#ifdef MM64
#include "mm64.h"
#endif
#ifdef MM_PAGING
extern int free_pcb_memph(struct pcb_t *caller);
#endif

/* Expose queue_lock helpers from sched.c */
extern void sched_queue_lock(void);
extern void sched_queue_unlock(void);

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

static int time_slot;  /* số instruction mỗi process được chạy liên tục */
static int num_cpus;   /* số CPU thread */
static int done = 0;   /* 1 = loader đã load xong tất cả process */
static struct krnl_t os; /* kernel context toàn cục */

#ifdef MM_PAGING
static unsigned long memramsz;                       /* kích thước RAM (byte) */
static unsigned long memswpsz[PAGING_MAX_MMSWP];     /* kích thước mỗi SWAP device */

/* mmpaging_ld_args: struct đóng gói các tham số truyền vào ld_routine
 * (vì pthread chỉ nhận 1 con trỏ void* làm argument) */
struct mmpaging_ld_args {
  int vmemsz;                      /* kích thước virtual memory (chưa dùng) */
  struct memphy_struct *mram;      /* con trỏ đến RAM device */
  struct memphy_struct **mswp;     /* mảng SWAP devices */
  struct memphy_struct *active_mswp; /* SWAP device đang hoạt động (mswp[0]) */
  int active_mswp_id;              /* ID của active_mswp */
  struct timer_id_t *timer_id;     /* timer event cho loader thread */
};
#endif

/* ld_args: thông tin tất cả process cần load
 * path       : đường dẫn đến file định nghĩa process
 * start_time : thời điểm cần load (timer slot)
 * prio       : mức ưu tiên trong MLQ (chỉ có khi MLQ_SCHED) */
static struct ld_args {
  char **path;
  unsigned long *start_time;
#ifdef MLQ_SCHED
  unsigned long *prio;
#endif
} ld_processes;
int num_processes;

/* cpu_args: tham số truyền vào mỗi cpu_routine thread */
struct cpu_args {
  struct timer_id_t *timer_id; /* timer event của thread này */
  int id;                      /* CPU ID (0, 1, ..., num_cpus-1) */
};


/*
 * cpu_routine: Vòng lặp chính của 1 CPU ảo.
 *
 * Mỗi iteration = 1 "clock tick" (time slot):
 *
 * TRẠNG THÁI proc:
 *   NULL             → lấy process mới từ ready queue (get_proc)
 *   pc == code->size → process hoàn thành: in thông báo, giải phóng, lấy mới
 *   time_left == 0   → hết time slice: trả lại queue (put_proc), lấy process tiếp
 *
 * SAU KHI CÓ PROCESS:
 *   done=1 và proc=NULL → không còn gì để chạy → thoát thread
 *   proc=NULL (loader chưa ready) → skip slot, thử lại sau
 *   time_left==0 (process mới) → print "Dispatched", đặt time_left = time_slot
 *
 * CHẠY MỘT INSTRUCTION:
 *   run(proc) → thực hiện 1 lệnh (calc/alloc/free/read/write)
 *   time_left--
 *   next_slot() → đồng bộ với timer (chờ tick tiếp theo)
 *
 * THU HỒI KHI PROCESS HOÀN THÀNH:
 *   purgequeue(running_list, proc) → xóa khỏi running list
 *   free(proc) → giải phóng PCB
 *   (frame vật lý được giải phóng bởi free_pcb_memph — TODO trong bản này)
 */
static void *cpu_routine(void *args)
{
  struct timer_id_t *timer_id = ((struct cpu_args *)args)->timer_id;
  int id = ((struct cpu_args *)args)->id;
  int time_left = 0;
  struct pcb_t *proc = NULL;

  while (1) {
    if (proc == NULL) {
      /* Không có process → lấy từ ready queue */
      proc = get_proc();
      if (proc == NULL) {
        next_slot(timer_id);
        continue;
      }
    } else if (proc->pc == proc->code->size) {
      /* Process hoàn thành tất cả instruction */
      printf("\tCPU %d: Processed %2d has finished\n", id, proc->pid);
#ifdef MM_PAGING
      free_pcb_memph(proc);  /* reclaim physical frames before freeing PCB */
#endif
      sched_queue_lock();
      purgequeue(proc->krnl->running_list, proc);
      sched_queue_unlock();
      free(proc);
      proc = get_proc();
      time_left = 0;
    } else if (time_left == 0) {
      /* Hết time slice → preempt, đưa lại vào ready queue */
      printf("\tCPU %d: Put process %2d to run queue\n", id, proc->pid);
      put_proc(proc);
      proc = get_proc();
    }

    if (proc == NULL && done) {
      /* Loader xong, queue trống → CPU dừng */
      printf("\tCPU %d stopped\n", id);
      break;
    } else if (proc == NULL) {
      /* Loader chưa load xong → chờ slot tiếp */
      next_slot(timer_id);
      continue;
    } else if (time_left == 0) {
      /* Process mới được dispatch */
      printf("\tCPU %d: Dispatched process %2d\n", id, proc->pid);
      time_left = time_slot;
    }

    /* Thực hiện 1 instruction của process */
    run(proc);
    time_left--;
    next_slot(timer_id); /* đồng bộ timer */
  }

  detach_event(timer_id);
  pthread_exit(NULL);
}

/*
 * ld_routine: Thread loader — đọc và nạp các process vào hệ thống.
 *
 * KHỞI TẠO KERNEL PAGE TABLE:
 *   Trong MM64 mode: cấp phát 5 mảng krnl_pgd/.../krnl_pt cho kernel space.
 *   Hiện tại: điền địa chỉ heap (không phải PTE hợp lệ — chưa hoàn thiện).
 *
 * VÒI NẠP PROCESS:
 *   Với mỗi process i trong ld_processes:
 *     1. load(path[i])  → tạo pcb_t, đọc instruction stream từ file
 *     2. Tạo krnl_t mới, kế thừa cấu hình từ global os (queues, RAM, SWAP)
 *     3. Chờ đến đúng start_time[i] (next_slot loop)
 *     4. init_mm(krnl->mm) → khởi tạo page table 5 cấp cho process này
 *     5. Gán krnl->mram, mswp → process biết nơi cấp phát frame
 *     6. add_proc(proc) → thêm vào MLQ ready queue
 *     7. next_slot → cho CPU cơ hội chạy
 *
 * Sau khi load xong: done=1 → các CPU thread sẽ thoát khi queue trống.
 *
 * LƯU Ý: Mỗi process có krnl_t RIÊNG nhưng chia sẻ cùng RAM và SWAP.
 * mm_struct trong krnl cũng là riêng → page table riêng cho mỗi process.
 */
static void *ld_routine(void *args)
{
#ifdef MM_PAGING
  struct memphy_struct  *mram        = ((struct mmpaging_ld_args *)args)->mram;
  struct memphy_struct **mswp        = ((struct mmpaging_ld_args *)args)->mswp;
  struct memphy_struct  *active_mswp = ((struct mmpaging_ld_args *)args)->active_mswp;
  struct timer_id_t     *timer_id    = ((struct mmpaging_ld_args *)args)->timer_id;
#else
  struct timer_id_t *timer_id = (struct timer_id_t *)args;
#endif
  int i = 0;

  /* Khởi tạo kernel page table (chỉ trong MM64 mode) */
#ifdef MM64
  os.krnl_pgd = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
  os.krnl_p4d = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
  os.krnl_pud = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
  os.krnl_pmd = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
  os.krnl_pt  = malloc(PAGING64_MAX_PGN * sizeof(addr_t));

  /* Điền chain pointer (địa chỉ mảng cấp dưới — chưa phải PTE chuẩn) */
  for (i = 0; i < PAGING64_MAX_PGN; i++) {
    os.krnl_pgd[i] = (addr_t)&os.krnl_p4d;
    os.krnl_p4d[i] = (addr_t)&os.krnl_pud;
    os.krnl_pud[i] = (addr_t)&os.krnl_pmd;
    os.krnl_pmd[i] = (addr_t)&os.krnl_pt;
    os.krnl_pt[i]  = 0;
  }
#else
  os.krnl_pgd = malloc(PAGING_MAX_PGN * sizeof(uint32_t));
#endif

  i = 0;
  printf("ld_routine\n");

  while (i < num_processes) {
    /* Tạo PCB cho process i */
    struct pcb_t *proc = load(ld_processes.path[i]);
    struct krnl_t *krnl = malloc(sizeof(struct krnl_t));
    *krnl = os;           /* kế thừa scheduler queues, kernel page table, ... */
    proc->krnl = krnl;

#ifdef MLQ_SCHED
    proc->prio = ld_processes.prio[i]; /* gán priority cho MLQ */
#endif

    /* Chờ đúng thời điểm nạp process */
    while (current_time() < ld_processes.start_time[i])
      next_slot(timer_id);

#ifdef MM_PAGING
    /* Khởi tạo bộ nhớ ảo riêng cho process này */
    krnl->mm = malloc(sizeof(struct mm_struct));
    init_mm(krnl->mm, proc);     /* tạo VMA0, 5 mảng page directory rỗng */
    krnl->mram = mram;            /* dùng chung RAM với các process khác */
    krnl->mswp = mswp;
    krnl->active_mswp    = active_mswp;
    krnl->active_mswp_id = 0;
#endif

    printf("\tLoaded a process at %s, PID: %d PRIO: %ld\n",
           ld_processes.path[i], proc->pid, ld_processes.prio[i]);

    add_proc(proc);               /* đưa vào MLQ ready queue */
    free(ld_processes.path[i]);
    i++;
    next_slot(timer_id);
  }

  free(ld_processes.path);
  free(ld_processes.start_time);
  done = 1; /* báo cho CPU threads: không còn process nào sẽ được load */
  detach_event(timer_id);
  pthread_exit(NULL);
}

/*
 * read_config: Đọc file cấu hình OS simulator.
 *
 * ĐỊNH DẠNG FILE:
 *   Dòng 1: time_slot  num_cpus  num_processes
 *   Dòng 2 (MM_PAGING only): memramsz memswpsz[0..3]
 *           (nếu thiếu dòng này: dùng giá trị mặc định)
 *   Dòng 3..N+2: start_time  process_name  [priority]
 *
 * XỬ LÝ BACKWARD COMPATIBILITY:
 *   Dòng 2 có thể là dòng cấu hình memory MỚI (5 số: RAM + 4 SWAP)
 *   hoặc là dòng process ĐẦU TIÊN (legacy format, không có memory config).
 *   → has_pending_proc_line: nếu dòng 2 là dòng process, lưu lại để parse sau.
 *
 * CHẾ ĐỘ DETERMINISTIC (OSSIM_DETERMINISTIC=1):
 *   Ép num_cpus=1 để tránh non-determinism do multi-thread scheduling.
 *   Giúp output khớp với expected output trong test.
 */
static void read_config(const char *path)
{
  FILE *file;
  int has_pending_proc_line = 0;
  char pending_proc_line[256];

  if ((file = fopen(path, "r")) == NULL) {
    printf("Cannot find configure file at %s\n", path);
    exit(1);
  }

  fscanf(file, "%d %d %d\n", &time_slot, &num_cpus, &num_processes);

  /* Chế độ deterministic: 1 CPU để tránh output khác nhau giữa các lần chạy */
  if (getenv("OSSIM_DETERMINISTIC") != NULL &&
      strcmp(getenv("OSSIM_DETERMINISTIC"), "1") == 0)
    num_cpus = 1;
	ld_processes.path = (char**)malloc(sizeof(char*) * num_processes);
	ld_processes.start_time = (unsigned long*)
		malloc(sizeof(unsigned long) * num_processes);
#ifdef MM_PAGING
	int sit;
#ifdef MM_FIXED_MEMSZ
	/* We provide here a back compatible with legacy OS simulatiom config file
         * In which, it have no addition config line for Mema, keep only one line
	 * for legacy info 
         *  [time slice] [N = Number of CPU] [M = Number of Processes to be run]
         */
        memramsz  =  0x100000000;
        memswpsz[0] = 0x1000000;
	for(sit = 1; sit < PAGING_MAX_MMSWP; sit++)
		memswpsz[sit] = 0;
#else
		/*
		 * Backward-compatible parsing for paging memory config:
		 * - New format includes a dedicated memory line:
		 *     MEM_RAM_SZ MEM_SWP0_SZ MEM_SWP1_SZ MEM_SWP2_SZ MEM_SWP3_SZ
		 * - Legacy format omits this line, so we use defaults and treat the
		 *   next line as the first process entry.
		 */
		memramsz = 268435456UL;
		memswpsz[0] = 16777216UL;
		for (sit = 1; sit < PAGING_MAX_MMSWP; sit++)
			memswpsz[sit] = 0;

		if (fgets(pending_proc_line, sizeof(pending_proc_line), file) != NULL) {
			unsigned long cfg_ram, cfg_swp0, cfg_swp1, cfg_swp2, cfg_swp3;
			int cfg_items = sscanf(
				pending_proc_line,
				"%lu %lu %lu %lu %lu",
				&cfg_ram,
				&cfg_swp0,
				&cfg_swp1,
				&cfg_swp2,
				&cfg_swp3
			);

			if (cfg_items == 5) {
				memramsz = cfg_ram;
				memswpsz[0] = cfg_swp0;
				memswpsz[1] = cfg_swp1;
				memswpsz[2] = cfg_swp2;
				memswpsz[3] = cfg_swp3;
				has_pending_proc_line = 0;
			} else {
				has_pending_proc_line = 1;
			}
		}
#endif
#endif

#ifdef MLQ_SCHED
	ld_processes.prio = (unsigned long*)
		malloc(sizeof(unsigned long) * num_processes);
#endif
	int i;
	for (i = 0; i < num_processes; i++) {
		ld_processes.path[i] = (char*)malloc(sizeof(char) * 100);
		ld_processes.path[i][0] = '\0';
		strcat(ld_processes.path[i], "input/proc/");
		char proc[100];
#ifdef MLQ_SCHED
		{
			int scan_res;
			if (i == 0 && has_pending_proc_line)
				scan_res = sscanf(pending_proc_line, "%lu %99s %lu", &ld_processes.start_time[i], proc, &ld_processes.prio[i]);
			else
				scan_res = fscanf(file, "%lu %99s %lu\n", &ld_processes.start_time[i], proc, &ld_processes.prio[i]);

			if (scan_res != 3) {
				printf("Invalid process config line at index %d in %s\n", i, path);
				exit(1);
			}
		}
#else
		{
			int scan_res;
			if (i == 0 && has_pending_proc_line)
				scan_res = sscanf(pending_proc_line, "%lu %99s", &ld_processes.start_time[i], proc);
			else
				scan_res = fscanf(file, "%lu %99s\n", &ld_processes.start_time[i], proc);

			if (scan_res != 2) {
				printf("Invalid process config line at index %d in %s\n", i, path);
				exit(1);
			}
		}
#endif
		strcat(ld_processes.path[i], proc);
	}
}

/*
 * main: Hàm khởi động OS simulator.
 *
 * Nhận 1 argument: tên file cấu hình (ví dụ "os_0_mlq_paging")
 * → đường dẫn đầy đủ: "input/os_0_mlq_paging"
 *
 * THỨ TỰ KHỞI TẠO:
 *   1. setvbuf: tắt buffering stdout → in ngay, không delay
 *   2. read_config: đọc time_slot, num_cpus, num_processes, memramsz, ...
 *   3. attach_event: đăng ký CPU và loader với timer
 *   4. start_timer: bắt đầu đếm tick
 *   5. init_memphy(RAM, SWAP): cấp phát storage arrays, tạo free frame lists
 *   6. init_scheduler: khởi tạo MLQ queues
 *   7. pthread_create(ld): loader thread
 *   8. pthread_create(cpu[i]): N CPU threads
 *   9. pthread_join: chờ tất cả threads kết thúc
 *   10. stop_timer: dừng timer thread
 */
int main(int argc, char *argv[])
{
  /* Tắt buffering để output in ngay — quan trọng cho multi-thread debug */
  setvbuf(stdout, NULL, _IONBF, 0);

  if (argc != 2) {
    printf("Usage: os [path to configure file]\n");
    return 1;
  }

  /* Xây dựng đường dẫn đầy đủ: "input/" + argv[1] */
  char path[100];
  path[0] = '\0';
  strcat(path, "input/");
  strcat(path, argv[1]);
  read_config(path);

  /* Cấp phát mảng thread và argument cho các CPU */
  pthread_t *cpu = (pthread_t *)malloc(num_cpus * sizeof(pthread_t));
  struct cpu_args *args = (struct cpu_args *)malloc(sizeof(struct cpu_args) * num_cpus);
  pthread_t ld;

  /* Đăng ký event với timer: mỗi CPU + loader đều cần 1 event slot */
  int i;
  for (i = 0; i < num_cpus; i++) {
    args[i].timer_id = attach_event();
    args[i].id = i;
  }
  struct timer_id_t *ld_event = attach_event();
  start_timer(); /* bắt đầu timer thread đếm tick */

#ifdef MM_PAGING
  int rdmflag = 1; /* RAM là random-access device */

  struct memphy_struct mram;
  struct memphy_struct mswp[PAGING_MAX_MMSWP];

  /* Khởi tạo RAM: cấp phát storage[], tạo free frame list */
  init_memphy(&mram, memramsz, rdmflag);

  /* Khởi tạo các SWAP device (có thể có kích thước 0 = không dùng) */
  int sit;
  for (sit = 0; sit < PAGING_MAX_MMSWP; sit++)
    init_memphy(&mswp[sit], memswpsz[sit], rdmflag);

  /* Đóng gói tất cả tham số MM vào 1 struct để truyền vào ld_routine */
  struct mmpaging_ld_args *mm_ld_args = malloc(sizeof(struct mmpaging_ld_args));
  mm_ld_args->timer_id      = ld_event;
  mm_ld_args->mram          = (struct memphy_struct *)&mram;
  mm_ld_args->mswp          = (struct memphy_struct **)&mswp;
  mm_ld_args->active_mswp   = (struct memphy_struct *)&mswp[0];
  mm_ld_args->active_mswp_id = 0;
#endif

  init_scheduler(); /* khởi tạo MLQ queues */

  /* Tạo loader thread và N CPU threads */
#ifdef MM_PAGING
  pthread_create(&ld, NULL, ld_routine, (void *)mm_ld_args);
#else
  pthread_create(&ld, NULL, ld_routine, (void *)ld_event);
#endif
  for (i = 0; i < num_cpus; i++)
    pthread_create(&cpu[i], NULL, cpu_routine, (void *)&args[i]);

  /* Chờ tất cả threads kết thúc (join) */
  for (i = 0; i < num_cpus; i++)
    pthread_join(cpu[i], NULL);
  pthread_join(ld, NULL);

  stop_timer(); /* dừng timer thread */
  return 0;
}



