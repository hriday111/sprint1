/**
 * @file roi.c
 * @brief ROI JSON parsing, clipping, means, saturation, and PGM export.
 */

#include "roi.h"
#include "logger.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

/** @brief Advance past ASCII whitespace. */
static const char *skip_ws(const char *p)
{
    while (p && *p && isspace((unsigned char)*p))
        p++;
    return p;
}

/**
 * @brief Find a numeric JSON field @p key inside [obj, end).
 * @return 0 on success, -1 if missing or not a number.
 */
static int json_key_double(const char *obj, const char *end, const char *key, double *out)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = obj;
    while (p && p < end) {
        p = strstr(p, pat);
        if (!p || p >= end)
            return -1;
        p += strlen(pat);
        p = skip_ws(p);
        if (*p != ':')
            continue;
        p = skip_ws(p + 1);
        char *next = NULL;
        *out = strtod(p, &next);
        if (next == p)
            return -1;
        return 0;
    }
    return -1;
}

/**
 * @brief Find a string JSON field @p key inside [obj, end).
 * @return 0 on success, -1 if missing.
 */
static int json_key_string(const char *obj, const char *end, const char *key,
                           char *out, size_t out_sz)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(obj, pat);
    if (!p || p >= end)
        return -1;
    p = strchr(p + strlen(pat), ':');
    if (!p)
        return -1;
    p = skip_ws(p + 1);
    if (*p != '"')
        return -1;
    p++;
    size_t n = 0;
    while (p < end && *p && *p != '"' && n + 1 < out_sz)
        out[n++] = *p++;
    out[n] = '\0';
    return 0;
}

void roi_inscribe(int bx, int by, int bw, int bh, roi_t *out)
{
    /*
     * Ellipse filling (bx,by,bw,bh) has semi-axes bw/2, bh/2. The largest
     * axis-aligned rectangle inside that ellipse scales the AABB by 1/sqrt(2)
     * about its centre (AABB corners lie outside a disc).
     */
    double cx = bx + bw * 0.5;
    double cy = by + bh * 0.5;
    int w = (int)(bw / M_SQRT2 + 0.5);
    int h = (int)(bh / M_SQRT2 + 0.5);
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    out->w = w;
    out->h = h;
    out->x = (int)(cx - w * 0.5);
    out->y = (int)(cy - h * 0.5);
    out->pixels = w * h;
}

int roi_load_json(const char *path, rois_t *out)
{
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("roi_load_json: open(%s): %s", path, strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        LOG_ERROR("roi_load_json: fseek(%s): %s", path, strerror(errno));
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 2) {
        fclose(f);
        LOG_ERROR("%s: empty", path);
        return -1;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        LOG_ERROR("roi_load_json: out of memory");
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        LOG_ERROR("roi_load_json: fread(%s): %s", path, strerror(errno));
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[sz] = '\0';

    const char *p = buf;
    while (out->n < ROI_MAX) {
        const char *start = strchr(p, '{');
        if (!start)
            break;
        const char *stop = strchr(start, '}');
        if (!stop)
            break;
        roi_t *r = &out->item[out->n];
        memset(r, 0, sizeof(*r));
        double x = 0, y = 0, w = 0, h = 0;
        double cx, cy, rx, ry;
        int got_box = json_key_double(start, stop, "x", &x) == 0 &&
                      json_key_double(start, stop, "y", &y) == 0 &&
                      json_key_double(start, stop, "w", &w) == 0 &&
                      json_key_double(start, stop, "h", &h) == 0;
        if (got_box) {
            r->x = (int)x;
            r->y = (int)y;
            r->w = (int)w;
            r->h = (int)h;
        } else if (json_key_double(start, stop, "cx", &cx) == 0 &&
                   json_key_double(start, stop, "cy", &cy) == 0 &&
                   json_key_double(start, stop, "rx", &rx) == 0 &&
                   json_key_double(start, stop, "ry", &ry) == 0) {
            /* Legacy ellipse JSON: convert to the inscribed axis-aligned box. */
            int bw = (int)(2.0 * rx + 0.5);
            int bh = (int)(2.0 * ry + 0.5);
            int bx = (int)(cx - rx);
            int by = (int)(cy - ry);
            roi_inscribe(bx, by, bw, bh, r);
        } else {
            p = stop + 1;
            continue;
        }
        if (json_key_string(start, stop, "name", r->name, sizeof(r->name)) != 0)
            snprintf(r->name, sizeof(r->name), "ring%d", out->n + 1);
        r->pixels = r->w * r->h;
        out->n++;
        p = stop + 1;
    }
    free(buf);
    if (out->n == 0) {
        LOG_ERROR("%s: no ROIs found", path);
        return -1;
    }
    LOG_INFO("Loaded %d ROI(s) from %s", out->n, path);
    return 0;
}

int roi_save_json(const char *path, const rois_t *rois)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        LOG_ERROR("roi_save_json: open(%s): %s", path, strerror(errno));
        return -1;
    }
    fprintf(f, "[\n");
    for (int i = 0; i < rois->n; i++) {
        const roi_t *r = &rois->item[i];
        fprintf(f,
                "  {\"name\": \"%s\", \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d}%s\n",
                r->name, r->x, r->y, r->w, r->h, i + 1 < rois->n ? "," : "");
    }
    fprintf(f, "]\n");
    fclose(f);
    LOG_INFO("Wrote %d ROI(s) to %s", rois->n, path);
    return 0;
}

