/* libbbpresent.so: the app's end of the Bionic game's frame path (tools/android/present/
 * bb_present_proto.h). Listens on a unix socket, receives AHardwareBuffers and frames from the
 * game process and shows them with an ASurfaceControl child of the SurfaceView, scaled to fit.
 *
 * minSdk is 28: ASurfaceControl / ASurfaceTransaction (API 29) are resolved with dlsym; without
 * them frames are dropped (released at once) and start() says so. All state is behind one mutex.
 * The receive thread owns the socket; the display callbacks (any thread) answer RELEASE.
 */
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/surface_control.h>
#include <dlfcn.h>
#include <errno.h>
#include <jni.h>
#include <pthread.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "bb_present_proto.h"

#define TAG "BbPresent"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* API 29 entry points, from libandroid.so. */
static struct {
    ASurfaceControl* (*create_from_window)(ANativeWindow*, const char*);
    void (*release)(ASurfaceControl*);
    ASurfaceTransaction* (*txn_create)(void);
    void (*txn_delete)(ASurfaceTransaction*);
    void (*txn_apply)(ASurfaceTransaction*);
    void (*set_buffer)(ASurfaceTransaction*, ASurfaceControl*, AHardwareBuffer*, int);
    void (*set_geometry)(ASurfaceTransaction*, ASurfaceControl*, const ARect*, const ARect*, int32_t);
    void (*set_visibility)(ASurfaceTransaction*, ASurfaceControl*, int8_t);
    void (*set_transparency)(ASurfaceTransaction*, ASurfaceControl*, int8_t);
    void (*set_on_complete)(ASurfaceTransaction*, void*, void (*)(void*, ASurfaceTransactionStats*));
    int (*prev_release_fence)(ASurfaceTransactionStats*, ASurfaceControl*);
    void (*get_controls)(ASurfaceTransactionStats*, ASurfaceControl***, size_t*);
    void (*release_controls)(ASurfaceControl**);
    int ok;
} api;

static void load_api(void) {
    void* lib = dlopen("libandroid.so", RTLD_NOW);
    if (!lib) return;
#define SYM(field, name) if (!(*(void**)&api.field = dlsym(lib, name))) return
    SYM(create_from_window, "ASurfaceControl_createFromWindow");
    SYM(release, "ASurfaceControl_release");
    SYM(txn_create, "ASurfaceTransaction_create");
    SYM(txn_delete, "ASurfaceTransaction_delete");
    SYM(txn_apply, "ASurfaceTransaction_apply");
    SYM(set_buffer, "ASurfaceTransaction_setBuffer");
    SYM(set_geometry, "ASurfaceTransaction_setGeometry");
    SYM(set_visibility, "ASurfaceTransaction_setVisibility");
    SYM(set_transparency, "ASurfaceTransaction_setBufferTransparency");
    SYM(set_on_complete, "ASurfaceTransaction_setOnComplete");
    SYM(prev_release_fence, "ASurfaceTransactionStats_getPreviousReleaseFenceFd");
    SYM(get_controls, "ASurfaceTransactionStats_getASurfaceControls");
    SYM(release_controls, "ASurfaceTransactionStats_releaseASurfaceControls");
#undef SYM
    api.ok = 1;
}

static struct {
    pthread_mutex_t lock;
    int started;
    int server_fd;
    char path[108];
    pthread_t thread;
    /* Display. */
    ANativeWindow* window;
    ASurfaceControl* sc;
    int last_set; /* buffer index of the newest transaction, -1 none */
    /* Connection (session ids tell the display callbacks which connection they belong to). */
    int conn_fd;
    uint32_t session;
    uint32_t generation;
    uint32_t count;
    AHardwareBuffer* bufs[BB_PRESENT_MAX_BUFFERS];
    atomic_ullong frames;
} S = {.lock = PTHREAD_MUTEX_INITIALIZER, .server_fd = -1, .conn_fd = -1, .last_set = -1};

/* ---- lock held for everything below unless noted ---- */

static void send_release(uint32_t generation, uint32_t index, uint64_t frame, int fence) {
    bb_present_msg m = bb_present_make(BB_PRESENT_RELEASE);
    m.generation = generation;
    m.index = index;
    m.frame = frame;
    if (S.conn_fd >= 0) {
        int r = bb_present_send(S.conn_fd, &m, fence);
        if (r != 0) LOGE("release send failed: %d", r);
    }
    if (fence >= 0) close(fence);
}

static void drop_buffers(void) {
    for (uint32_t i = 0; i < BB_PRESENT_MAX_BUFFERS; ++i) {
        if (S.bufs[i]) AHardwareBuffer_release(S.bufs[i]);
        S.bufs[i] = NULL;
    }
    S.last_set = -1;
}

static void drop_surface_control(void) {
    if (S.sc) {
        /* The newest buffer is on the display until the control goes: its release has no fence. */
        if (S.last_set >= 0 && S.conn_fd >= 0)
            send_release(S.generation, (uint32_t)S.last_set, 0, -1);
        S.last_set = -1;
        api.release(S.sc);
        S.sc = NULL;
    }
}

