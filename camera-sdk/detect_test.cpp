/**
 * @file detect_test.cpp
 * @brief Offline test of @c detect_boxes_cv on a PNG/PGM (no camera).
 *
 * Not built by the default Makefile. Writes overlay to /tmp/detect_test.png.
 *
 * Usage: @c ./detect_test image.png [n]
 */

#include <cstdio>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
extern "C" {
#include "roi.h"
#include "logger.h"
#include "crash.h"
}
#include "detect.hpp"

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s image.png [n]\n", argv[0]);
        return 2;
    }
    if (sprint1_logging_init("detect_test", NULL, 0) != 0)
        return 1;

    cv::Mat gray = cv::imread(argv[1], cv::IMREAD_GRAYSCALE);
    if (gray.empty()) {
        LOG_ERROR("cannot read %s", argv[1]);
        return 1;
    }
    int n = argc > 2 ? std::atoi(argv[2]) : 3;
    rois_t rois;
    detect_boxes_cv(gray, n, &rois);
    LOG_INFO("found %d", rois.n);
    cv::Mat vis;
    cv::cvtColor(gray, vis, cv::COLOR_GRAY2BGR);
    for (int i = 0; i < rois.n; i++) {
        const roi_t *r = &rois.item[i];
        LOG_INFO("  %s (%d,%d %dx%d)", r->name, r->x, r->y, r->w, r->h);
        cv::rectangle(vis, cv::Rect(r->x, r->y, r->w, r->h), cv::Scalar(0, 255, 0), 2);
    }
    cv::imwrite("/tmp/detect_test.png", vis);
    LOG_INFO("wrote /tmp/detect_test.png");
    logger_flush();
    logger_exitFileLogger();
    return rois.n > 0 ? 0 : 1;
}
