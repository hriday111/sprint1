/**
 * @file detect.cpp
 * @brief Disc-mark detector for @c calibrate.
 *
 * Chip illumination is uneven (left side darker), so a single global threshold
 * often merges the top-left disc into vignette and lets small cross fiducials
 * steal a detection slot. Pipeline:
 *   1. Flatten the frame (gray / local mean).
 *   2. Threshold dark spots at several levels of the flattened image.
 *   3. Reject blobs that are too small (crosses) or too elongated.
 *   4. Refine each survivor; keep the best non-overlapping set.
 *   5. Name by spatial order: ring1 = top-left, ring2 = top-right, ring3 = bottom.
 */

#include "detect.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "utils.h"
#include <opencv2/imgproc.hpp>

extern "C" {
#include "logger.h"
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** @brief Intermediate ellipse candidate before conversion to @c roi_t. */
struct EllipseCand {
    double cx = 0, cy = 0, rx = 0, ry = 0, angle = 0;
    double score = 0, coverage = 0;
};

/**
 * @brief Build a binary mask of the bright chip region on a downscaled frame.
 * @param small Downscaled grayscale.
 * @param frac Brightness fraction of the 99th-percentile background.
 * @param hull If true, fill the convex hull of the largest component.
 * @return 8-bit mask (255 = on chip).
 */
static cv::Mat chip_mask(const cv::Mat &small, double frac, bool hull)
{
    cv::Mat bg;
    cv::blur(small, bg, cv::Size(31, 31));

    double p99;
    {
        std::vector<uchar> buf(bg.begin<uchar>(), bg.end<uchar>());
        std::nth_element(buf.begin(), buf.begin() + (int)(0.99 * (buf.size() - 1)), buf.end());
        p99 = buf[(int)(0.99 * (buf.size() - 1))];
    }

    cv::Mat mask = bg > (frac * p99);
    mask.convertTo(mask, CV_8U, 255.0);

    cv::Mat labels, stats, cents;
    int n = cv::connectedComponentsWithStats(mask, labels, stats, cents, 8, CV_32S);
    if (n <= 1)
        return cv::Mat::ones(small.size(), CV_8U);

    int best = 1, best_a = 0;
    for (int i = 1; i < n; i++) {
        int a = stats.at<int>(i, cv::CC_STAT_AREA);
        if (a > best_a) {
            best_a = a;
            best = i;
        }
    }
    mask = (labels == best);
    mask.convertTo(mask, CV_8U, 255.0);

    if (hull) {
        std::vector<std::vector<cv::Point>> cnts;
        cv::findContours(mask, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        mask = cv::Mat::zeros(small.size(), CV_8U);
        if (!cnts.empty()) {
            auto it = std::max_element(cnts.begin(), cnts.end(),
                                       [](const auto &a, const auto &b) {
                                           return cv::contourArea(a) < cv::contourArea(b);
                                       });
            std::vector<cv::Point> hull_pts;
            cv::convexHull(*it, hull_pts);
            cv::fillConvexPoly(mask, hull_pts, 255);
        }
    }

    cv::Mat k = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(9, 9));
    cv::erode(mask, mask, k);
    return mask;
}

/**
 * @brief Divide by a large Gaussian blur so chip background centres near 128.
 * Dark discs fall well below that level.
 */
static cv::Mat flatten(const cv::Mat &gray, double r_hi)
{
    int bk = ((int)(2.0 * r_hi) | 1);
    if (bk < 51)
        bk = 51;
    if (bk > 151)
        bk = 151; /* Cap kernel size — large blurs are slow on the Pi. */
    cv::Mat bg;
    cv::GaussianBlur(gray, bg, cv::Size(bk, bk), 0);
    cv::Mat g32, b32, out;
    gray.convertTo(g32, CV_32F);
    bg.convertTo(b32, CV_32F);
    cv::Mat n = g32 / (b32 + 1.f) * 128.f;
    n.convertTo(out, CV_8U);
    return out;
}

/** @brief Fill a scaled ellipse into mask @p m with value 255. */
static void draw_ellipse_mask(cv::Mat &m, double cx, double cy, double rx, double ry,
                              double angle_deg, double scale)
{
    int arx = std::max(1, (int)std::lround(rx * scale));
    int ary = std::max(1, (int)std::lround(ry * scale));
    cv::ellipse(m, cv::Point((int)std::lround(cx), (int)std::lround(cy)),
                cv::Size(arx, ary), angle_deg, 0, 360, 255, -1);
}

/**
 * @brief Local re-threshold and ellipse fit around a coarse candidate.
 * @return Refined candidate, or the original @p e if refinement fails checks.
 */
static EllipseCand refine_disc(const cv::Mat &sm, EllipseCand e)
{
    int h = sm.rows, w = sm.cols;
    double r = std::max(e.rx, e.ry);
    int x0 = std::max(0, (int)(e.cx - 1.8 * r));
    int y0 = std::max(0, (int)(e.cy - 1.8 * r));
    int x1 = std::min(w, (int)(e.cx + 1.8 * r));
    int y1 = std::min(h, (int)(e.cy + 1.8 * r));
    if (x1 <= x0 || y1 <= y0)
        return e;

    cv::Mat crop = sm(cv::Rect(x0, y0, x1 - x0, y1 - y0));
    double lcx = e.cx - x0, lcy = e.cy - y0;

    cv::Mat inner = cv::Mat::zeros(crop.size(), CV_8U);
    cv::Mat out_big = cv::Mat::zeros(crop.size(), CV_8U);
    cv::Mat out_small = cv::Mat::zeros(crop.size(), CV_8U);
    draw_ellipse_mask(inner, lcx, lcy, e.rx, e.ry, e.angle, 0.7);
    draw_ellipse_mask(out_big, lcx, lcy, e.rx, e.ry, e.angle, 1.6);
    draw_ellipse_mask(out_small, lcx, lcy, e.rx, e.ry, e.angle, 1.2);
    cv::Mat ring_out = out_big & ~out_small;

    if (cv::countNonZero(inner) < 20 || cv::countNonZero(ring_out) < 20)
        return e;

    std::vector<uchar> iv, ov;
    for (int y = 0; y < crop.rows; y++) {
        const uchar *row = crop.ptr<uchar>(y);
        const uchar *ii = inner.ptr<uchar>(y);
        const uchar *oo = ring_out.ptr<uchar>(y);
        for (int x = 0; x < crop.cols; x++) {
            if (ii[x])
                iv.push_back(row[x]);
            if (oo[x])
                ov.push_back(row[x]);
        }
    }
    if (iv.size() < 20 || ov.size() < 20)
        return e;

    std::nth_element(iv.begin(), iv.begin() + (int)(0.75 * (iv.size() - 1)), iv.end());
    double lo = iv[(int)(0.75 * (iv.size() - 1))];
    std::nth_element(ov.begin(), ov.begin() + ov.size() / 2, ov.end());
    double hi = ov[ov.size() / 2];
    if (hi <= lo)
        return e;
    e.score = 1.0 - lo / hi;
    double thr = 0.5 * (lo + hi);

    cv::Mat limit = cv::Mat::zeros(crop.size(), CV_8U);
    draw_ellipse_mask(limit, lcx, lcy, e.rx, e.ry, e.angle, 1.4);
    cv::Mat blob = (crop < thr) & limit;

    cv::Mat labels, stats, cents;
    int nlab = cv::connectedComponentsWithStats(blob, labels, stats, cents, 8, CV_32S);
    if (nlab <= 1)
        return e;

    int cy_i = (int)lcy, cx_i = (int)lcx;
    int lab = 0;
    if (cy_i >= 0 && cy_i < crop.rows && cx_i >= 0 && cx_i < crop.cols)
        lab = labels.at<int>(cy_i, cx_i);
    if (lab == 0) {
        int best = 1, best_a = 0;
        for (int i = 1; i < nlab; i++) {
            int a = stats.at<int>(i, cv::CC_STAT_AREA);
            if (a > best_a) {
                best_a = a;
                best = i;
            }
        }
        lab = best;
    }

    cv::Mat comp = (labels == lab);
    comp.convertTo(comp, CV_8U, 255.0);
    std::vector<std::vector<cv::Point>> cnts;
    cv::findContours(comp, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (cnts.empty())
        return e;
    auto c = *std::max_element(cnts.begin(), cnts.end(),
                               [](const auto &a, const auto &b) {
                                   return cv::contourArea(a) < cv::contourArea(b);
                               });
    if (c.size() < 5)
        return e;

    cv::RotatedRect rr = cv::fitEllipse(c);
    EllipseCand out;
    out.cx = rr.center.x + x0;
    out.cy = rr.center.y + y0;
    out.rx = rr.size.width / 2.0;
    out.ry = rr.size.height / 2.0;
    out.angle = rr.angle;
    double area = cv::contourArea(c);
    out.coverage = (rr.size.width * rr.size.height > 0)
                       ? area / (M_PI * rr.size.width * rr.size.height / 4.0)
                       : 0.0;
    out.score = e.score;

    double area_coarse = M_PI * e.rx * e.ry;
    double area_new = M_PI * out.rx * out.ry;
    double dist = std::hypot(out.cx - e.cx, out.cy - e.cy);
    /* Accept modest centre drift; left-side discs often shift after refine. */
    if (!(0.55 < area_new / area_coarse && area_new / area_coarse < 1.55) ||
        dist > 0.4 * r)
        return e;
    return out;
}

/** @brief True if ellipse centres are closer than 0.85 of summed major radii. */
static bool overlaps(const EllipseCand &a, const EllipseCand &b)
{
    double ra = std::max(a.rx, a.ry), rb = std::max(b.rx, b.ry);
    return std::hypot(a.cx - b.cx, a.cy - b.cy) < 0.85 * (ra + rb);
}

cv::Mat frame_mat(const unsigned char *pixels, int width, int height, int pitch)
{
    if (pitch == width)
        return cv::Mat(height, width, CV_8UC1, (void *)pixels).clone();
    cv::Mat out(height, width, CV_8UC1);
    for (int y = 0; y < height; y++)
        std::memcpy(out.ptr(y), pixels + (size_t)y * (size_t)pitch, (size_t)width);
    return out;
}

int detect_boxes_cv(const cv::Mat &gray, int nwant, rois_t *out)
{
    std::memset(out, 0, sizeof(*out));
    if (gray.empty() || gray.type() != CV_8UC1) {
        LOG_ERROR("detect_boxes_cv: empty or non-gray frame");
        return -1;
    }
    if (nwant < 1)
        nwant = 1;
    if (nwant > ROI_MAX)
        nwant = ROI_MAX;

    LOG_INFO("detect_boxes_cv: %dx%d want=%d", gray.cols, gray.rows, nwant);

    int h = gray.rows, w = gray.cols;
    /* Discs are ~80–110 px radius on a 600×740 crop; crosses are much smaller. */
    double r_lo = 0.09 * std::min(h, w);
    double r_hi = 0.20 * std::min(h, w);
    double area_lo = M_PI * r_lo * r_lo * 0.85; /* Reject cross-sized blobs. */
    double area_hi = M_PI * r_hi * r_hi * 1.3;

    cv::Mat flat = flatten(gray, r_hi);
    int k = std::max(3, ((int)(0.18 * r_lo) | 1));
    cv::Mat sm;
    cv::blur(flat, sm, cv::Size(k, k));

    cv::Mat small;
    cv::resize(flat, small, cv::Size(), 0.25, 0.25, cv::INTER_AREA);
    cv::Mat chip = chip_mask(small, 0.45, true);
    cv::resize(chip, chip, gray.size(), 0, 0, cv::INTER_NEAREST);
    /* Mild erode keeps the top-left disc inside the chip mask. */
    int er = std::max(3, ((int)(0.22 * r_lo) | 1));
    cv::erode(chip, chip, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(er, er)));
    if (cv::countNonZero(chip) == 0)
        return 0;

    /* Multi-level thresholds relative to flattened background (~128). */
    const double fracs[] = {0.78, 0.82, 0.86, 0.88, 0.92};
    std::vector<EllipseCand> found;
    for (double frac : fracs) {
        cv::Mat dark = (sm < frac * 128.0) & chip;
        dark.convertTo(dark, CV_8U, 255.0);
        int ko_s = std::max(3, ((int)(0.18 * r_lo) | 1));
        /* Strong close fills streaky disc interiors (especially the bottom mark). */
        int kc_s = std::max(3, ((int)(0.45 * r_lo) | 1));
        cv::morphologyEx(dark, dark, cv::MORPH_OPEN,
                         cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(ko_s, ko_s)));
        cv::morphologyEx(dark, dark, cv::MORPH_CLOSE,
                         cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kc_s, kc_s)));

        std::vector<std::vector<cv::Point>> cnts;
        cv::findContours(dark, cnts, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        for (const auto &c : cnts) {
            double area = cv::contourArea(c);
            if (area < area_lo || area > area_hi || c.size() < 5)
                continue;
            cv::RotatedRect rr = cv::fitEllipse(c);
            EllipseCand e;
            e.cx = rr.center.x;
            e.cy = rr.center.y;
            e.rx = rr.size.width / 2.0;
            e.ry = rr.size.height / 2.0;
            e.angle = rr.angle;
            e.coverage = (rr.size.width * rr.size.height > 0)
                             ? area / (M_PI * rr.size.width * rr.size.height / 4.0)
                             : 0.0;
            if (std::min(e.rx, e.ry) < 0.85 * r_lo)
                continue;
            /* Skip blobs glued to the frame border. */
            if (e.cx < 25 || e.cy < 25 || e.cx > w - 25 || e.cy > h - 25)
                continue;
            found.push_back(refine_disc(sm, e));
        }
    }

    std::vector<EllipseCand> filtered;
    for (const auto &e : found) {
        double ratio = std::min(e.rx, e.ry) / std::max(e.rx, e.ry);
        /* Streaky bottom discs often land near coverage ~0.3 after refine. */
        if (e.coverage >= 0.30 && ratio > 0.55 && std::min(e.rx, e.ry) >= 0.8 * r_lo)
            filtered.push_back(e);
    }
    /* Prefer high contrast, then better fill / circularity. */
    std::sort(filtered.begin(), filtered.end(), [](const EllipseCand &a, const EllipseCand &b) {
        double sa = a.score + 0.15 * a.coverage;
        double sb = b.score + 0.15 * b.coverage;
        return sa > sb;
    });

    std::vector<EllipseCand> kept;
    for (const auto &e : filtered) {
        bool hit = false;
        for (const auto &k2 : kept) {
            if (overlaps(e, k2)) {
                hit = true;
                break;
            }
        }
        if (!hit)
            kept.push_back(e);
        if ((int)kept.size() >= nwant)
            break;
    }

    /* Stable naming: top row left-to-right, then lower marks by y. */
    std::sort(kept.begin(), kept.end(),
              [](const EllipseCand &a, const EllipseCand &b) { return a.cy < b.cy; });
    if (kept.size() >= 2) {
        double ysep = kept[1].cy - kept[0].cy;
        if (ysep < 0.35 * h && kept[0].cx > kept[1].cx)
            std::swap(kept[0], kept[1]);
    }

    for (size_t i = 0; i < kept.size() && out->n < ROI_MAX; i++) {
        EllipseCand e = kept[i];
        /* Recenter on dark pixels so streaky discs keep the blob on the mark. */
        {
            cv::Mat m = cv::Mat::zeros(sm.size(), CV_8U);
            draw_ellipse_mask(m, e.cx, e.cy, e.rx, e.ry, e.angle, 0.85);
            double sx = 0, sy = 0;
            int n = 0;
            for (int y = 0; y < h; y++) {
                const uchar *mp = m.ptr<uchar>(y);
                const uchar *sp = sm.ptr<uchar>(y);
                for (int x = 0; x < w; x++) {
                    if (mp[x] && sp[x] < 110) {
                        sx += x;
                        sy += y;
                        n++;
                    }
                }
            }
            if (n > 50) {
                e.cx = sx / n;
                e.cy = sy / n;
            }
        }
        /* Store the fitted disc blob (slightly inset)*/
        roi_t blob;
        std::memset(&blob, 0, sizeof(blob));
        blob.cx = e.cx;
        blob.cy = e.cy;
        blob.rx = std::max(1.0, e.rx * (1-ERROR));
        blob.ry = std::max(1.0, e.ry * (1-ERROR));
        blob.angle = e.angle;
        std::snprintf(blob.name, sizeof(blob.name), "ring%zu", i + 1);
        roi_sync_aabb(&blob);
        out->item[out->n++] = blob;
    }
    roi_clip(out, w, h);
    LOG_INFO("detect_boxes_cv: kept %d blob(s)", out->n);
    return 0;
}