static void make_surface_control(void) {
    if (!api.ok || !S.window || S.sc) return;
    S.sc = api.create_from_window(S.window, "bbpresent");
    if (!S.sc) LOGE("ASurfaceControl_createFromWindow failed");
    else LOGI("surface control created");
}

typedef struct {
    uint32_t session, generation, index; /* the buffer the transaction replaced; index == ~0: none */
    uint64_t frame;
} DoneCtx;

static void on_complete(void* ctx_, ASurfaceTransactionStats* stats) {
    DoneCtx* ctx = ctx_;
    int fence = -1;
    ASurfaceControl** controls = NULL;
    size_t n = 0;
    api.get_controls(stats, &controls, &n);
    if (n > 0) fence = api.prev_release_fence(stats, controls[0]);
    if (controls) api.release_controls(controls);
    pthread_mutex_lock(&S.lock);
    if (ctx->index != ~0u && ctx->session == S.session && S.conn_fd >= 0) {
        send_release(ctx->generation, ctx->index, ctx->frame, fence);
    } else if (fence >= 0) {
        close(fence);
    }
    pthread_mutex_unlock(&S.lock);
    free(ctx);
}

static void present_frame(const bb_present_msg* m, int acquire_fd) {
    if (m->generation != S.generation || m->index >= S.count || !S.bufs[m->index]) {
        if (acquire_fd >= 0) close(acquire_fd);
        return; /* a frame of a set we do not have: nothing to show, nothing to release */
    }
    atomic_fetch_add(&S.frames, 1);
    if (!S.sc || !api.ok) {
        /* No display now (surface lost, app paused): the frame is dropped; its own acquire fence
         * is the release (the game's writes are what the buffer still waits for). */
        send_release(m->generation, m->index, m->frame, acquire_fd);
        return;
    }
    int w = ANativeWindow_getWidth(S.window), h = ANativeWindow_getHeight(S.window);
    if (w <= 0 || h <= 0) { w = (int)m->width; h = (int)m->height; }
    /* Fit the buffer into the window keeping its aspect. */
    int dw = w, dh = (int)((int64_t)w * m->height / (m->width ? m->width : 1));
    if (dh > h) { dh = h; dw = (int)((int64_t)h * m->width / (m->height ? m->height : 1)); }
    ARect src = {0, 0, (int32_t)m->width, (int32_t)m->height};
    ARect dst = {(w - dw) / 2, (h - dh) / 2, (w - dw) / 2 + dw, (h - dh) / 2 + dh};
    DoneCtx* ctx = malloc(sizeof *ctx);
    if (!ctx) { send_release(m->generation, m->index, m->frame, acquire_fd); return; }
    ctx->session = S.session;
    ctx->generation = S.generation;
    ctx->index = S.last_set >= 0 ? (uint32_t)S.last_set : ~0u; /* replaced by this transaction */
    ctx->frame = 0;
    ASurfaceTransaction* t = api.txn_create();
    api.set_buffer(t, S.sc, S.bufs[m->index], acquire_fd); /* takes the fd */
    api.set_geometry(t, S.sc, &src, &dst, 0);
    api.set_transparency(t, S.sc, ASURFACE_TRANSACTION_TRANSPARENCY_OPAQUE);
    api.set_visibility(t, S.sc, ASURFACE_TRANSACTION_VISIBILITY_SHOW);
    api.set_on_complete(t, ctx, on_complete);
    api.txn_apply(t);
    api.txn_delete(t);
    S.last_set = (int)m->index;
}

static void close_connection(void) {
    if (S.conn_fd >= 0) { close(S.conn_fd); S.conn_fd = -1; }
    S.session++;
    S.last_set = -1;
    drop_buffers();
    S.count = 0;
    /* The control stays: the next game connection shows on the same surface. */
}

