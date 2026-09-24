/**
 * @file roi_interp.c
 * @brief Save/load and evaluate two-point elliptical ROI interpolation.
 */

#include "roi_interp.h"
#include "logger.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *p)
{
    while (p && *p && isspace((unsigned char)*p))
        p++;
    return p;
}

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

/** @brief Find the JSON array value for key @p key; return start of '[' and end after ']'. */
static int find_array(const char *buf, const char *key, const char **arr_start,
                      const char **arr_end)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(buf, pat);
    if (!p)
        return -1;
    p = strchr(p + strlen(pat), ':');
    if (!p)
        return -1;
    p = skip_ws(p + 1);
    if (*p != '[')
        return -1;
    *arr_start = p;
    int depth = 0;
    for (const char *q = p; *q; q++) {
        if (*q == '[')
            depth++;
        else if (*q == ']') {
            depth--;
            if (depth == 0) {
                *arr_end = q + 1;
                return 0;
            }
        }
    }
    return -1;
}

static void write_rois_array(FILE *f, const rois_t *rois, int indent)
{
    fprintf(f, "[\n");
    for (int i = 0; i < rois->n; i++) {
        const roi_t *r = &rois->item[i];
        fprintf(f,
                "%*s{\"name\": \"%s\", \"cx\": %.3f, \"cy\": %.3f, \"rx\": %.3f, "
                "\"ry\": %.3f, \"angle\": %.3f}%s\n",
                indent, "", r->name, r->cx, r->cy, r->rx, r->ry, r->angle,
                i + 1 < rois->n ? "," : "");
    }
    fprintf(f, "%*s]", indent - 2 > 0 ? indent - 2 : 0, "");
}

int roi_interp_at(const roi_interp_t *model, int pos, rois_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!model || model->at0.n < 1 || model->at0.n != model->at1.n)
        return -1;
    if (model->pos1 == model->pos0)
        return -1;

    double t = (double)(pos - model->pos0) / (double)(model->pos1 - model->pos0);
    out->n = model->at0.n;
    for (int i = 0; i < out->n; i++) {
        const roi_t *a = &model->at0.item[i];
        const roi_t *b = &model->at1.item[i];
        roi_t *r = &out->item[i];
        memset(r, 0, sizeof(*r));
        snprintf(r->name, sizeof(r->name), "%s", a->name);
        r->cx = a->cx + t * (b->cx - a->cx);
        r->cy = a->cy + t * (b->cy - a->cy);
        r->rx = a->rx + t * (b->rx - a->rx);
        r->ry = a->ry + t * (b->ry - a->ry);
        r->angle = a->angle + t * (b->angle - a->angle);
        if (r->rx < 1.0)
            r->rx = 1.0;
        if (r->ry < 1.0)
            r->ry = 1.0;
        roi_sync_aabb(r);
    }
    return 0;
}

int roi_interp_save(const char *path, const roi_interp_t *model)
{
    if (!model || model->at0.n < 1 || model->at0.n != model->at1.n) {
        LOG_ERROR("roi_interp_save: invalid model");
        return -1;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        LOG_ERROR("roi_interp_save: open(%s): %s", path, strerror(errno));
        return -1;
    }
    fprintf(f, "{\n  \"pos0\": %d,\n  \"pos1\": %d,\n  \"at0\": ", model->pos0,
            model->pos1);
    write_rois_array(f, &model->at0, 4);
    fprintf(f, ",\n  \"at1\": ");
    write_rois_array(f, &model->at1, 4);
    fprintf(f, "\n}\n");
    fclose(f);
    LOG_INFO("Wrote interp model (%d blobs, pos %d..%d) to %s", model->at0.n,
             model->pos0, model->pos1, path);
    return 0;
}

int roi_interp_load(const char *path, roi_interp_t *model)
{
    memset(model, 0, sizeof(*model));
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("roi_interp_load: open(%s): %s", path, strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 8) {
        fclose(f);
        LOG_ERROR("%s: empty", path);
        return -1;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[sz] = '\0';

    double p0 = 0, p1 = 0;
    if (json_key_double(buf, buf + sz, "pos0", &p0) != 0 ||
        json_key_double(buf, buf + sz, "pos1", &p1) != 0) {
        LOG_ERROR("%s: missing pos0/pos1", path);
        free(buf);
        return -1;
    }
    model->pos0 = (int)lround(p0);
    model->pos1 = (int)lround(p1);

    const char *a0s, *a0e, *a1s, *a1e;
    if (find_array(buf, "at0", &a0s, &a0e) != 0 || find_array(buf, "at1", &a1s, &a1e) != 0) {
        LOG_ERROR("%s: missing at0/at1 arrays", path);
        free(buf);
        return -1;
    }

    size_t n0 = (size_t)(a0e - a0s);
    size_t n1 = (size_t)(a1e - a1s);
    char *s0 = malloc(n0 + 1);
    char *s1 = malloc(n1 + 1);
    if (!s0 || !s1) {
        free(s0);
        free(s1);
        free(buf);
        return -1;
    }
    memcpy(s0, a0s, n0);
    s0[n0] = '\0';
    memcpy(s1, a1s, n1);
    s1[n1] = '\0';

    int rc = 0;
    if (roi_parse_json(s0, &model->at0) != 0 || roi_parse_json(s1, &model->at1) != 0) {
        LOG_ERROR("%s: failed to parse at0/at1 ROIs", path);
        rc = -1;
    } else if (model->at0.n != model->at1.n) {
        LOG_ERROR("%s: at0 has %d ROIs, at1 has %d", path, model->at0.n, model->at1.n);
        rc = -1;
    } else {
        LOG_INFO("Loaded interp model (%d blobs, pos %d..%d) from %s", model->at0.n,
                 model->pos0, model->pos1, path);
    }

    free(s0);
    free(s1);
    free(buf);
    return rc;
}
