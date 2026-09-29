#ifndef ROI_H
#define ROI_H

/**
 * @file roi.h
 * @brief Elliptical blob ROIs, JSON I/O, and intensity statistics.
 *
 * Intensity is the mean of pixels inside each fitted ellipse (blob mask).
 * Axis-aligned @c x,y,w,h is only the bounding box used for iteration/clip.
 */

#define ROI_MAX 8

/** Default path written by @c calibrate and read by @c sweep_run. */
#define ROI_PATH "/var/lib/sprint1/rois.json"

/**
 * @brief One named elliptical blob in crop coordinates.
 *
 * @c cx,cy,rx,ry,@c angle define the measurement mask. @c x,y,w,h is the
 * axis-aligned bounding box of that ellipse (updated by @c roi_sync_aabb /
 * @c roi_clip).
 */
typedef struct {
    double cx, cy;  /**< Ellipse centre (crop pixels). */
    double rx, ry;  /**< Semi-axes in pixels. */
    double angle;   /**< Rotation in degrees (OpenCV fitEllipse convention). */
    int x, y, w, h; /**< AABB of the ellipse (iteration / diagnostics). */
    char name[32];  /**< Label (e.g. ring1); used as CSV column header. */
    int pixels;     /**< Mask pixel count after clipping (for diagnostics). */
} roi_t;

/**
 * @brief Fixed-capacity list of ROIs (at most @c ROI_MAX).
 */
typedef struct {
    roi_t item[ROI_MAX];
    int n; /**< Number of valid entries in @c item. */
} rois_t;

/**
 * @brief Refresh @c x,y,w,h from ellipse parameters (rotated AABB).
 * @param r ROI to update.
 */
void roi_sync_aabb(roi_t *r);

/**
 * @brief Parse ROIs from a JSON array/object string (ellipse or legacy box).
 * @param buf NUL-terminated JSON text containing @c {...} ROI objects.
 * @param out Cleared and filled on success.
 * @return 0 on success, -1 if no ROIs found.
 */
int roi_parse_json(const char *buf, rois_t *out);

/**
 * @brief Load ROIs from JSON (ellipse cx/cy/rx/ry[/angle] or legacy box x/y/w/h).
 * @param path File path.
 * @param out Cleared and filled on success.
 * @return 0 on success, -1 on failure.
 */
int roi_load_json(const char *path, rois_t *out);

/**
 * @brief Write ROIs as a JSON array of {name,cx,cy,rx,ry,angle} objects.
 * @param path Destination path.
 * @param rois Source list.
 * @return 0 on success, -1 on failure.
 */
int roi_save_json(const char *path, const rois_t *rois);

/**
 * @brief Clear the ROI count (no heap to free today).
 * @param rois List to reset.
 */
void roi_free(rois_t *rois);

/**
 * @brief Clip each AABB to [0,width) x [0,height) and recount mask pixels.
 * @param rois In/out list.
 * @param width Frame width.
 * @param height Frame height.
 */
void roi_clip(rois_t *rois, int width, int height);

/**
 * @brief Mean intensity 0..255 for each elliptical blob mask.
 * @param pixels Frame buffer.
 * @param width Frame width (unused for indexing; pitch is authoritative).
 * @param height Frame height.
 * @param pitch Bytes per row.
 * @param rois ROI list.
 * @param means Output array with at least @c rois->n entries.
 * @return Always 0.
 */
int roi_means(const unsigned char *pixels, int width, int height, int pitch,
              const rois_t *rois, double *means);

/**
 * @brief Fraction of pixels at or above @p sat_level in each blob mask.
 * @param pixels Frame buffer.
 * @param width Frame width.
 * @param height Frame height.
 * @param pitch Bytes per row.
 * @param rois ROI list.
 * @param sat_level Threshold (e.g. 250).
 * @param frac Output array with at least @c rois->n entries.
 * @return Always 0.
 */
int roi_sat_frac(const unsigned char *pixels, int width, int height, int pitch,
                 const rois_t *rois, int sat_level, double *frac);

/**
 * @brief Draw white (255) ellipse outlines onto @p pixels in place.
 * @param pixels Mutable frame.
 * @param width Frame width.
 * @param height Frame height.
 * @param pitch Bytes per row.
 * @param rois Blobs to outline.
 */
void roi_draw(unsigned char *pixels, int width, int height, int pitch,
              const rois_t *rois);

/**
 * @brief Write a binary PGM (P5) using @p pitch as the source row stride.
 * @param path Output path.
 * @param pixels Frame buffer.
 * @param width Image width written to the header.
 * @param height Image height.
 * @param pitch Source stride in bytes.
 * @return 0 on success, -1 on failure.
 */
int write_pgm(const char *path, const unsigned char *pixels,
              int width, int height, int pitch);

/**
 * @brief Largest axis-aligned rectangle inside an axis-aligned ellipse AABB.
 * @param bx Ellipse bounding-box left.
 * @param by Ellipse bounding-box top.
 * @param bw Ellipse bounding-box width.
 * @param bh Ellipse bounding-box height.
 * @param out Filled with the inscribed rectangle (ellipse fields also set).
 */
void roi_inscribe(int bx, int by, int bw, int bh, roi_t *out);

#endif
