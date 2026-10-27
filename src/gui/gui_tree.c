/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#include "gui_tree.h"
#include "app/cdie_translation.h"
#include "gui_backend.h"
#include <xxwidgets/die_engine/xxwidgets_die_signature_editor.h>
#include <xxwidgets/xxwidgets_process.h>
#include <stdio.h>
#include <xxfclib/threads/xx_threads.h>
#include <stdlib.h>
#include <string.h>

enum { MARK_SIGNATURE = 0, MARK_INFO, MARK_COUNT };

typedef struct tree_buffer {
    xxwidgets_tree_node *nodes;
    xxwidgets_tree_color *colors;
    char **names, **signatures; /* per node; NULL on group rows */
    unsigned int *marks;
    size_t count, capacity, detections;
} tree_buffer;

/* The rows of the tree on screen, and what a button press waits on. */
static struct {
    tree_buffer rows;
    char *path;
    int type_id;
    const xxwidgets_widget *tree;
    size_t pending_node;
    int pending_mark;
} g_marks = {{0}, NULL, 0, NULL, 0, -1};

static void free_rows(tree_buffer *rows)
{
    for (size_t i = 0; i < rows->count; ++i) {
        free((void *)rows->nodes[i].text);
        free(rows->names[i]); free(rows->signatures[i]);
    }
    free(rows->nodes); free(rows->colors); free(rows->names); free(rows->signatures); free(rows->marks);
    memset(rows, 0, sizeof(*rows));
}

static char *copy_text(const char *text)
{
    char *copy = text ? malloc(strlen(text) + 1) : NULL;
    if (copy) strcpy(copy, text);
    return copy;
}

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

static int grow(void **array, size_t size, size_t capacity)
{
    void *grown = realloc(*array, capacity * size);
    if (!grown) return 0;
    *array = grown;
    return 1;
}

static int collect_node(size_t parent, const char *text, const char *category, const gui_backend_detection *detection, void *user)
{
    tree_buffer *buffer = user;
    char *copy, *name = NULL, *signature = NULL;
    if (buffer->count == buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity * 2 : 16;
        if (capacity < buffer->capacity || capacity > SIZE_MAX / sizeof(xxwidgets_tree_node)) return 0;
        if (!grow((void **)&buffer->nodes, sizeof(*buffer->nodes), capacity) ||
            !grow((void **)&buffer->colors, sizeof(*buffer->colors), capacity) ||
            !grow((void **)&buffer->names, sizeof(*buffer->names), capacity) ||
            !grow((void **)&buffer->signatures, sizeof(*buffer->signatures), capacity) ||
            !grow((void **)&buffer->marks, sizeof(*buffer->marks), capacity)) return 0;
        buffer->capacity = capacity;
    }
    copy = copy_text(text);
    if (detection) {
        name = copy_text(detection->name);
        signature = detection->signature ? copy_text(detection->signature) : NULL;
    }
    if (!copy || (detection && (!name || (detection->signature && !signature)))) {
        free(copy); free(name); free(signature);
        return 0;
    }
    buffer->nodes[buffer->count] = (xxwidgets_tree_node){parent, copy, 1};
    buffer->colors[buffer->count] = xxwidgets_scan_result_color(category);
    buffer->names[buffer->count] = name;
    buffer->signatures[buffer->count] = signature;
    buffer->marks[buffer->count++] = (signature ? 1u << MARK_SIGNATURE : 0) | (name ? 1u << MARK_INFO : 0);
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
    if (owner) (void)xxwidgets_process_dialog(owner, CDIE_TR("Scanning"), &progress, update_progress, &job);
    ok = xx_thread_join(thread, NULL) && job.ok;
    xx_mutex_destroy(job.lock);
    if (cancelled) *cancelled = job.work.is_stop;
    if (ok && panel) ok = xxwidgets_scan_panel_set_tree(panel, job.buffer.nodes, job.buffer.colors,
        job.buffer.count, job.buffer.detections, job.report) == XXWIDGETS_OK;
    if (ok && panel) {
        /* Labels as DIE draws them; the rows stay for the button presses. */
        static const char *const labels[MARK_COUNT] = {"S", "?"};
        xxwidgets_widget *tree = xxwidgets_scan_panel_control(panel, XXWIDGETS_SCAN_PANEL_RESULTS);
        char *copy = copy_text(path);
        if (copy && xxwidgets_treeview_set_marks(tree, labels, MARK_COUNT, job.buffer.marks, job.buffer.count) == XXWIDGETS_OK) {
            gui_tree_reset();
            g_marks.rows = job.buffer;
            memset(&job.buffer, 0, sizeof(job.buffer));
            g_marks.path = copy;
            g_marks.type_id = type_id;
            g_marks.tree = tree;
        } else free(copy);
    }
    if (ok && type) { *type = job.type; job.type = NULL; }
    free_rows(&job.buffer);
    gui_backend_free(job.report); gui_backend_free(job.type);
    return ok;
}

