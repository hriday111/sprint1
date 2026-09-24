#ifndef MOTOR_API_H
#define MOTOR_API_H

/**
 * @file motor_api.h
 * @brief Motor driver abstraction (A4988 stepper / SG-90 servo).
 *
 * Tools include this header and call through @c motor_api.
 * The Makefile @c MOTOR variable selects which backend defines that symbol.
 *
 * Illumination (laser / LED) is separate — see @c illumination_api.h.
 */

/** Opaque motor session; layout is private to A4988/ or SG-90/. */
typedef struct motor motor_t;

typedef struct {
    /**
     * @brief Open the motor and allocate a session.
     * @param out Receives new handle; caller must @c close.
     * @param portName Unused for GPIO backends; pass NULL.
     * @return 0 on success, -1 on failure.
     */
    int (*open)(motor_t **out, const char *portName);

    /** Stop threads / release GPIO and free @p m (NULL is ok). */
    void (*close)(motor_t *m);

    /**
     * @brief Dispatch a protocol command or absolute goto.
     * @return 1 on success (legacy), or backend-defined.
     */
    int (*send)(motor_t *m, int cmd);

    /** Current position, or -1 if not homed. */
    int (*read)(motor_t *m);

    /** Home / initialize; returns homed position or -1. */
    int (*init)(motor_t *m);

    void (*go_to)(motor_t *m, int pos);
    void (*go_rel)(motor_t *m, int rel);
    void (*go_start)(motor_t *m);
    void (*go_stop)(motor_t *m);
    void (*go_zero)(motor_t *m);
    void (*go_prepare)(motor_t *m);

    int (*get_percent)(motor_t *m);
    int (*next)(motor_t *m);
    int (*prev)(motor_t *m);

    int (*get_pos)(motor_t *m);
    double (*prism_angle)(motor_t *m);

    /** Scan window loaded from saved.prm (start / stop / prep). */
    void (*set_scan_params)(motor_t *m, int start, int stop, int prep);
    int (*get_start)(const motor_t *m);
    int (*get_stop)(const motor_t *m);
    int (*get_prep)(const motor_t *m);
} motor_api_t;

/** Defined by exactly one linked backend. */
extern const motor_api_t motor_api;

#endif
