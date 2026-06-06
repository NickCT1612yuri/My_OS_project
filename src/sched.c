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
 * FILE: sched.c
 * MỤC ĐÍCH: Triển khai thuật toán lập lịch tiến trình.
 *
 * HAI CHẾ ĐỘ:
 * ┌─────────────────────────────────────────────────────────┐
 * │ [MLQ_SCHED bật] Multi-Level Queue Scheduling            │
 * │  - 140 hàng đợi (priority 0..139), mỗi hàng một mức.   │
 * │  - Priority 0 = cao nhất (ưu tiên nhất).               │
 * │  - Mỗi mức được phân bổ số "slot" = MAX_PRIO - prio.   │
 * │    Ví dụ: prio=0 → slot=140, prio=139 → slot=1.        │
 * │  - Scheduler chọn tiến trình ở mức ưu tiên cao nhất    │
 * │    còn slot. Khi hết slot tất cả mức → reset tất cả.   │
 * │  - Thiết kế "stateful": slot[] là trạng thái toàn cục, │
 * │    được chia sẻ giữa các lần gọi get_mlq_proc().        │
 * └─────────────────────────────────────────────────────────┘
 * ┌─────────────────────────────────────────────────────────┐
 * │ [MLQ_SCHED tắt] FIFO Scheduling đơn giản               │
 * │  - ready_queue: tiến trình mới vào                     │
 * │  - run_queue  : tiến trình bị preempt, ưu tiên hơn     │
 * │  - Lấy từ run_queue trước, sau đó ready_queue.         │
 * └─────────────────────────────────────────────────────────┘
 *
 * THREAD SAFETY: queue_lock bảo vệ mọi thao tác với queue.
 * ============================================================
 */
#include "queue.h"
#include "sched.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

/* ready_queue  : tiến trình đã sẵn sàng chạy (cho FIFO mode) */
static struct queue_t ready_queue;
/* run_queue    : tiến trình bị preempt, sẽ được ưu tiên lấy lại */
static struct queue_t run_queue;
/* queue_lock   : mutex bảo vệ mọi thao tác đọc/ghi queue */
static pthread_mutex_t queue_lock;
/* running_list : các tiến trình đang thực sự chạy trên CPU */
static struct queue_t running_list;

#ifdef MLQ_SCHED
/* mlq_ready_queue[i]: hàng đợi cho mức ưu tiên i (i=0..MAX_PRIO-1) */
static struct queue_t mlq_ready_queue[MAX_PRIO];
/* slot[i]: số lần tiến trình mức i còn được lấy ra trước khi reset.
 * slot[i] = MAX_PRIO - i → mức 0 có 140 slot, mức 139 có 1 slot.
 * Khi tất cả slot đều hết → reset (công bằng lại từ đầu). */
static int slot[MAX_PRIO];
#endif

/* queue_empty: kiểm tra xem còn tiến trình nào chờ chạy không.
 * Trả về 0 nếu CÒN tiến trình (-1 trong MLQ mode), 1 nếu rỗng.
 * Dùng trong cpu_routine để quyết định có nên tiếp tục chạy không. */
int queue_empty(void)
{
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if (!empty(&mlq_ready_queue[prio]))
			return -1; /* còn ít nhất 1 tiến trình */
#endif
	return (empty(&ready_queue) && empty(&run_queue));
}

/* init_scheduler: khởi tạo tất cả hàng đợi về trạng thái rỗng.
 * Gọi một lần duy nhất trong main() trước khi tạo CPU threads. */
void init_scheduler(void)
{
#ifdef MLQ_SCHED
	int i;
	for (i = 0; i < MAX_PRIO; i++) {
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i; /* slot ban đầu: ưu tiên cao → nhiều slot hơn */
	}
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
	running_list.size = 0;
	pthread_mutex_init(&queue_lock, NULL);
}

#ifdef MLQ_SCHED
/*
 * ── MLQ SCHEDULER ─────────────────────────────────────────
 *
 * THUẬT TOÁN get_mlq_proc() (lấy tiến trình tiếp theo):
 *
 *   Bước 1: Duyệt từ prio=0 đến 139, tìm mức có tiến trình
 *           VÀ còn slot > 0.
 *           - has_ready: đánh dấu có ít nhất 1 tiến trình nào đó
 *           - selected_prio: mức ưu tiên được chọn
 *
 *   Bước 2: Nếu không tìm được mức nào có slot > 0 (mà vẫn còn
 *           tiến trình) → RESET toàn bộ slot về giá trị ban đầu.
 *           Sau đó chọn mức ưu tiên cao nhất có tiến trình.
 *
 *   Bước 3: dequeue từ mức được chọn, trừ slot[selected_prio]--,
 *           thêm vào running_list.
 *
 * VÍ DỤ VỚI 2 TIẾN TRÌNH (prio=0 và prio=15):
 *   slot[0]=140, slot[15]=125 (ban đầu)
 *   → Luôn chọn prio=0 (có slot cao hơn AND priority cao hơn)
 *   → Sau 140 lần chọn prio=0, slot[0]=0 → reset tất cả
 *   → Tiếp tục chọn prio=0 lại...
 */
struct pcb_t *get_mlq_proc(void)
{
	struct pcb_t *proc = NULL;

