#define _GNU_SOURCE
#include "pasillo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <poll.h>

int ps_adb_find(char *out, size_t outlen) {
    const char *env = getenv("PS_ADB_PATH");
    if (env && *env && access(env, X_OK) == 0) {
        snprintf(out, outlen, "%s", env);
        return 0;
    }
    const char *candidates[] = {
        "/home/arduino/.arduino15/packages/arduino/tools/adb/32.0.0/adb",
        "/usr/bin/adb",
        "/usr/local/bin/adb",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], X_OK) == 0) {
            snprintf(out, outlen, "%s", candidates[i]);
            return 0;
        }
    }
    /* search PATH */
    const char *path = getenv("PATH");
    if (path) {
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s", path);
        char *save = NULL;
        for (char *p = strtok_r(tmp, ":", &save); p; p = strtok_r(NULL, ":", &save)) {
            char full[PS_MAX_PATH];
            snprintf(full, sizeof(full), "%s/adb", p);
            if (access(full, X_OK) == 0) {
                snprintf(out, outlen, "%s", full);
                return 0;
            }
        }
    }
    out[0] = '\0';
    return -1;
}

int ps_adb_run(const char *adb, char *const argv[], char *out, size_t outlen,
               int timeout_ms) {
    if (out && outlen) out[0] = '\0';
    if (!adb || !argv) return -1;
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execv(adb, argv);
        _exit(127);
    }
    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    size_t used = 0;
    double deadline = ps_now() + (double)timeout_ms / 1000.0;
    int status = 0;
    bool exited = false;
    while (ps_now() < deadline) {
        struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
        int pr = poll(&pfd, 1, 100);
        if (pr > 0) {
            char buf[512];
            ssize_t n = read(pipefd[0], buf, sizeof(buf));
            if (n > 0) {
                if (used + (size_t)n + 1 < outlen) {
                    memcpy(out + used, buf, (size_t)n);
                    used += (size_t)n;
                    out[used] = '\0';
                }
            } else if (n == 0) {
                break;
            }
        }
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) {
            exited = true;
            status = st;
            break;
        }
    }
    close(pipefd[0]);
    if (!exited) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        return -2;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

int ps_adb_status(const char *adb, char *state, size_t slen,
                  char *detail, size_t dlen) {
    snprintf(state, slen, "none");
    snprintf(detail, dlen, "adb no encontrado");
    if (!adb || !*adb) return -1;
    char out[4096];
    char *argv[] = { (char *)adb, "devices", "-l", NULL };
    int rc = ps_adb_run(adb, argv, out, sizeof(out), 5000);
    if (rc < 0) {
        snprintf(state, slen, "error");
        snprintf(detail, dlen, "no se pudo ejecutar adb");
        return -1;
    }
    snprintf(detail, dlen, "%s", out);
    /* trim */
    char *nl = strchr(detail, '\n');
    if (nl && nl[1]) {
        /* keep all; JSON escape handles newlines */
    }
    if (strstr(out, "unauthorized")) {
        snprintf(state, slen, "unauthorized");
    } else if (strstr(out, "offline")) {
        snprintf(state, slen, "offline");
    } else {
        char *line = out;
        while (line && *line) {
            char *e = strchr(line, '\n');
            if (e) *e = '\0';
            if (strstr(line, "\tdevice") || strstr(line, " device ")) {
                snprintf(state, slen, "device");
                break;
            }
            line = e ? e + 1 : NULL;
        }
    }
    return 0;
}

int ps_adb_setup(const char *adb, int cam_port, int viewer_port,
                 char *msg, size_t mlen) {
    char state[32], detail[2048];
    if (ps_adb_status(adb, state, sizeof(state), detail, sizeof(detail)) != 0) {
        snprintf(msg, mlen, "adb no disponible");
        return -1;
    }
    if (strcmp(state, "device") != 0) {
        snprintf(msg, mlen, "telefono en estado %s; activa depuracion USB y acepta RSA", state);
        return -1;
    }
    char fwd[128];
    snprintf(fwd, sizeof(fwd), "tcp:%d", cam_port);
    char fwd2[128];
    snprintf(fwd2, sizeof(fwd2), "tcp:8080");
    char rev1[128], rev2[128];
    snprintf(rev1, sizeof(rev1), "tcp:%d", viewer_port);
    snprintf(rev2, sizeof(rev2), "tcp:%d", viewer_port);

    char *a1[] = { (char *)adb, "forward", fwd, fwd2, NULL };
    char out1[1024];
    int r1 = ps_adb_run(adb, a1, out1, sizeof(out1), 5000);
    char *a2[] = { (char *)adb, "reverse", rev1, rev2, NULL };
    char out2[1024];
    int r2 = ps_adb_run(adb, a2, out2, sizeof(out2), 5000);
    if (r1 != 0) {
        snprintf(msg, mlen, "fallo adb forward: %s", out1);
        return -1;
    }
    if (r2 != 0) {
        snprintf(msg, mlen, "forward OK; fallo adb reverse: %s", out2);
        return -1;
    }
    snprintf(msg, mlen, "USB listo: camara 127.0.0.1:%d, pagina 127.0.0.1:%d",
             cam_port, viewer_port);
    return 0;
}

int ps_adb_teardown(const char *adb, char *msg, size_t mlen) {
    if (!adb || !*adb) {
        snprintf(msg, mlen, "adb no encontrado");
        return -1;
    }
    char out[1024];
    char *a1[] = { (char *)adb, "forward", "--remove", "tcp:8080", NULL };
    char *a2[] = { (char *)adb, "reverse", "--remove", "tcp:8020", NULL };
    ps_adb_run(adb, a1, out, sizeof(out), 3000);
    ps_adb_run(adb, a2, out, sizeof(out), 3000);
    snprintf(msg, mlen, "forward/reverse eliminados");
    return 0;
}
