#include <stdlib.h>
#include "wq.h"
#include "utlist.h"
#include <stdio.h>

/* Initializes a work queue WQ. */
void wq_init(wq_t *wq) {

    pthread_mutex_init(&wq->lock, NULL);
    pthread_cond_init(&wq->cond, NULL);

    pthread_mutex_lock(&wq->lock);
    wq->size = 0;
    wq->head = NULL;

    pthread_mutex_unlock(&wq->lock);
}

/* Remove an item from the WQ. This function should block until there
 * is at least one item on the queue. */
int wq_pop(wq_t *wq) {

    /* TODO: Make me blocking and thread-safe! */

    // Lock
    pthread_mutex_lock(&wq->lock);

    // Suspended wait
    while(wq->head == NULL){
        pthread_cond_wait(&wq->cond, &wq->lock);
    }

    wq_item_t *wq_item = wq->head;
    int client_socket_fd = wq->head->client_socket_fd;
    wq->size--;
    DL_DELETE(wq->head, wq->head);

    free(wq_item);

    //Unlock
    pthread_mutex_unlock(&wq->lock);

    return client_socket_fd;
}

/* Add ITEM to WQ. */
void wq_push(wq_t *wq, int client_socket_fd) {

    /* TODO: Make me thread-safe! */

    // Lock
    pthread_mutex_lock(&wq->lock);

    wq_item_t *wq_item = calloc(1, sizeof(wq_item_t));
    wq_item->client_socket_fd = client_socket_fd;
    DL_APPEND(wq->head, wq_item);
    wq->size++;

    // Signal if req
    pthread_cond_signal(&wq->cond);

    // Unlock
    pthread_mutex_unlock(&wq->lock);
}
