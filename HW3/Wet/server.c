#include "request.h"
#include "segel.h"
#include <pthread.h>

//
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c

typedef enum schedalg {
    BLOCK,
    DROP_TAIL,
    DROP_HEAD,
    DROP_RANDOM
} schedalg_e;

typedef enum remove_type {
    HEAD,
    TAIL
} remove_type_e;

typedef enum retval {
    SUCCESS,
    MEMORY_ERROR,
    QUEUE_IS_FULL,
    QUEUE_IS_EMPTY,
    NOT_ENOUGH_ELEMENTS
} retval_e;

typedef struct session {
    int connection_fd;
    struct timeval arrival_time;
} session_t;

typedef struct cyclic_queue {
    size_t size;
    ssize_t head;
    ssize_t tail;
    session_t* elements_array;
} cyclic_queue_t;

typedef struct jobs_manager {
    schedalg_e schedalg;
    size_t max_accepted_count;
    size_t waiting_count;
    size_t running_count;
    pthread_mutex_t mutex;
    pthread_cond_t produce;
    pthread_cond_t consume;
    cyclic_queue_t waiting_jobs;
    pthread_t* threads;
} jobs_manager_t;

jobs_manager_t global_job_manager;

retval_e init_cyclic_queue(cyclic_queue_t* queue, size_t size)
{
    queue->elements_array = (session_t*)malloc(sizeof(*queue->elements_array) * size);
    if (queue->elements_array == NULL) {
        return MEMORY_ERROR;
    }
    queue->size = size;
    queue->head = -1;
    queue->tail = -1;
    return SUCCESS;
}

void free_cyclic_queue(cyclic_queue_t* queue)
{
    free(queue->elements_array);
}

retval_e add_queue_element(cyclic_queue_t* queue, session_t element)
{
    if ((queue->tail + 1 == queue->head) || (queue->head == 0 && queue->tail == queue->size - 1)) {
        return QUEUE_IS_FULL;
    }
    if (queue->head == -1) {
        queue->head = 0;
    }
    queue->tail = (queue->tail + 1) % queue->size;
    queue->elements_array[queue->tail] = element;
    return SUCCESS;
}

retval_e remove_queue_element(cyclic_queue_t* queue, session_t* element, remove_type_e type)
{
    if (queue->head == -1) {
        return QUEUE_IS_EMPTY;
    }
    if (element != NULL) {
        if (type == HEAD)
            *element = queue->elements_array[queue->head];
        else
            *element = queue->elements_array[queue->tail];
    }
    // has single element
    if (queue->head == queue->tail) {
        queue->head = -1;
        queue->tail = -1;
    } else if (type == HEAD) {
        queue->head = (queue->head + 1) % queue->size;
    } else {
        queue->tail = (queue->tail == 0) ? queue->size - 1 : queue->tail - 1;
    }
    return SUCCESS;
}

void request_handle_thread(size_t job_id);

retval_e init_jobs_manager(jobs_manager_t* jobs_manager, size_t max_accepted_count, size_t threads_num, schedalg_e schedalg)
{
    jobs_manager->schedalg = schedalg;
    jobs_manager->max_accepted_count = max_accepted_count;
    jobs_manager->waiting_count = 0;
    jobs_manager->running_count = 0;
    pthread_mutex_init(&jobs_manager->mutex, NULL);
    pthread_cond_init(&jobs_manager->consume, NULL);
    pthread_cond_init(&jobs_manager->produce, NULL);
    jobs_manager->threads = (pthread_t*)malloc(sizeof(*jobs_manager->threads) * threads_num);
    if (jobs_manager->threads == NULL) {
        return MEMORY_ERROR;
    }
    retval_e retval = init_cyclic_queue(&jobs_manager->waiting_jobs, max_accepted_count);
    if (retval != SUCCESS) {
        return retval;
    }
    for (size_t id = 0; id < threads_num; id++) {
        if (pthread_create(&jobs_manager->threads[id], NULL, (void* (*)(void*))request_handle_thread, (void*)id) != 0) {
            fprintf(stderr, "Error: pthread_create\n");
            exit(1);
        }
    }
    return SUCCESS;
}

