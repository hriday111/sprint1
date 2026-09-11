#ifndef DETECT_HPP
#define DETECT_HPP

/**
 * @file detect.hpp
 * @brief OpenCV disc-mark detection used by @c calibrate.
 */

#ifdef __cplusplus
extern "C" {
#endif
#include "roi.h"
#ifdef __cplusplus
}
#endif

#include <opencv2/core.hpp>

/**
 * @brief Find dark filled marks and store an inscribed axis-aligned box per mark.
 *
 * Boxes are sized inside the fitted ellipse (not the full ellipse extent).
 * Names follow spatial order: ring1 top-left, ring2 top-right, then lower marks.
 *
 * @param gray Mono8 crop (CAM_CROP_* geometry).
 * @param nwant Desired number of marks (clamped to 1..ROI_MAX).
 * @param out Cleared and filled with detected boxes.
 * @return 0 on success (including fewer marks than requested), -1 if @p gray is invalid.
 */
int detect_boxes_cv(const cv::Mat &gray, int nwant, rois_t *out);

/**
 * @brief Wrap a possibly strided buffer as a contiguous CV_8UC1 Mat (copies if needed).
 * @param pixels Source pixels.
 * @param width Row width in pixels.
 * @param height Number of rows.
 * @param pitch Bytes per row (may exceed @p width).
 * @return Owned Mat suitable for OpenCV processing.
 */
cv::Mat frame_mat(const unsigned char *pixels, int width, int height, int pitch);

#endif