void roi_free(rois_t *rois)
{
    rois->n = 0;
}

void roi_clip(rois_t *rois, int width, int height)
{
    for (int i = 0; i < rois->n; i++) {
        roi_t *r = &rois->item[i];
        if (r->x < 0) {
            r->w += r->x;
            r->x = 0;
        }
        if (r->y < 0) {
            r->h += r->y;
            r->y = 0;
        }
        if (r->x + r->w > width)
            r->w = width - r->x;
        if (r->y + r->h > height)
            r->h = height - r->y;
        if (r->w < 0)
            r->w = 0;
        if (r->h < 0)
            r->h = 0;
        r->pixels = r->w * r->h;
    }
}

/**
 * @brief Accumulate sum / count (and optional saturation count) over one box.
 */
static int accum_box(const unsigned char *pixels, int width, int height, int pitch,
                     const roi_t *r, unsigned long *sum, int *n, int sat_level,
                     unsigned long *nsat)
{
    (void)width;
    *sum = 0;
    *n = 0;
    if (nsat)
        *nsat = 0;
    if (r->w < 1 || r->h < 1)
        return 0;
    for (int y = r->y; y < r->y + r->h && y < height; y++) {
        const unsigned char *row = pixels + (size_t)y * (size_t)pitch;
        for (int x = r->x; x < r->x + r->w; x++) {
            unsigned char v = row[x];
            *sum += v;
            (*n)++;
            if (nsat && v >= (unsigned char)sat_level)
                (*nsat)++;
        }
    }
    return 0;
}

int roi_means(const unsigned char *pixels, int width, int height, int pitch,
              const rois_t *rois, double *means)
{
    for (int i = 0; i < rois->n; i++) {
        unsigned long sum = 0;
        int n = 0;
        accum_box(pixels, width, height, pitch, &rois->item[i], &sum, &n, 0, NULL);
        means[i] = n ? (double)sum / (double)n : 0.0;
    }
    return 0;
}

int roi_sat_frac(const unsigned char *pixels, int width, int height, int pitch,
                 const rois_t *rois, int sat_level, double *frac)
{
    for (int i = 0; i < rois->n; i++) {
        unsigned long sum = 0, nsat = 0;
        int n = 0;
        accum_box(pixels, width, height, pitch, &rois->item[i], &sum, &n, sat_level,
                  &nsat);
        frac[i] = n ? (double)nsat / (double)n : 0.0;
    }
    return 0;
}

void roi_draw(unsigned char *pixels, int width, int height, int pitch,
              const rois_t *rois)
{
    for (int i = 0; i < rois->n; i++) {
        const roi_t *r = &rois->item[i];
        int x0 = r->x, y0 = r->y, x1 = r->x + r->w - 1, y1 = r->y + r->h - 1;
        if (x0 < 0)
            x0 = 0;
        if (y0 < 0)
            y0 = 0;
        if (x1 >= width)
            x1 = width - 1;
        if (y1 >= height)
            y1 = height - 1;
        for (int x = x0; x <= x1; x++) {
            pixels[(size_t)y0 * pitch + x] = 255;
            pixels[(size_t)y1 * pitch + x] = 255;
            if (y0 + 1 < height)
                pixels[(size_t)(y0 + 1) * pitch + x] = 255;
            if (y1 - 1 >= 0)
                pixels[(size_t)(y1 - 1) * pitch + x] = 255;
        }
        for (int y = y0; y <= y1; y++) {
            pixels[(size_t)y * pitch + x0] = 255;
            pixels[(size_t)y * pitch + x1] = 255;
            if (x0 + 1 < width)
                pixels[(size_t)y * pitch + (x0 + 1)] = 255;
            if (x1 - 1 >= 0)
                pixels[(size_t)y * pitch + (x1 - 1)] = 255;
        }
    }
}

int write_pgm(const char *path, const unsigned char *pixels,
              int width, int height, int pitch)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        LOG_ERROR("write_pgm: open(%s): %s", path, strerror(errno));
        return -1;
    }
    fprintf(f, "P5\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; y++) {
        if (fwrite(pixels + (size_t)y * (size_t)pitch, 1, (size_t)width, f) != (size_t)width) {
            LOG_ERROR("write_pgm: fwrite(%s): %s", path, strerror(errno));
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    LOG_DEBUG("Wrote PGM %s (%dx%d)", path, width, height);
    return 0;
}