void gui_tree_reset(void)
{
    free_rows(&g_marks.rows);
    free(g_marks.path);
    g_marks.path = NULL;
    g_marks.tree = NULL;
    g_marks.pending_mark = -1;
}

int gui_tree_mark_event(const xxwidgets_scan_panel *panel, const xxwidgets_event *event)
{
    const xxwidgets_widget *tree = panel ? xxwidgets_scan_panel_control(panel, XXWIDGETS_SCAN_PANEL_RESULTS) : NULL;
    if (!event || event->type != XXWIDGETS_EVENT_CLICK || !tree || event->widget != tree) return 0;
    /* Only presses on the rows this module put there, in this tree. */
    if (tree == g_marks.tree && event->value >= 0 && (size_t)event->value < g_marks.rows.count &&
        event->x >= 0 && event->x < MARK_COUNT && (g_marks.rows.marks[event->value] >> event->x & 1u)) {
        g_marks.pending_node = (size_t)event->value;
        g_marks.pending_mark = event->x;
    }
    return 1;
}

typedef struct run_target {
    const char *path, *signature;
    int type_id;
} run_target;

/* The signature editor's Run: its result text belongs to the editor's heap. */
static xxwidgets_status run_edited(const char *source, const char *path, char **result, void *user)
{
    const run_target *target = user;
    char *text = NULL;
    int ok;
    (void)path;
    ok = gui_backend_run_signature(target->path, target->type_id, target->signature, source, &text);
    *result = copy_text(text && text[0] ? text : ok ? CDIE_TR("Nothing detected.") : CDIE_TR("Cannot scan the file."));
    gui_backend_free(text);
    return *result ? XXWIDGETS_OK : XXWIDGETS_OUT_OF_MEMORY;
}

/* "Name" for the search, percent-encoded as a query value. */
static char *search_url(const char *name)
{
    static const char prefix[] = "https://www.google.com/search?q=", hex[] = "0123456789ABCDEF";
    char *url = malloc(sizeof(prefix) + strlen(name) * 3), *out;
    if (!url) return NULL;
    memcpy(url, prefix, sizeof(prefix) - 1);
    out = url + sizeof(prefix) - 1;
    for (const unsigned char *s = (const unsigned char *)name; *s; ++s) {
        if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || strchr("-_.~", *s)) *out++ = (char)*s;
        else { *out++ = '%'; *out++ = hex[*s >> 4]; *out++ = hex[*s & 15]; }
    }
    *out = 0;
    return url;
}

const char *gui_tree_mark_process(xxwidgets_widget *owner)
{
    static char message[512];
    size_t node = g_marks.pending_node;
    int mark = g_marks.pending_mark;
    xxwidgets_status status;
    g_marks.pending_mark = -1;
    if (mark < 0 || node >= g_marks.rows.count) return NULL;
    if (mark == MARK_SIGNATURE) {
        const char *signature = g_marks.rows.signatures[node];
        char *source = gui_backend_signature_source(signature), *title;
        run_target target = {g_marks.path, signature, g_marks.type_id};
        const char *base = signature, *s;
        if (!source) {
            cdie_translation_snprintf(message, sizeof(message), CDIE_N_("The source of %s is not available: it is compiled or no longer loaded."), signature);
            return message;
        }
        for (s = signature; *s; ++s) if (*s == '/' || *s == '\\') base = s + 1;
        title = malloc(strlen(CDIE_TR("Signature")) + strlen(base) + 3);
        if (!title) { gui_backend_free(source); return xxwidgets_status_string(XXWIDGETS_OUT_OF_MEMORY); }
        sprintf(title, "%s: %s", CDIE_TR("Signature"), base);
        status = xxwidgets_die_signature_editor_dialog(owner, title, source, signature, run_edited, &target);
        free(title);
        gui_backend_free(source);
        return status == XXWIDGETS_OK ? NULL : xxwidgets_status_string(status);
    }
    {
        const char *name = g_marks.rows.names[node];
        char *info = gui_backend_info_text(name), *url;
        if (info) {
            status = xxwidgets_text_dialog(owner, name, info);
            gui_backend_free(info);
            return status == XXWIDGETS_OK ? NULL : xxwidgets_status_string(status);
        }
        url = search_url(name);
        if (!url) return xxwidgets_status_string(XXWIDGETS_OUT_OF_MEMORY);
        status = xxwidgets_open_url(owner, url);
        /* Without a browser, say where to look instead. */
        if (status == XXWIDGETS_OK) cdie_translation_snprintf(message, sizeof(message), CDIE_N_("No description of %s is bundled; searching the web for it."), name);
        else cdie_translation_snprintf(message, sizeof(message), CDIE_N_("No description of %s is bundled. Search the web: %s"), name, url);
        free(url);
        return message;
    }
}
