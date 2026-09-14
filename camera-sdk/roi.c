/**
 * @file roi.c
 * @brief Elliptical blob ROI JSON parsing, clipping, means, saturation, and PGM export.
 */

#include "roi.h"
#include "logger.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
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

/**
 * @brief True if pixel centre (@p x + 0.5, @p y + 0.5) lies inside the ellipse.
 */
static int point_in_ellipse(int x, int y, const roi_t *r)
{
    double dx = (x + 0.5) - r->cx;
    double dy = (y + 0.5) - r->cy;
    double ang = r->angle * (M_PI / 180.0);
    double c = cos(ang);
    double s = sin(ang);
    double xr = dx * c + dy * s;
    double yr = -dx * s + dy * c;
    if (r->rx < 1e-6 || r->ry < 1e-6)
        return 0;
    return (xr * xr) / (r->rx * r->rx) + (yr * yr) / (r->ry * r->ry) <= 1.0;
}

void roi_sync_aabb(roi_t *r)
{
    double ang = r->angle * (M_PI / 180.0);
    double c = cos(ang);
    double s = sin(ang);
    double ux = r->rx * c;
    double uy = r->rx * s;
    double vx = r->ry * -s;
    double vy = r->ry * c;
    double half_w = sqrt(ux * ux + vx * vx);
    double half_h = sqrt(uy * uy + vy * vy);
    int x0 = (int)floor(r->cx - half_w);
    int y0 = (int)floor(r->cy - half_h);
    int x1 = (int)ceil(r->cx + half_w);
    int y1 = (int)ceil(r->cy + half_h);
    r->x = x0;
    r->y = y0;
    r->w = x1 - x0;
    r->h = y1 - y0;
    if (r->w < 0)
        r->w = 0;
    if (r->h < 0)
        r->h = 0;
    r->pixels = (int)lround(M_PI * r->rx * r->ry);
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
    out->cx = cx;
    out->cy = cy;
    out->rx = w * 0.5;
    out->ry = h * 0.5;
    out->angle = 0.0;
    out->w = w;
    out->h = h;
    out->x = (int)(cx - w * 0.5);
    out->y = (int)(cy - h * 0.5);
    out->pixels = w * h;
}

int roi_parse_json(const char *buf, rois_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!buf)
        return -1;
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
        double cx, cy, rx, ry, angle = 0.0;
        double x = 0, y = 0, w = 0, h = 0;
        int got_ellipse = json_key_double(start, stop, "cx", &cx) == 0 &&
                          json_key_double(start, stop, "cy", &cy) == 0 &&
                          json_key_double(start, stop, "rx", &rx) == 0 &&
                          json_key_double(start, stop, "ry", &ry) == 0;
        if (got_ellipse) {
            (void)json_key_double(start, stop, "angle", &angle);
            r->cx = cx;
            r->cy = cy;
            r->rx = rx;
            r->ry = ry;
            r->angle = angle;
            roi_sync_aabb(r);
        } else if (json_key_double(start, stop, "x", &x) == 0 &&
                   json_key_double(start, stop, "y", &y) == 0 &&
                   json_key_double(start, stop, "w", &w) == 0 &&
                   json_key_double(start, stop, "h", &h) == 0) {
            /* Legacy box JSON: treat the rectangle as an axis-aligned ellipse. */
            r->cx = x + w * 0.5;
            r->cy = y + h * 0.5;
            r->rx = w * 0.5;
            r->ry = h * 0.5;
            r->angle = 0.0;
            roi_sync_aabb(r);
        } else {
            p = stop + 1;
            continue;
        }
        if (json_key_string(start, stop, "name", r->name, sizeof(r->name)) != 0)
            snprintf(r->name, sizeof(r->name), "ring%d", out->n + 1);
        out->n++;
        p = stop + 1;
    }
    return out->n > 0 ? 0 : -1;
}

int roi_load_json(const char *path, rois_t *out)
{
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

    int rc = roi_parse_json(buf, out);
    free(buf);
    if (rc != 0) {
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
                "  {\"name\": \"%s\", \"cx\": %.3f, \"cy\": %.3f, \"rx\": %.3f, "
                "\"ry\": %.3f, \"angle\": %.3f}%s\n",
                r->name, r->cx, r->cy, r->rx, r->ry, r->angle,
                i + 1 < rois->n ? "," : "");
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
        roi_sync_aabb(r);
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

        int n = 0;
        for (int y = r->y; y < r->y + r->h; y++) {
            for (int x = r->x; x < r->x + r->w; x++) {
                if (point_in_ellipse(x, y, r))
                    n++;
            }
        }
        r->pixels = n;
    }
}

/**
 * @brief Accumulate sum / count (and optional saturation count) over one blob.
 */
static int accum_blob(const unsigned char *pixels, int width, int height, int pitch,
                      const roi_t *r, unsigned long *sum, int *n, int sat_level,
                      unsigned long *nsat)
{
    *sum = 0;
    *n = 0;
    if (nsat)
        *nsat = 0;
    if (r->w < 1 || r->h < 1 || r->rx < 1e-6 || r->ry < 1e-6)
        return 0;
    for (int y = r->y; y < r->y + r->h && y < height; y++) {
        if (y < 0)
            continue;
        const unsigned char *row = pixels + (size_t)y * (size_t)pitch;
        for (int x = r->x; x < r->x + r->w; x++) {
            if (x < 0 || x >= width)
                continue;
            if (!point_in_ellipse(x, y, r))
                continue;
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
        accum_blob(pixels, width, height, pitch, &rois->item[i], &sum, &n, 0, NULL);
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
        accum_blob(pixels, width, height, pitch, &rois->item[i], &sum, &n, sat_level,
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
        /* Outline: pixels near the ellipse boundary (1.0 ± epsilon). */
        for (int y = r->y; y < r->y + r->h && y < height; y++) {
            if (y < 0)
                continue;
            for (int x = r->x; x < r->x + r->w; x++) {
                if (x < 0 || x >= width)
                    continue;
                double dx = (x + 0.5) - r->cx;
                double dy = (y + 0.5) - r->cy;
                double ang = r->angle * (M_PI / 180.0);
                double c = cos(ang);
                double s = sin(ang);
                double xr = dx * c + dy * s;
                double yr = -dx * s + dy * c;
                if (r->rx < 1e-6 || r->ry < 1e-6)
                    continue;
                double v = (xr * xr) / (r->rx * r->rx) + (yr * yr) / (r->ry * r->ry);
                if (v >= 0.92 && v <= 1.08)
                    pixels[(size_t)y * (size_t)pitch + (size_t)x] = 255;
            }
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