	pthread_mutex_lock(&queue_lock);
	{
		int i;
		int has_ready = 0;
		int selected_prio = -1;

		/* Tìm mức ưu tiên cao nhất có tiến trình VÀ còn slot */
		for (i = 0; i < MAX_PRIO; i++) {
			if (!empty(&mlq_ready_queue[i])) {
				has_ready = 1;
				if (slot[i] > 0) {
					selected_prio = i;
					break; /* chọn mức cao nhất có thể */
				}
			}
		}

		/* Không có mức nào còn slot? → reset và thử lại */
		if (selected_prio == -1 && has_ready) {
			for (i = 0; i < MAX_PRIO; i++)
				slot[i] = MAX_PRIO - i; /* reset toàn bộ */

			for (i = 0; i < MAX_PRIO; i++) {
				if (!empty(&mlq_ready_queue[i])) {
					selected_prio = i;
					break;
				}
			}
		}

		if (selected_prio != -1) {
			proc = dequeue(&mlq_ready_queue[selected_prio]);
			slot[selected_prio]--; /* tiêu thụ 1 slot của mức này */
			enqueue(&running_list, proc);
		}
	}
	pthread_mutex_unlock(&queue_lock);

	return proc;
}

/* put_mlq_proc: đưa tiến trình đang chạy TRỞ LẠI hàng đợi (preempt).
 * Xóa khỏi running_list, thêm vào mlq_ready_queue[proc->prio].
 * Cập nhật con trỏ krnl để proc biết queue nào đang dùng. */
void put_mlq_proc(struct pcb_t *proc)
{
	/* cập nhật các con trỏ queue trong kernel của tiến trình */
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);              /* rời running list */
	enqueue(&mlq_ready_queue[proc->prio], proc);  /* vào lại ready queue */
	pthread_mutex_unlock(&queue_lock);
}

/* add_mlq_proc: thêm tiến trình MỚI (vừa load) vào hàng đợi.
 * Không cần xóa khỏi running_list (vì chưa bao giờ chạy).
 * Tiến trình mới ngay lập tức cạnh tranh ở mức prio của nó. */
void add_mlq_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

/* Wrapper functions — cho phép gọi get/put/add_proc() độc lập với
 * chế độ scheduler (MLQ hay FIFO). cpu_routine và ld_routine chỉ
 * gọi get_proc/put_proc/add_proc, không cần biết chế độ scheduler. */
struct pcb_t *get_proc(void) { return get_mlq_proc(); }
void put_proc(struct pcb_t *proc) { return put_mlq_proc(proc); }
void add_proc(struct pcb_t *proc) { return add_mlq_proc(proc); }

/* sched_queue_lock / sched_queue_unlock: Expose queue_lock for external
 * modules (e.g., sys_mem.c) that need to safely traverse the queues.
 * Called around running_list / mlq_ready_queue accesses in syscall handlers. */
void sched_queue_lock(void)   { pthread_mutex_lock(&queue_lock); }
void sched_queue_unlock(void) { pthread_mutex_unlock(&queue_lock); }
#else
/*
 * ── FIFO SCHEDULER (MLQ_SCHED tắt) ───────────────────────
 *
 * Hai hàng đợi:
 *   run_queue   : tiến trình bị preempt (đang giữa chừng) → ưu tiên hơn
 *   ready_queue : tiến trình mới nạp vào → chờ lượt
 *
 * Khi CPU cần proc tiếp theo → lấy từ run_queue trước,
 * nếu rỗng mới lấy từ ready_queue.
 */

/* get_proc: lấy tiến trình tiếp theo để CPU chạy.
 * Ưu tiên run_queue (tiến trình đang bị gián đoạn) hơn ready_queue.
 * Thêm proc được chọn vào running_list để tracking.
 * Trả về NULL nếu cả hai queue đều rỗng. */
struct pcb_t *get_proc(void)
{
	struct pcb_t *proc = NULL;

	pthread_mutex_lock(&queue_lock);
	if (!empty(&run_queue))
		proc = dequeue(&run_queue);    /* tiến trình bị preempt — ưu tiên hơn */
	else
		proc = dequeue(&ready_queue);  /* tiến trình mới */

	if (proc != NULL)
		enqueue(&running_list, proc);  /* đánh dấu đang chạy */
	pthread_mutex_unlock(&queue_lock);

	return proc;
}

/* put_proc: trả tiến trình đang chạy về run_queue (bị preempt).
 * Xóa khỏi running_list, thêm vào run_queue để chờ được chạy lại.
 * Cập nhật con trỏ krnl để proc biết queue hiện tại. */
void put_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue  = &ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc); /* rời running list */
	enqueue(&run_queue, proc);       /* vào run_queue để được lấy lại */
	pthread_mutex_unlock(&queue_lock);
}

/* add_proc: thêm tiến trình MỚI (vừa load từ file) vào ready_queue.
 * Tiến trình chưa bao giờ chạy nên không cần xóa khỏi running_list. */
void add_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue  = &ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

/* sched_queue_lock / sched_queue_unlock: Expose queue_lock cho các module
 * bên ngoài (ví dụ sys_mem.c) cần duyệt queue an toàn trong syscall handler. */
void sched_queue_lock(void)   { pthread_mutex_lock(&queue_lock); }
void sched_queue_unlock(void) { pthread_mutex_unlock(&queue_lock); }
#endif


