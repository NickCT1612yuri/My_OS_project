#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: put a new process to queue [q] */
        if (q == NULL || proc == NULL)
                return;
        if (q->size >= MAX_QUEUE_SIZE)
                return;

        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        int i;
        struct pcb_t *proc;

        if (q == NULL || q->size <= 0)
                return NULL;

        proc = q->proc[0];
        for (i = 1; i < q->size; i++)
                q->proc[i - 1] = q->proc[i];
        q->size--;

        return proc;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
        int index;

        if (q == NULL || proc == NULL || q->size <= 0)
                return NULL;

        for (index = 0; index < q->size; index++) {
                if (q->proc[index] == proc) {
                        int shift;
                        for (shift = index + 1; shift < q->size; shift++)
                                q->proc[shift - 1] = q->proc[shift];
                        q->size--;
                        return proc;
                }
        }

        return NULL;
}