/*
 * ============================================================
 * FILE: queue.h
 * MỤC ĐÍCH: Định nghĩa cấu trúc hàng đợi (queue) dùng để
 *           quản lý các tiến trình trong scheduler.
 *
 * THIẾT KẾ: Mảng cố định (không phải linked list).
 *   - Đơn giản, không cần malloc cho queue node.
 *   - Giới hạn tối đa MAX_QUEUE_SIZE tiến trình.
 *
 * DÙNG Ở ĐÂU:
 *   - ready_queue    : tiến trình chờ chạy (FIFO scheduler)
 *   - run_queue      : tiến trình bị preempt, sẽ chạy lại
 *   - running_list   : tiến trình đang chạy trên CPU
 *   - mlq_ready_queue[140]: hàng đợi MLQ, mỗi mức ưu tiên 1 queue
 * ============================================================
 */
#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

/* MAX_QUEUE_SIZE: số tiến trình tối đa trong một hàng đợi.
 * Nếu vượt quá, enqueue() sẽ bỏ qua (không thêm vào). */
#define MAX_QUEUE_SIZE 50

/* queue_t: mảng con trỏ PCB + biến đếm kích thước hiện tại.
 *   proc[]: mảng các con trỏ đến PCB
 *   size  : số phần tử hiện có trong queue (0..MAX_QUEUE_SIZE)
 */
struct queue_t {
	struct pcb_t *proc[MAX_QUEUE_SIZE];
	int size;
};

/* enqueue(q, proc): thêm tiến trình proc vào cuối hàng đợi q.
 * Nếu q đầy (size >= MAX_QUEUE_SIZE), không làm gì. */
void enqueue(struct queue_t *q, struct pcb_t *proc);

/* dequeue(q): lấy tiến trình ở đầu hàng đợi ra (FIFO).
 * Dịch chuyển toàn bộ mảng về trước 1 vị trí.
 * Trả về NULL nếu queue rỗng. */
struct pcb_t *dequeue(struct queue_t *q);

/* purgequeue(q, proc): xóa một tiến trình CỤ THỂ khỏi queue
 * (tìm theo con trỏ, không phải theo thứ tự).
 * Trả về proc nếu tìm thấy và xóa thành công, NULL nếu không có. */
struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc);

/* empty(q): trả về 1 nếu queue rỗng hoặc NULL, 0 nếu có phần tử. */
int empty(struct queue_t *q);

#endif

