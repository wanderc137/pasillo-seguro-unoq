#define _GNU_SOURCE
#include "pasillo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf jmp;
    char msg[JMSG_LENGTH_MAX];
} PsJpegError;

static void ps_jpeg_error_exit(j_common_ptr cinfo) {
    PsJpegError *e = (PsJpegError *)cinfo->err;
    (*cinfo->err->format_message)(cinfo, e->msg);
    longjmp(e->jmp, 1);
}

static void ps_jpeg_emit_message(j_common_ptr cinfo, int msg_level) {
    (void)cinfo;
    (void)msg_level;
}

int ps_jpeg_decode(const uint8_t *buf, size_t len, uint8_t **rgb, int *w, int *h,
                   char *err, size_t elen) {
    struct jpeg_decompress_struct cinfo;
    PsJpegError jerr;
    FILE *f = NULL;
    uint8_t *out = NULL;
    int created = 0;

    if (err && elen) err[0] = '\0';
    *rgb = NULL;
    *w = *h = 0;
    if (!buf || len < 4) {
        snprintf(err, elen, "JPEG vacio");
        return -1;
    }

    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = ps_jpeg_error_exit;
    jerr.pub.emit_message = ps_jpeg_emit_message;

    if (setjmp(jerr.jmp)) {
        if (created) jpeg_destroy_decompress(&cinfo);
        if (f) fclose(f);
        free(out);
        snprintf(err, elen, "%s", jerr.msg[0] ? jerr.msg : "error JPEG");
        return -1;
    }

    f = fmemopen((void *)buf, len, "rb");
    if (!f) {
        snprintf(err, elen, "no se pudo abrir el JPEG en memoria");
        return -1;
    }
    jpeg_create_decompress(&cinfo);
    created = 1;
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);
    if (cinfo.image_width == 0 || cinfo.image_height == 0 ||
        cinfo.image_width > 8192 || cinfo.image_height > 8192) {
        jpeg_destroy_decompress(&cinfo);
        fclose(f);
        snprintf(err, elen, "dimensiones JPEG invalidas");
        return -1;
    }
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    if (cinfo.output_components != 3) {
        jpeg_destroy_decompress(&cinfo);
        fclose(f);
        snprintf(err, elen, "JPEG no RGB");
        return -1;
    }

    int iw = (int)cinfo.output_width;
    int ih = (int)cinfo.output_height;
    size_t stride = (size_t)iw * 3;
    if ((size_t)iw > SIZE_MAX / 3 || stride > SIZE_MAX / (size_t)ih) {
        jpeg_destroy_decompress(&cinfo);
        fclose(f);
        snprintf(err, elen, "JPEG demasiado grande");
        return -1;
    }
    out = malloc(stride * (size_t)ih);
    if (!out) {
        jpeg_destroy_decompress(&cinfo);
        fclose(f);
        snprintf(err, elen, "sin memoria para JPEG");
        return -1;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = out + stride * (size_t)cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);

    *rgb = out;
    *w = iw;
    *h = ih;
    return 0;
}

int ps_jpeg_encode(const uint8_t *rgb, int w, int h, int quality,
                   uint8_t **out, size_t *outlen, char *err, size_t elen) {
    struct jpeg_compress_struct cinfo;
    PsJpegError jerr;
    FILE *f = NULL;
    char *mem = NULL;
    size_t memlen = 0;
    int created = 0;

    if (err && elen) err[0] = '\0';
    *out = NULL;
    *outlen = 0;
    if (!rgb || w <= 0 || h <= 0) {
        snprintf(err, elen, "frame invalido");
        return -1;
    }
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = ps_jpeg_error_exit;
    jerr.pub.emit_message = ps_jpeg_emit_message;

    if (setjmp(jerr.jmp)) {
        if (created) jpeg_destroy_compress(&cinfo);
        if (f) fclose(f);
        free(mem);
        snprintf(err, elen, "%s", jerr.msg[0] ? jerr.msg : "error JPEG");
        return -1;
    }

    f = open_memstream(&mem, &memlen);
    if (!f) {
        snprintf(err, elen, "no se pudo crear el buffer JPEG");
        return -1;
    }
    jpeg_create_compress(&cinfo);
    created = 1;
    jpeg_stdio_dest(&cinfo, f);

    cinfo.image_width = (JDIMENSION)w;
    cinfo.image_height = (JDIMENSION)h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    size_t stride = (size_t)w * 3;
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = (JSAMPROW)(rgb + stride * (size_t)cinfo.next_scanline);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    if (fclose(f) != 0) {
        free(mem);
        snprintf(err, elen, "error al cerrar JPEG");
        return -1;
    }

    *out = (uint8_t *)mem;
    *outlen = memlen;
    return 0;
}
