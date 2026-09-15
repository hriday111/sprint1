#ifndef ROI_INTERP_H
#define ROI_INTERP_H

/**
 * @file roi_interp.h
 * @brief Two-point linear interpolation of elliptical blob ROIs vs servo position.
 */

#include "roi.h"

/** Default two-point model path written by @c calibrate2. */
#define ROI_INTERP_PATH "/var/lib/sprint1/rois_interp.json"

/**
 * @brief Calibration endpoints: blob geometry at @c pos0 and @c pos1.
 *
 * @c at0.item[i] and @c at1.item[i] must share the same @c name and count.
 */
typedef struct {
    int pos0;   /**< Servo step for @c at0 (e.g. 400). */
    int pos1;   /**< Servo step for @c at1 (e.g. 420). */
    rois_t at0; /**< Blobs detected at @c pos0. */
    rois_t at1; /**< Blobs detected at @c pos1. */
} roi_interp_t;

/**
 * @brief Linearly interpolate blob ROIs for servo step @p pos.
 *
 * t = (pos - pos0) / (pos1 - pos0); each of cx, cy, rx, ry, angle is lerped.
 * Extrapolation outside [pos0, pos1] is allowed.
 *
 * @param model Two-point calibration.
 * @param pos Servo step.
 * @param out Filled with interpolated blobs (same names/count as endpoints).
 * @return 0 on success, -1 if model is invalid.
 */
int roi_interp_at(const roi_interp_t *model, int pos, rois_t *out);

/**
 * @brief Write the two-point model as JSON.
 * @return 0 on success, -1 on failure.
 */
int roi_interp_save(const char *path, const roi_interp_t *model);

/**
 * @brief Load a model written by @c roi_interp_save.
 * @return 0 on success, -1 on failure.
 */
int roi_interp_load(const char *path, roi_interp_t *model);

#endif
