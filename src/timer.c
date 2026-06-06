/*
 * ============================================================
 * FILE: timer.c
 * MỤC ĐÍCH: Điều phối các luồng (thread) hoạt động theo
 *           "time slot" — mỗi time slot là một đơn vị thời gian.
 *
 * CƠ CHẾ HOẠT ĐỘNG:
 *   Có 1 timer thread + N device threads (N CPU + 1 loader).
 *   Mỗi time slot diễn ra theo 3 bước:
 *
 *   [Bước 1] Các device thread làm việc của mình (chạy tiến trình,
 *            load tiến trình...), rồi gọi next_slot() để báo "xong".
 *
 *   [Bước 2] Timer thread chờ đến khi TẤT CẢ device đã báo "done"
 *            (hoặc "finished" nếu device dừng hẳn).
 *
 *   [Bước 3] Timer tăng _time++, in "Time slot N", rồi ra hiệu cho
 *            tất cả device tiếp tục time slot mới.
 *
 * ĐỒNG BỘ HÓA (Synchronization):
 *   Mỗi device có một timer_id_t chứa 2 cặp mutex+cond:
 *   ┌─────────────────────────────────────────────────────┐
 *   │ event_lock + event_cond:                            │
 *   │   Device báo "done" cho timer bằng cách:            │
 *   │   set done=1 → signal event_cond                    │
 *   │   Timer chờ event_cond để biết device đã xong.      │
 *   │                                                     │
 *   │ timer_lock + timer_cond:                            │
 *   │   Timer báo "next slot" cho device bằng cách:       │
 *   │   set done=0 → signal timer_cond                    │
 *   │   Device chờ timer_cond sau khi báo done.           │
 *   └─────────────────────────────────────────────────────┘
 *
 * BIẾN TRẠNG THÁI:
 *   done = 1  → device đã xong slot hiện tại
 *   fsh  = 1  → device đã kết thúc vĩnh viễn (detach_event)
 * ============================================================
 */
#include "timer.h"
#include <stdio.h>
#include <stdlib.h>

/* Thread của timer (chạy timer_routine) */
static pthread_t _timer;

/* timer_id_container_t: wrapper để lưu timer_id_t trong linked list.
 * dev_list là danh sách tất cả device đang đăng ký với timer. */
struct timer_id_container_t {
	struct timer_id_t id;
	struct timer_id_container_t *next;
};

static struct timer_id_container_t *dev_list = NULL;

/* _time: biến đếm time slot hiện tại (0, 1, 2, ...) */
static uint64_t _time;

static int timer_started = 0; /* bảo vệ: không thêm device sau khi timer bắt đầu */
static int timer_stop = 0;    /* flag dừng timer từ bên ngoài (stop_timer) */


/* timer_routine: hàm chạy trong timer thread.
 *
 * Vòng lặp chính:
 *   1. Duyệt danh sách device, chờ từng device báo "done" (hoặc "fsh").
 *   2. Nếu TẤT CẢ device đều "fsh" (kết thúc) → thoát vòng lặp.
 *   3. Ngược lại: tăng _time, in header slot mới,
 *      rồi giải phóng tất cả device (reset done=0, signal timer_cond).
 */
static void *timer_routine(void *args)
{
	printf("Time slot %3llu\n", (unsigned long long)current_time());
	while (!timer_stop) {
		int fsh = 0;   /* số device đã kết thúc vĩnh viễn */
		int event = 0; /* tổng số device */

		/* Bước 1: chờ từng device xong slot hiện tại */
		struct timer_id_container_t *temp;
		for (temp = dev_list; temp != NULL; temp = temp->next) {
			pthread_mutex_lock(&temp->id.event_lock);
			/* chờ device đặt done=1 hoặc fsh=1 */
			while (!temp->id.done && !temp->id.fsh) {
				pthread_cond_wait(&temp->id.event_cond,
				                  &temp->id.event_lock);
			}
			if (temp->id.fsh)
				fsh++; /* device này đã dừng hẳn */
			event++;
			pthread_mutex_unlock(&temp->id.event_lock);
		}

		/* Bước 2: nếu tất cả device đã dừng → kết thúc mô phỏng */
		if (fsh == event)
			break;

		/* Bước 3: chuyển sang time slot mới */
		_time++;
		printf("Time slot %3llu\n", (unsigned long long)current_time());

		/* Đánh thức tất cả device tiếp tục */
		for (temp = dev_list; temp != NULL; temp = temp->next) {
			pthread_mutex_lock(&temp->id.timer_lock);
			temp->id.done = 0;             /* reset trạng thái */
			pthread_cond_signal(&temp->id.timer_cond); /* đánh thức device */
			pthread_mutex_unlock(&temp->id.timer_lock);
		}
	}
	pthread_exit(args);
}

