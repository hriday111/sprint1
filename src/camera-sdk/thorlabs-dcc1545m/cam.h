#ifndef THORLABS_DCC1545M_CAM_H
#define THORLABS_DCC1545M_CAM_H

/**
 * @file cam.h
 * @brief IDS uEye / Thorlabs DCC1545M backend (private to this directory).
 *
 * Utilities should include @c cam_api.h and use @c camera_api, not this header.
 */

#include <ueye.h>

/**
 * Fixed crop applied by grab (sensor coordinates).
 * Detection and ROI work see only this window; ROI coords are relative to it.
 */
#define CAM_CROP_X 300
#define CAM_CROP_Y 60
#define CAM_CROP_W 700
#define CAM_CROP_H 740

/** Backend-private session state (opaque @c cam_t to the rest of the tree). */
struct cam {
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
};

#endif
