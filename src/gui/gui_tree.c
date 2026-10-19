/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#include "gui_tree.h"
#include "gui_backend.h"
#include <xxwidgets/xxwidgets_process.h>
#include <xxfclib/threads/xx_threads.h>
#include <stdlib.h>
#include <string.h>

typedef struct tree_buffer {
    xxwidgets_tree_node *nodes;
    xxwidgets_tree_color *colors;
    size_t count, capacity, detections;
} tree_buffer;

/* The worker scans with its own progress object, work. xx_pd_struct has plain
 * counters, so the UI never reads it: an observer bound on the worker copies
 * it into snapshot under lock and returns the UI's stop request, as
 * xxwidgets_process_dialog expects. Everything below lock is shared. */
typedef struct scan_job {
    const char *path;
    int type_id;
    tree_buffer buffer;
    char *report, *type;
    int ok;
    xx_pd_struct work;
    xx_mutex_t *lock;
    xx_pd_struct snapshot;
    int stop, done;
} scan_job;

static int collect_node(size_t parent, const char *text, const char *category, void *user)
{
    tree_buffer *buffer = user;
    char *copy;
    if (buffer->count == buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity * 2 : 16;
        xxwidgets_tree_node *nodes;
        xxwidgets_tree_color *colors;
        if (capacity < buffer->capacity || capacity > SIZE_MAX / sizeof(*nodes)) return 0;
        nodes = realloc(buffer->nodes, capacity * sizeof(*nodes));
        if (!nodes) return 0;
        buffer->nodes = nodes;
        colors = realloc(buffer->colors, capacity * sizeof(*colors));
        if (!colors) return 0;
        buffer->colors = colors; buffer->capacity = capacity;
    }
    copy = malloc(strlen(text) + 1);
    if (!copy) return 0;
    strcpy(copy, text);
    buffer->nodes[buffer->count] = (xxwidgets_tree_node){parent, copy, 1};
    buffer->colors[buffer->count++] = xxwidgets_scan_result_color(category);
    if (category[0]) ++buffer->detections;
    return 1;
}

/* Worker thread, after each progress update and at each stop check. */
static bool observe_progress(const xx_pd_struct *progress, void *user)
{
    scan_job *job = user;
    int stop;
    xx_mutex_lock(job->lock);
    job->snapshot = *progress;
    stop = job->stop;
    xx_mutex_unlock(job->lock);
    return stop != 0;
}

static int scan_worker(void *arg)
{
    scan_job *job = arg;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_progress, job);
    job->ok = gui_backend_scan_tree(job->path, job->type_id, collect_node, &job->buffer,
                                    &job->report, &job->type, &job->work);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock);
    job->snapshot = job->work;
    job->done = 1;
    xx_mutex_unlock(job->lock);
    return 0;
}

/* UI thread: an xxwidgets_process_update_fn. */
static xxwidgets_status update_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    scan_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop_requested) job->stop = 1;
    *progress = job->snapshot;
    *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}

int gui_tree_scan(xxwidgets_widget *owner, xxwidgets_scan_panel *panel, const char *path, int type_id,
                  char **type, int *cancelled)
{
    scan_job job = {0};
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    int ok;
    if (type) *type = NULL;
    if (cancelled) *cancelled = 0;
    job.path = path; job.type_id = type_id;
    job.work = xx_pd_init(); job.snapshot = job.work;
    job.lock = xx_mutex_create();
    if (!job.lock) return 0;
    /* The script interpreter recurses. macOS gives secondary threads only
     * 512 KiB, so ask for at least what a main thread gets (Windows reserves). */
    thread = xx_thread_create_ex(scan_worker, &job, 8u * 1024u * 1024u);
    if (!thread) { xx_mutex_destroy(job.lock); return 0; }
    /* BUSY (inside an input callback or another modal form) returns before
     * starting, so the scan simply runs to its end; any other failure has
     * already passed the worker a stop request. Either way, join it. */
    if (owner) (void)xxwidgets_process_dialog(owner, "Scanning", &progress, update_progress, &job);
    ok = xx_thread_join(thread, NULL) && job.ok;
    xx_mutex_destroy(job.lock);
    if (cancelled) *cancelled = job.work.is_stop;
    if (ok && panel) ok = xxwidgets_scan_panel_set_tree(panel, job.buffer.nodes, job.buffer.colors,
        job.buffer.count, job.buffer.detections, job.report) == XXWIDGETS_OK;
    if (ok && type) { *type = job.type; job.type = NULL; }
    for (size_t i = 0; i < job.buffer.count; ++i) free((void *)job.buffer.nodes[i].text);
    free(job.buffer.nodes); free(job.buffer.colors);
    gui_backend_free(job.report); gui_backend_free(job.type);
    return ok;
}
