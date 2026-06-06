/*
 * ============================================================
 * FILE: queue.c
 * MỤC ĐÍCH: Triển khai các thao tác trên hàng đợi tiến trình.
 *
 * LƯU Ý VỀ THREAD SAFETY:
 *   Các hàm này KHÔNG tự lock mutex — việc bảo vệ bằng
 *   pthread_mutex_lock/unlock được thực hiện BÊN NGOÀI
 *   (trong sched.c) trước khi gọi các hàm này.
 * ============================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

/* empty: kiểm tra queue rỗng.
 * Trả về 1 (rỗng) nếu q == NULL hoặc q->size == 0. */
int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

/* enqueue: thêm tiến trình vào CUỐI queue.
 * Phức tạp: O(1) — chỉ ghi vào proc[size] rồi tăng size.
 *
 * queue sau: [ proc0, proc1, ..., procN, new_proc ]
 *                                          ^^^^^^^^ thêm vào đây
 */
void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        if (q == NULL || proc == NULL)
                return;
        if (q->size >= MAX_QUEUE_SIZE)
                return;

        q->proc[q->size] = proc;
        q->size++;
}

/* dequeue: lấy tiến trình ra từ ĐẦU queue (FIFO).
 * Sau khi lấy, dịch toàn bộ mảng về trước 1 vị trí.
 * Phức tạp: O(n) — phải dịch mảng.
 *
 * Trước: [ proc0, proc1, proc2, ... ]
 * Sau:   [ proc1, proc2, ...        ]  (proc0 được trả về)
 *
 * NOTE: Trong MLQ scheduler, dequeue KHÔNG được gọi trực tiếp
 *       cho mlq_ready_queue — mỗi priority level có queue riêng,
 *       scheduler chọn queue nào trước rồi mới dequeue từ đó.
 */
struct pcb_t *dequeue(struct queue_t *q)
{
        int i;
        struct pcb_t *proc;

        if (q == NULL || q->size <= 0)
                return NULL;

        proc = q->proc[0];                      /* lưu phần tử đầu */
        for (i = 1; i < q->size; i++)
                q->proc[i - 1] = q->proc[i];   /* dịch trái 1 vị trí */
        q->size--;

        return proc;
}

/* purgequeue: xóa một tiến trình CỤ THỂ (theo con trỏ) khỏi queue.
 * Dùng khi cần xóa một proc không ở đầu queue —
 * ví dụ: khi tiến trình chuyển từ running_list sang mlq_ready_queue.
 * Phức tạp: O(n) — tìm kiếm tuyến tính.
 *
 * Trả về proc nếu tìm thấy và xóa, NULL nếu không có trong queue.
 */
struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        int index;

        if (q == NULL || proc == NULL || q->size <= 0)
                return NULL;

        for (index = 0; index < q->size; index++) {
                if (q->proc[index] == proc) {
                        int shift;
                        /* dịch các phần tử sau index về trước 1 vị trí */
                        for (shift = index + 1; shift < q->size; shift++)
                                q->proc[shift - 1] = q->proc[shift];
                        q->size--;
                        return proc;
                }
        }

        return NULL; /* không tìm thấy */
}