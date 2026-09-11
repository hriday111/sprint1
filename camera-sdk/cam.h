#ifndef CAM_H
#define CAM_H

/**
 * @file cam.h
 * @brief Thin C wrapper around the IDS uEye API for the Thorlabs DCC1545M (mono8).
 */

#include <ueye.h>

/**
 * @brief Open camera session and image memory.
 *
 * After @c cam_open, frames are captured into @c mem; @c cam_grab returns a
 * cropped view into that buffer (no copy).
 */
typedef struct {
    HIDS hcam;       /**< uEye camera handle. */
    char *mem;       /**< Allocated image memory. */
    INT mem_id;      /**< Image memory id. */
    INT pitch;       /**< Bytes per full sensor row. */
    INT bits;        /**< Bits per pixel (8 for mono8). */
    int width;       /**< Full AOI width in pixels. */
    int height;      /**< Full AOI height in pixels. */
    int open;        /**< Non-zero while the session is active. */
    int has_gain;    /**< Non-zero if hardware master gain is available. */
    char sensor[64]; /**< Sensor name from @c is_GetSensorInfo. */
} cam_t;

/**
 * @brief Initialize the first available uEye camera in mono8 DIB mode.
 * @param cam Out structure (zeroed and filled on success).
 * @return 0 on success, -1 on failure (message on stderr).
 */
int cam_open(cam_t *cam);

/**
 * @brief Free image memory and exit the camera session.
 * @param cam Open camera, or no-op if not open.
 */
void cam_close(cam_t *cam);

/**
 * @brief Set exposure in milliseconds, clamped to the sensor range.
 * @param cam Open camera.
 * @param ms Requested exposure.
 * @param actual Optional; receives the applied exposure (may be NULL).
 * @return 0 on success, -1 on failure.
 */
int cam_set_exposure_ms(cam_t *cam, double ms, double *actual);

/**
 * @brief Read the current exposure in milliseconds.
 * @param cam Open camera.
 * @param ms Out parameter for exposure.
 * @return 0 on success, -1 on failure.
 */
int cam_get_exposure_ms(cam_t *cam, double *ms);

/**
 * Fixed crop applied by @c cam_grab (sensor coordinates).
 * Detection and ROI work see only this window; ROI coords are relative to it.
 */
#define CAM_CROP_X 350
#define CAM_CROP_Y 60
#define CAM_CROP_W 600
#define CAM_CROP_H 740

/**
 * @brief Capture one mono8 frame and return a cropped view into camera memory.
 *
 * @p pixels remains valid until the next grab or @c cam_close.
 * @p pitch is the full sensor row stride (may be greater than @p width).
 * Index as @c pixels[y * pitch + x].
 *
 * @param cam Open camera.
 * @param pixels Out pointer into image memory at the crop origin.
 * @param width Out crop width (CAM_CROP_W).
 * @param height Out crop height (CAM_CROP_H).
 * @param pitch Out bytes per full sensor row.
 * @return 0 on success, -1 on failure.
 */
int cam_grab(cam_t *cam, unsigned char **pixels, int *width, int *height, int *pitch);

#endif