/* Serves one connection until it ends. Called without the lock. */
static void serve(int fd) {
    pthread_mutex_lock(&S.lock);
    S.conn_fd = fd;
    S.session++;
    S.generation = 0;
    S.count = 0;
    pthread_mutex_unlock(&S.lock);
    LOGI("game connected");
    for (;;) {
        bb_present_msg m;
        int fence = -1;
        int r = bb_present_recv(fd, &m, &fence, 0); /* no lock: blocks */
        if (r != 0) {
            if (r != -ECONNRESET) LOGE("receive failed: %d", r);
            break;
        }
        pthread_mutex_lock(&S.lock);
        int stop = 0;
        switch (m.type) {
        case BB_PRESENT_HELLO: {
            if (m.version != BB_PRESENT_VERSION || m.count == 0 || m.count > BB_PRESENT_MAX_BUFFERS) {
                LOGE("bad HELLO: version %u count %u", m.version, m.count);
                stop = 1;
                break;
            }
            drop_buffers();
            S.generation = m.generation;
            S.count = m.count;
            bb_present_msg a = bb_present_make(BB_PRESENT_HELLO);
            a.generation = m.generation;
            stop = bb_present_send(fd, &a, -1) != 0;
            break;
        }
        case BB_PRESENT_RESET:
            drop_buffers();
            S.generation = m.generation;
            S.count = m.count <= BB_PRESENT_MAX_BUFFERS ? m.count : 0;
            break;
        case BB_PRESENT_BUFFER: {
            AHardwareBuffer* b = NULL;
            /* The buffer is the very next datagram. */
            pthread_mutex_unlock(&S.lock);
            int rr = AHardwareBuffer_recvHandleFromUnixSocket(fd, &b);
            pthread_mutex_lock(&S.lock);
            if (rr != 0 || !b) { LOGE("receiving buffer %u failed: %d", m.index, rr); stop = 1; break; }
            if (m.generation == S.generation && m.index < S.count) {
                if (S.bufs[m.index]) AHardwareBuffer_release(S.bufs[m.index]);
                S.bufs[m.index] = b;
            } else {
                AHardwareBuffer_release(b);
            }
            break;
        }
        case BB_PRESENT_FRAME:
            present_frame(&m, fence);
            fence = -1;
            break;
        case BB_PRESENT_BYE:
            stop = 1;
            break;
        default:
            break;
        }
        pthread_mutex_unlock(&S.lock);
        if (fence >= 0) close(fence);
        if (stop) break;
    }
    pthread_mutex_lock(&S.lock);
    close_connection();
    pthread_mutex_unlock(&S.lock);
    LOGI("game disconnected");
}

static void* server_thread(void* unused) {
    (void)unused;
    for (;;) {
        int fd = accept4(S.server_fd, NULL, NULL, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR) continue;
            return NULL; /* closed by shutdown */
        }
        serve(fd);
    }
}

/* ---- JNI ---- */

JNIEXPORT jstring JNICALL Java_ro_cobrabm_fexdroid_BbPresent_nativeStart(JNIEnv* env, jclass cls,
        jstring jpath) {
    (void)cls;
    const char* path = (*env)->GetStringUTFChars(env, jpath, NULL);
    char msg[160];
    pthread_mutex_lock(&S.lock);
    if (!S.started) {
        load_api();
        if (strlen(path) >= sizeof S.path) {
            snprintf(msg, sizeof msg, "socket path too long");
            goto fail;
        }
        int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        struct sockaddr_un a;
        memset(&a, 0, sizeof a);
        a.sun_family = AF_UNIX;
        strcpy(a.sun_path, path);
        unlink(path);
        if (fd < 0 || bind(fd, (struct sockaddr*)&a, sizeof a) != 0 || listen(fd, 1) != 0) {
            snprintf(msg, sizeof msg, "cannot listen on %s: %s", path, strerror(errno));
            if (fd >= 0) close(fd);
            goto fail;
        }
        chmod(path, 0600);
        strcpy(S.path, path);
        S.server_fd = fd;
        S.started = 1;
        pthread_create(&S.thread, NULL, server_thread, NULL);
        LOGI("listening on %s, ASurfaceControl %s", path, api.ok ? "available" : "NOT available (Android 10+)");
    }
    pthread_mutex_unlock(&S.lock);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    return (*env)->NewStringUTF(env, "present running");
fail:
    pthread_mutex_unlock(&S.lock);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    return (*env)->NewStringUTF(env, msg);
}

JNIEXPORT void JNICALL Java_ro_cobrabm_fexdroid_BbPresent_nativeSetSurface(JNIEnv* env, jclass cls,
        jobject surface) {
    (void)cls;
    pthread_mutex_lock(&S.lock);
    drop_surface_control();
    if (S.window) { ANativeWindow_release(S.window); S.window = NULL; }
    if (surface) {
        S.window = ANativeWindow_fromSurface(env, surface);
        make_surface_control();
    }
    pthread_mutex_unlock(&S.lock);
}

JNIEXPORT jlong JNICALL Java_ro_cobrabm_fexdroid_BbPresent_nativeFrames(JNIEnv* env, jclass cls) {
    (void)env; (void)cls;
    return (jlong)atomic_load(&S.frames);
}

JNIEXPORT void JNICALL Java_ro_cobrabm_fexdroid_BbPresent_nativeShutdown(JNIEnv* env, jclass cls) {
    (void)env; (void)cls;
    pthread_mutex_lock(&S.lock);
    drop_surface_control();
    if (S.window) { ANativeWindow_release(S.window); S.window = NULL; }
    if (S.started) {
        shutdown(S.server_fd, SHUT_RDWR);
        close(S.server_fd);
        S.server_fd = -1;
        unlink(S.path);
        if (S.conn_fd >= 0) shutdown(S.conn_fd, SHUT_RDWR);
        S.started = 0;
    }
    pthread_mutex_unlock(&S.lock);
}