void random_drop_connections(cyclic_queue_t* queue, size_t* elements_num)
{
    session_t session;
    srand(time(NULL));
    if (*elements_num == 1) {
        remove_queue_element(queue, &session, HEAD);
        Close(session.connection_fd);
        *elements_num = 0;
        return;
    }
    size_t remove_elements_num = *elements_num / 2;
    *elements_num -= remove_elements_num;
    for (size_t i = 0; i < remove_elements_num; i++) {
        if (rand() % 2 == HEAD) {
            remove_queue_element(queue, &session, HEAD);
        } else {
            remove_queue_element(queue, &session, TAIL);
        }
        Close(session.connection_fd);
    }
}

void add_request(jobs_manager_t* jobs_manager, session_t session)
{
    session_t head_session;
    pthread_mutex_lock(&jobs_manager->mutex);
    while (jobs_manager->waiting_count + jobs_manager->running_count == jobs_manager->max_accepted_count) {
        switch (jobs_manager->schedalg) {
        case BLOCK:
            pthread_cond_wait(&jobs_manager->produce, &jobs_manager->mutex);
            break;
        case DROP_TAIL:
            Close(session.connection_fd);
            goto unlock_and_exit;
            break;
        case DROP_HEAD:
            remove_queue_element(&jobs_manager->waiting_jobs, &head_session, HEAD);
            Close(head_session.connection_fd);
            jobs_manager->waiting_count--;
            break;
        case DROP_RANDOM:
            if (jobs_manager->waiting_count == 0) {
                Close(session.connection_fd);
                goto unlock_and_exit;
            }
            random_drop_connections(&jobs_manager->waiting_jobs, &jobs_manager->waiting_count);
            break;
        }
    }
    add_queue_element(&jobs_manager->waiting_jobs, session);
    jobs_manager->waiting_count++;
    pthread_cond_signal(&jobs_manager->consume);
unlock_and_exit:
    pthread_mutex_unlock(&jobs_manager->mutex);
}

void get_request(jobs_manager_t* jobs_manager, session_t* session)
{
    pthread_mutex_lock(&jobs_manager->mutex);
    while (jobs_manager->waiting_count == 0) {
        pthread_cond_wait(&jobs_manager->consume, &jobs_manager->mutex);
    }
    remove_queue_element(&jobs_manager->waiting_jobs, session, HEAD);
    jobs_manager->waiting_count--;
    jobs_manager->running_count++;
    // Pay attention we don't wake the main thread to add more jobs because
    // the total number of accepted jobs didn't change
    pthread_mutex_unlock(&jobs_manager->mutex);
}

void notify_request_finished(jobs_manager_t* jobs_manager)
{
    pthread_mutex_lock(&jobs_manager->mutex);
    jobs_manager->running_count--;
    pthread_cond_signal(&jobs_manager->produce);
    pthread_mutex_unlock(&jobs_manager->mutex);
}

void request_handle_thread(size_t thread_id)
{
    session_t session;
    request_stat_t request_stat = { 0 };
    request_stat.thread_id = thread_id;
    while (1) {
        get_request(&global_job_manager, &session);
        gettimeofday(&request_stat.dispatch_time, NULL);
        request_stat.arrival_time = session.arrival_time;
        timersub(&request_stat.dispatch_time, &request_stat.arrival_time, &request_stat.dispatch_time);
        requestHandle(session.connection_fd, &request_stat);
        Close(session.connection_fd);
        notify_request_finished(&global_job_manager);
    }
}

void getargs(int* port, int* threads_num, int* queue_size, schedalg_e* schedalg, int argc, char* argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <port> <threads> <queue_size> <schedalg>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *threads_num = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
    if (strcmp(argv[4], "block") == 0) {
        *schedalg = BLOCK;
    } else if (strcmp(argv[4], "dt") == 0) {
        *schedalg = DROP_TAIL;
    } else if (strcmp(argv[4], "dh") == 0) {
        *schedalg = DROP_HEAD;
    } else if (strcmp(argv[4], "random") == 0) {
        *schedalg = DROP_RANDOM;
    }
}

int main(int argc, char* argv[])
{
    session_t session;
    schedalg_e schedalg;
    struct sockaddr_in clientaddr;
    int listenfd, clientlen, port, threads_num, queue_size;

    getargs(&port, &threads_num, &queue_size, &schedalg, argc, argv);
    if (init_jobs_manager(&global_job_manager, queue_size, threads_num, schedalg) != SUCCESS) {
        fprintf(stderr, "Error: init_jobs_manager\n");
        exit(1);
    }
    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        session.connection_fd = Accept(listenfd, (SA*)&clientaddr, (socklen_t*)&clientlen);
        gettimeofday(&session.arrival_time, NULL);
        add_request(&global_job_manager, session);
    }
}
