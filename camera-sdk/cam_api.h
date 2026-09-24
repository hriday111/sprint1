#ifndef CAM_API_H
#define CAM_API_H

/**
 * @file cam_api.h
 * @brief Camera driver abstraction (vtable selected at link time via Makefile).
 *
 * Utilities include this header and call through @c camera_api, e.g.
 * @c camera_api.open(&cam). The Makefile @c CAMERA variable chooses which
 * backend object is linked; that object defines @c camera_api.
 */

/**
 * Opaque camera session. Backend-private layout; allocate via @c camera_api.open.
 */
typedef struct cam cam_t;

/**
 * @brief Function table for one camera backend.
 *
 * Only one backend is linked per build. Signatures match across backends so
 * tools stay camera-agnostic.
 */
typedef struct {
    /**
     * @brief Open the camera and allocate a session.
     * @param out Receives a new @c cam_t on success; caller must @c close.
     * @return 0 on success, -1 on failure.
     */
    int (*open)(cam_t **out);

    /**
     * @brief Free image memory, exit the session, and free @p cam.
     * @param cam Session from @c open, or NULL (no-op).
     */
    void (*close)(cam_t *cam);

    /**
     * @brief Set exposure in milliseconds (clamped to sensor range).
     * @param cam Open camera.
     * @param ms Requested exposure.
     * @param actual Optional; receives applied exposure (may be NULL).
     * @return 0 on success, -1 on failure.
     */
    int (*set_exposure_ms)(cam_t *cam, double ms, double *actual);

    /**
     * @brief Read current exposure in milliseconds.
     * @param cam Open camera.
     * @param ms Out parameter for exposure.
     * @return 0 on success, -1 on failure.
     */
    int (*get_exposure_ms)(cam_t *cam, double *ms);

    /**
     * @brief Capture one mono8 frame; return a cropped view into camera memory.
     *
     * @p pixels remains valid until the next grab or @c close.
     * Index as @c pixels[y * pitch + x].
     *
     * @return 0 on success, -1 on failure.
     */
    int (*grab)(cam_t *cam, unsigned char **pixels, int *width, int *height,
                int *pitch);

    /** Full sensor / AOI width in pixels. */
    int (*width)(const cam_t *cam);

    /** Full sensor / AOI height in pixels. */
    int (*height)(const cam_t *cam);

    /** Bytes per full sensor row. */
    int (*pitch)(const cam_t *cam);

    /** Sensor name string (valid until @c close). */
    const char *(*sensor)(const cam_t *cam);
} cam_api_t;

/** Backend-provided ops table (exactly one definition linked per build). */
extern const cam_api_t camera_api;

#endif
