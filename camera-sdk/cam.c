/**
 * @file cam.c
 * @brief IDS uEye camera open/close, exposure, and cropped grab.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "cam.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/**
 * Declared in libueye_api but missing from some SDK ueye.h headers.
 * On the UI-154x this call actually applies exposure; is_Exposure SET alone
 * is not sufficient.
 */
extern INT is_SetExposureTime(HIDS hCam, double EXP, double *newEXP);

/**
 * @brief Map a uEye status code to 0 / -1 and log failures.
 * @param code Return value from a uEye call.
 * @param call Name used in the error message.
 * @return 0 if IS_SUCCESS, else -1.
 */
static int check(INT code, const char *call)
{
    if (code == IS_SUCCESS)
        return 0;
    LOG_ERROR("%s failed: ueye %d", call, (int)code);
    return -1;
}

/**
 * @brief True if @p code is success or a benign "unsupported" status.
 */
static int supported(INT code)
{
    return code == IS_SUCCESS || code == IS_NOT_SUPPORTED || code == IS_INVALID_MODE;
}

int cam_open(cam_t *cam)
{
    memset(cam, 0, sizeof(*cam));
    cam->hcam = 0;

    INT n = 0;
    if (check(is_GetNumberOfCameras(&n), "is_GetNumberOfCameras"))
        return -1;
    if (n < 1) {
        LOG_ERROR("no uEye camera (is ueyeusbd running? lsusb should show 1409)");
        return -1;
    }

    if (check(is_InitCamera(&cam->hcam, NULL), "is_InitCamera"))
        return -1;

    SENSORINFO sensor;
    memset(&sensor, 0, sizeof(sensor));
    if (check(is_GetSensorInfo(cam->hcam, &sensor), "is_GetSensorInfo")) {
        is_ExitCamera(cam->hcam);
        return -1;
    }
    snprintf(cam->sensor, sizeof(cam->sensor), "%s", sensor.strSensorName);
    cam->has_gain = sensor.bMasterGain ? 1 : 0;

    if (check(is_ResetToDefault(cam->hcam), "is_ResetToDefault") ||
        check(is_SetDisplayMode(cam->hcam, IS_SET_DM_DIB), "is_SetDisplayMode") ||
        check(is_SetColorMode(cam->hcam, IS_CM_MONO8), "is_SetColorMode")) {
        is_ExitCamera(cam->hcam);
        return -1;
    }

    IS_RECT aoi;
    if (check(is_AOI(cam->hcam, IS_AOI_IMAGE_GET_AOI, &aoi, sizeof(aoi)), "is_AOI GET")) {
        is_ExitCamera(cam->hcam);
        return -1;
    }
    cam->width = (int)aoi.s32Width;
    cam->height = (int)aoi.s32Height;
    cam->bits = 8;

    if (check(is_AllocImageMem(cam->hcam, cam->width, cam->height, cam->bits,
                               &cam->mem, &cam->mem_id),
              "is_AllocImageMem") ||
        check(is_SetImageMem(cam->hcam, cam->mem, cam->mem_id), "is_SetImageMem") ||
        check(is_InquireImageMem(cam->hcam, cam->mem, cam->mem_id,
                                 NULL, NULL, NULL, &cam->pitch),
              "is_InquireImageMem")) {
        is_ExitCamera(cam->hcam);
        return -1;
    }

    /* Disable auto shutter/gain so a fixed exposure sticks. */
    double off = 0.0, dummy = 0.0;
    is_SetAutoParameter(cam->hcam, IS_SET_ENABLE_AUTO_SHUTTER, &off, &dummy);
    is_SetAutoParameter(cam->hcam, IS_SET_ENABLE_AUTO_GAIN, &off, &dummy);
    is_SetGainBoost(cam->hcam, IS_SET_GAINBOOST_OFF);
    if (cam->has_gain) {
        INT g = is_SetHardwareGain(cam->hcam, 0, IS_IGNORE_PARAMETER,
                                   IS_IGNORE_PARAMETER, IS_IGNORE_PARAMETER);
        if (!supported(g) && check(g, "is_SetHardwareGain")) {
            is_FreeImageMem(cam->hcam, cam->mem, cam->mem_id);
            is_ExitCamera(cam->hcam);
            return -1;
        }
        if (g == IS_NOT_SUPPORTED)
            cam->has_gain = 0;
    }

    cam->open = 1;
    LOG_INFO("Camera open: %s %dx%d pitch=%d gain=%d", cam->sensor, cam->width,
             cam->height, (int)cam->pitch, cam->has_gain);
    return 0;
}

void cam_close(cam_t *cam)
{
    if (!cam->open)
        return;
    LOG_INFO("Closing camera %s", cam->sensor);
    is_FreeImageMem(cam->hcam, cam->mem, cam->mem_id);
    is_ExitCamera(cam->hcam);
    cam->open = 0;
    cam->mem = NULL;
}

int cam_get_exposure_ms(cam_t *cam, double *ms)
{
    double v = 0.0;
    if (check(is_Exposure(cam->hcam, IS_EXPOSURE_CMD_GET_EXPOSURE, &v, sizeof(v)),
              "is_Exposure GET"))
        return -1;
    *ms = v;
    return 0;
}

int cam_set_exposure_ms(cam_t *cam, double ms, double *actual)
{
    double lo = 0.0, hi = 0.0;
    if (check(is_Exposure(cam->hcam, IS_EXPOSURE_CMD_GET_EXPOSURE_RANGE_MIN, &lo, sizeof(lo)),
              "is_Exposure MIN") ||
        check(is_Exposure(cam->hcam, IS_EXPOSURE_CMD_GET_EXPOSURE_RANGE_MAX, &hi, sizeof(hi)),
              "is_Exposure MAX"))
        return -1;
    if (ms < lo)
        ms = lo;
    if (ms > hi)
        ms = hi;

    double applied = 0.0;
    if (check(is_SetExposureTime(cam->hcam, ms, &applied), "is_SetExposureTime"))
        return -1;
    /* Allow one frame time so the new exposure is on the sensor. */
    usleep(150000);
    if (actual) {
        if (cam_get_exposure_ms(cam, actual) != 0)
            *actual = applied;
    }
    LOG_INFO("Exposure set: requested %.3f ms, applied %.3f ms", ms,
             actual ? *actual : applied);
    return 0;
}

int cam_grab(cam_t *cam, unsigned char **pixels, int *width, int *height, int *pitch)
{
    if (check(is_FreezeVideo(cam->hcam, IS_WAIT), "is_FreezeVideo"))
        return -1;

    int x = CAM_CROP_X;
    int y = CAM_CROP_Y;
    int w = CAM_CROP_W;
    int h = CAM_CROP_H;
    if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > cam->width || y + h > cam->height) {
        LOG_ERROR("cam_grab: crop (%d,%d %dx%d) outside %dx%d sensor", x, y, w, h,
                  cam->width, cam->height);
        return -1;
    }

    /* View into the buffer — no copy. pitch stays the full row stride. */
    *pixels = (unsigned char *)cam->mem + (size_t)y * (size_t)cam->pitch + (size_t)x;
    *width = w;
    *height = h;
    *pitch = (int)cam->pitch;
    LOG_TRACE("Grabbed crop %dx%d at (%d,%d)", w, h, x, y);
    return 0;
}
