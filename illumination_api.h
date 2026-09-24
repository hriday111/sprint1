#ifndef ILLUMINATION_API_H
#define ILLUMINATION_API_H

/**
 * @file illumination_api.h
 * @brief Illumination driver abstraction (laser / LED).
 *
 * Tools include this header only and call through @c illumination_api.
 * The Makefile picks which backend defines that symbol.
 */

/** Opaque handle — real layout is private to Laser/ or Led/. */
typedef struct illumination illumination_t;

typedef struct {
    /**
     * @brief Create a session (e.g. claim GPIO pin).
     * @param out Receives new handle; caller must @c close.
     * @param pin Backend-specific pin / channel id.
     * @return 0 on success, -1 on failure.
     */
    int (*open)(illumination_t **out, int pin);

    /** Turn illumination off and free @p illum (NULL is ok). */
    void (*close)(illumination_t *illum);

    void (*on)(illumination_t *illum);
    void (*off)(illumination_t *illum);

    /**
     * @brief Set brightness 0..255 (or backend range).
     * Laser may no-op; LED should implement it.
     */
    void (*set_brightness)(illumination_t *illum, int brightness);
} illumination_api_t;

/** Defined by exactly one linked backend (Laser/laser.c or Led/led.c). */
extern const illumination_api_t illumination_api;

#endif
