// Asgn 2: A simple HTTP server.
// By: Eugene Chou
//     Andrew Quinn
//     Brian Zhao

#include "asgn2_helper_funcs.h"
#include "connection.h"
#include "debug.h"
#include "response.h"
#include "request.h"
#include "queue.h"
#include <sys/types.h>
#include <sys/file.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <sys/stat.h>
#define UNUSED(x) (void) (x)

//pthread_mutex_t *FCLock;
queue_t *q = NULL;
pthread_mutex_t FCLock;

void handle_connection(int);
void handle_get(conn_t *);
void handle_put(conn_t *);
void handle_unsupported(conn_t *);

void *work_thread(void *args) {
    //gets fd from queue, puts in struct
    uintptr_t confd = 0;
    while (1) {
        queue_pop(q, (void **) &confd);
        // does instruction
        handle_connection((int) confd);
    }

    return args;
}

int main(int argc, char **argv) {
    //lock
    int rc = pthread_mutex_init(&FCLock, NULL);
    assert(!rc);
    // global queue
    q = queue_new(4096);

    // 2.  parsing portion
    int port, opt = 0;
    uintptr_t num_thread = 4;
    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
        case 't': num_thread = atoi(optarg); break;
        }
    }
    for (; optind < argc; optind++) {
        port = atoi(argv[optind]);
    }

    // 3. initialize socket
    signal(SIGPIPE, SIG_IGN);
    Listener_Socket sock;
    listener_init(&sock, port);

    // create threads
    pthread_t *thread = (pthread_t *) calloc(num_thread, sizeof(pthread_t));
    for (uintptr_t i = 0; i < num_thread; i++) {
        pthread_create(&thread[i], NULL, work_thread, NULL);
    }

    // 4. accept signals
    while (1) {
        intptr_t connfd = listener_accept(&sock);
        if (connfd < 0) {
            continue;
        }
        queue_push(q, (void *) connfd);
    }

    return EXIT_SUCCESS;
}

void handle_connection(int fd) {
    conn_t *conn = conn_new(fd);
    const Response_t *res = conn_parse(conn);

    if (res != NULL) {
        conn_send_response(conn, res);
    } else {
        const Request_t *req = conn_get_request(conn);
        if (req == &REQUEST_GET) {
            handle_get(conn);
        } else if (req == &REQUEST_PUT) {
            handle_put(conn);
        } else {
            handle_unsupported(conn);
        }
    }
    close(fd);
    conn_delete(&conn);
}

void handle_get(conn_t *conn) {
    uint64_t size = 0;
    uint16_t code = 0;
    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;
    char *id = conn_get_header(conn, "Request-Id");
    if (!id) {
        id = "0";
    }
    // Open the file..
    int fd = open(uri, O_RDONLY);
    if (fd < 0) {
        if (access(uri, F_OK) == -1) {
            res = &RESPONSE_NOT_FOUND;
            goto out;
        } else if (errno == EACCES || errno == EISDIR || errno == ENOENT) {
            res = &RESPONSE_FORBIDDEN;
            goto out;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            goto out;
        }
    }
    flock(fd, LOCK_SH);
    // get the file size
    struct stat st;
    fstat(fd, &st);
    size = st.st_size;
    // check directory
    if ((st.st_mode & S_IFMT) == S_IFDIR) {
        res = &RESPONSE_FORBIDDEN;
        goto out;
    }
    // send file
    conn_send_file(conn, fd, size);
out:
    if (!res) {
        code = 200;
    } else {
        code = response_get_code(res);
        conn_send_response(conn, res);
    }
    fprintf(stderr, "GET,%s,%d,%s\n", conn_get_uri(conn), code, id);
    close(fd);
}

void handle_unsupported(conn_t *conn) {
    // send responses
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
}

void handle_put(conn_t *conn) {
    //acquire FCL
    pthread_mutex_lock(&FCLock);
    char *id = conn_get_header(conn, "Request-Id");
    if (!id) {
        id = "0";
    }
    int lock_check = 0;
    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;

    // Check if file already exists before opening it.
    bool existed = access(uri, F_OK) == 0;

    // Open the file
    int fd = open(uri, O_CREAT | O_WRONLY, 0600);

    if (fd < 0) {
        lock_check = 1;
        if (errno == EACCES || errno == EISDIR || errno == ENOENT) {
            res = &RESPONSE_FORBIDDEN;
            goto out;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
            goto out;
        }
    }
    //flock the file
    flock(fd, LOCK_EX);
    //release FCL
    ftruncate(fd, 0);
    pthread_mutex_unlock(&FCLock);

    res = conn_recv_file(conn, fd);
    if (res == NULL && existed) {
        res = &RESPONSE_OK;
    } else if (res == NULL && !existed) {
        res = &RESPONSE_CREATED;
    }

out:
    fprintf(stderr, "PUT,%s,%d,%s\n", conn_get_uri(conn), response_get_code(res), id);
    conn_send_response(conn, res);
    if (lock_check) {
        pthread_mutex_unlock(&FCLock);
    }
    //close releases flock
    close(fd);
}
