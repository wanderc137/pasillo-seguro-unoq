#ifndef PASILLO_H
#define PASILLO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#define PS_MAX_URL 512
#define PS_MAX_PATH 512
#define PS_MAX_JSON 8192
#define PS_MAX_TRACKS 24
#define PS_MAX_CLIENTS 16
#define PS_MAX_JPEG (4 * 1024 * 1024)
#define PS_PROC_W 192
#define PS_PROC_H 144
#define PS_VERSION "1.0.0"

typedef enum { PS_TRANSPORT_USB = 0, PS_TRANSPORT_LAN = 1 } PsTransport;
typedef enum { PS_RISK_GREEN = 0, PS_RISK_AMBER = 1, PS_RISK_RED = 2 } PsRisk;

typedef struct {
    int port;
    char camera_url[PS_MAX_URL];
    char web_dir[PS_MAX_PATH];
    char data_dir[PS_MAX_PATH];
    char adb_path[PS_MAX_PATH];
    int proc_w;
    int proc_h;
    int stream_fps;
    int jpeg_quality;
    int red_frames;
    int green_frames;
    double risk_red;
    double risk_amber;
    int usb_camera_port;
    int usb_viewer_port;
    PsTransport transport;
    int fake;
} PsConfig;

typedef struct {
    uint8_t *data;
    size_t len;
    int width;
    int height;
    double ts;
} PsJpegFrame;

typedef struct {
    int id;
    int x, y, w, h;       /* current bbox in proc coords */
    int px, py, pw, ph;   /* previous bbox */
    double vx, vy;        /* proc px / s */
    double growth;        /* area growth / s */
    double ttc;           /* seconds to contact, 1e9 if unknown */
    double risk;
    int age;
    int missed;
    bool active;
} PsTrack;

typedef struct {
    uint8_t *gray;        /* proc_w * proc_h */
    uint8_t *prev;        /* proc_w * proc_h */
    uint8_t *diff;        /* proc_w * proc_h */
    uint8_t *mask;        /* proc_w * proc_h */
    int w;
    int h;
    int frame_no;
    double last_ts;
    PsTrack tracks[PS_MAX_TRACKS];
    int ntracks;
    int next_id;
    double risk;
    PsRisk level;
    int red_count;
    int green_count;
    int objects;
} PsVision;

typedef struct {
    uint8_t *jpeg;
    size_t jpeg_len;
    uint64_t seq;
    int width;
    int height;
    PsRisk risk;
    double risk_value;
    int objects;
    double fps;
    double latency_ms;
    char source[PS_MAX_URL];
    char state[32];
    char message[256];
    pthread_mutex_t lock;
    pthread_cond_t cond;
} PsHub;

typedef struct {
    PsConfig cfg;
    PsHub hub;
    PsVision vision;
    volatile int stop;
    volatile int running;
    pthread_t capture_thread;
    pthread_t adb_thread;
    pthread_mutex_t state_lock;
    char adb_state[32];
    char adb_detail[2048];
    char last_error[256];
    int adb_setup_done;
} PsApp;

/* util.c */
double ps_now(void);
long long ps_now_ms(void);
void ps_log(const char *fmt, ...);
void ps_json_escape(const char *in, char *out, size_t outlen);
int ps_normalize_url(const char *in, char *video, size_t vlen,
                     char *photo, size_t plen, char *err, size_t elen);
int ps_http_get(const char *url, uint8_t **out, size_t *outlen,
                int timeout_ms, char *err, size_t elen);
int ps_read_file(const char *path, uint8_t **out, size_t *outlen,
                 char *err, size_t elen);
int ps_write_file(const char *path, const void *data, size_t len);
void ps_sleep_ms(int ms);
int ps_parse_query(const char *query, const char *key, char *out, size_t outlen);
const char *ps_risk_name(PsRisk r);

/* jpeg.c */
int ps_jpeg_decode(const uint8_t *buf, size_t len, uint8_t **rgb, int *w, int *h,
                   char *err, size_t elen);
int ps_jpeg_encode(const uint8_t *rgb, int w, int h, int quality,
                   uint8_t **out, size_t *outlen, char *err, size_t elen);

/* camera.c */
typedef struct {
    int fd;
    char host[256];
    int port;
    char path[256];
    uint8_t *buf;
    size_t len;
    size_t cap;
    int header_done;
    double last_rx;
} PsCamera;

int ps_camera_open(PsCamera *cam, const char *url, char *err, size_t elen);
int ps_camera_read(PsCamera *cam, PsJpegFrame *frame, int timeout_ms,
                   char *err, size_t elen);
void ps_camera_close(PsCamera *cam);

/* risk.c */
double ps_track_risk(const PsTrack *t, int w, int h, const PsConfig *cfg);
PsRisk ps_risk_update(PsVision *v, double risk, const PsConfig *cfg);

/* vision.c */
int ps_vision_init(PsVision *v, int w, int h);
void ps_vision_free(PsVision *v);
void ps_vision_process(PsVision *v, uint8_t *rgb, int w, int h,
                       const PsConfig *cfg);
void ps_vision_draw_text(uint8_t *rgb, int w, int h, int x, int y,
                         const char *text, int scale,
                         uint8_t r, uint8_t g, uint8_t b);

/* adb.c */
int ps_adb_find(char *out, size_t outlen);
int ps_adb_run(const char *adb, char *const argv[], char *out, size_t outlen,
               int timeout_ms);
int ps_adb_status(const char *adb, char *state, size_t slen,
                  char *detail, size_t dlen);
int ps_adb_setup(const char *adb, int cam_port, int viewer_port,
                 char *msg, size_t mlen);
int ps_adb_teardown(const char *adb, char *msg, size_t mlen);

/* server.c */
int ps_server_run(PsApp *app);

/* main.c */
int ps_load_config(PsConfig *cfg, int argc, char **argv);
void ps_hub_update(PsHub *hub, const uint8_t *jpeg, size_t len, int w, int h,
                   PsRisk risk, double risk_value, int objects, double fps,
                   double latency_ms, const char *state, const char *message,
                   const char *source);

#endif