/* next_slot: gọi bởi mỗi device (CPU thread, loader thread) khi
 * nó đã hoàn thành công việc trong time slot hiện tại.
 *
 * Hàm này:
 *   1. Báo cho timer "tôi đã xong" (done=1, signal event_cond)
 *   2. Tự chặn lại và chờ timer báo "slot mới bắt đầu"
 *      (chờ done trở về 0 qua timer_cond)
 *
 * Khi hàm này trả về, device đang ở đầu time slot mới.
 */
void next_slot(struct timer_id_t *timer_id)
{
	/* Báo timer: device đã xong slot này */
	pthread_mutex_lock(&timer_id->event_lock);
	timer_id->done = 1;
	pthread_cond_signal(&timer_id->event_cond);
	pthread_mutex_unlock(&timer_id->event_lock);

	/* Chờ timer cho phép tiếp tục (done reset về 0) */
	pthread_mutex_lock(&timer_id->timer_lock);
	while (timer_id->done) {
		pthread_cond_wait(&timer_id->timer_cond,
		                  &timer_id->timer_lock);
	}
	pthread_mutex_unlock(&timer_id->timer_lock);
}

/* current_time: trả về time slot hiện tại (0-indexed). */
uint64_t current_time()
{
	return _time;
}

/* start_timer: khởi động timer thread.
 * Phải gọi TRƯỚC khi tạo CPU/loader thread.
 * Sau khi gọi start_timer, không thể attach_event() nữa. */
void start_timer()
{
	timer_started = 1;
	pthread_create(&_timer, NULL, timer_routine, NULL);
}

/* detach_event: device báo nó đã kết thúc VĨNH VIỄN (không chạy nữa).
 * Gọi khi CPU thread hay loader thread thoát khỏi vòng lặp chính.
 * Đặt fsh=1 để timer_routine biết device này không cần chờ nữa. */
void detach_event(struct timer_id_t *event)
{
	pthread_mutex_lock(&event->event_lock);
	event->fsh = 1;
	pthread_cond_signal(&event->event_cond);
	pthread_mutex_unlock(&event->event_lock);
}

/* attach_event: đăng ký một device mới với timer.
 * Trả về timer_id_t* để device dùng khi gọi next_slot/detach_event.
 * CHỈ gọi được TRƯỚC khi start_timer() (timer_started == 0).
 * Thêm vào đầu dev_list (stack, không phải queue). */
struct timer_id_t *attach_event()
{
	if (timer_started)
		return NULL; /* quá muộn, không thể thêm */

	struct timer_id_container_t *container =
		(struct timer_id_container_t *)malloc(
			sizeof(struct timer_id_container_t));
	container->id.done = 0;
	container->id.fsh = 0;
	pthread_cond_init(&container->id.event_cond, NULL);
	pthread_mutex_init(&container->id.event_lock, NULL);
	pthread_cond_init(&container->id.timer_cond, NULL);
	pthread_mutex_init(&container->id.timer_lock, NULL);

	/* thêm vào đầu danh sách */
	if (dev_list == NULL) {
		dev_list = container;
		dev_list->next = NULL;
	} else {
		container->next = dev_list;
		dev_list = container;
	}
	return &(container->id);
}

/* stop_timer: dừng timer thread và giải phóng tài nguyên.
 * Gọi từ main() sau khi tất cả CPU/loader thread đã pthread_join. */
void stop_timer()
{
	timer_stop = 1;
	pthread_join(_timer, NULL);
	/* giải phóng toàn bộ dev_list */
	while (dev_list != NULL) {
		struct timer_id_container_t *temp = dev_list;
		dev_list = dev_list->next;
		pthread_cond_destroy(&temp->id.event_cond);
		pthread_mutex_destroy(&temp->id.event_lock);
		pthread_cond_destroy(&temp->id.timer_cond);
		pthread_mutex_destroy(&temp->id.timer_lock);
		free(temp);
	}
}




