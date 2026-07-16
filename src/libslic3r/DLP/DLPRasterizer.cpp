///|/ Rasterizer for the independent DLP engine.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "DLPRasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "libslic3r/Point.hpp"

namespace Slic3r::dlp {

namespace {

using Interval = std::pair<double, double>;

std::vector<Interval> ring_intervals(const Polygon &ring, double y)
{
    std::vector<double> crossings;
    crossings.reserve(ring.points.size());
    if (ring.points.size() >= 3) {
        for (size_t current = 0, previous = ring.points.size() - 1;
             current < ring.points.size(); previous = current++) {
            const Point &a = ring.points[previous];
            const Point &b = ring.points[current];
            if ((static_cast<double>(a.y()) > y) != (static_cast<double>(b.y()) > y)) {
                crossings.push_back(static_cast<double>(a.x()) +
                    (y - static_cast<double>(a.y())) *
                    static_cast<double>(b.x() - a.x()) / static_cast<double>(b.y() - a.y()));
            }
        }
    }
    std::sort(crossings.begin(), crossings.end());
    std::vector<Interval> result;
    result.reserve(crossings.size() / 2);
    for (size_t index = 0; index + 1 < crossings.size(); index += 2)
        if (crossings[index] < crossings[index + 1])
            result.emplace_back(crossings[index], crossings[index + 1]);
    return result;
}

std::vector<Interval> subtract_intervals(std::vector<Interval> source,
                                         const std::vector<Interval> &cuts)
{
    for (const Interval &cut : cuts) {
        std::vector<Interval> next;
        for (const Interval &interval : source) {
            if (cut.second <= interval.first || cut.first >= interval.second) {
                next.push_back(interval);
            } else {
                if (interval.first < cut.first)
                    next.emplace_back(interval.first, std::min(interval.second, cut.first));
                if (cut.second < interval.second)
                    next.emplace_back(std::max(interval.first, cut.second), interval.second);
            }
        }
        source = std::move(next);
    }
    return source;
}

std::vector<Interval> exposed_intervals(const Layer &layer, double y)
{
    std::vector<Interval> intervals;
    for (const ExPolygons *polygons : {&layer.model, &layer.supports}) {
        for (const ExPolygon &polygon : *polygons) {
            std::vector<Interval> solid = ring_intervals(polygon.contour, y);
            for (const Polygon &hole : polygon.holes)
                solid = subtract_intervals(std::move(solid), ring_intervals(hole, y));
            intervals.insert(intervals.end(), solid.begin(), solid.end());
        }
    }
    std::sort(intervals.begin(), intervals.end());
    std::vector<Interval> merged;
    for (const Interval &interval : intervals) {
        if (merged.empty() || interval.first > merged.back().second)
            merged.push_back(interval);
        else
            merged.back().second = std::max(merged.back().second, interval.second);
    }
    return merged;
}

} // namespace

RasterLayer GrayscaleRasterizer::rasterize(const Layer &layer, const PrinterConfig &printer,
                                           const CancelFn &cancel) const
{
    const size_t width  = printer.resolution.width_px;
    const size_t height = printer.resolution.height_px;
    if (width == 0 || height == 0)
        throw std::invalid_argument("DLP raster resolution must be greater than zero");
    if (height > std::numeric_limits<size_t>::max() / width)
        throw std::overflow_error("DLP raster dimensions exceed the addressable buffer size");
    if (printer.build_width_mm <= 0.0 || printer.build_height_mm <= 0.0)
        throw std::invalid_argument("DLP raster build dimensions must be greater than zero");
    if (printer.antialiasing_samples == 0 || printer.antialiasing_samples > 8)
        throw std::invalid_argument("DLP anti-aliasing samples must be between 1 and 8");

    RasterLayer output;
    output.index           = layer.index;
    output.height_mm       = layer.height_mm;
    output.exposure_time_s = layer.exposure_time_s;
    output.resolution      = printer.resolution;
    output.grayscale.assign(width * height, uint8_t(0));

    const size_t samples = printer.antialiasing_samples;
    const size_t sample_count = samples * samples;

    // Most DLP layers cover only a small part of the projector. Restrict the
    // expensive point-in-polygon tests to the combined model/support bounds
    // instead of scanning every black background pixel.
    coord_t min_x = std::numeric_limits<coord_t>::max();
    coord_t min_y = std::numeric_limits<coord_t>::max();
    coord_t max_x = std::numeric_limits<coord_t>::lowest();
    coord_t max_y = std::numeric_limits<coord_t>::lowest();
    bool has_points = false;
    for (const ExPolygons *polygons : {&layer.model, &layer.supports}) {
        for (const ExPolygon &polygon : *polygons) {
            for (const Point &point : polygon.contour.points) {
                min_x = std::min(min_x, point.x());
                min_y = std::min(min_y, point.y());
                max_x = std::max(max_x, point.x());
                max_y = std::max(max_y, point.y());
                has_points = true;
            }
        }
    }
    if (! has_points)
        return output;

    const auto pixel_bounds = [](coord_t minimum, coord_t maximum, double build_size,
                                 size_t resolution, bool mirror) {
        const double scale = static_cast<double>(resolution) / build_size;
        const long long raw_begin = static_cast<long long>(std::floor(unscale<double>(minimum) * scale)) - 1;
        const long long raw_end   = static_cast<long long>(std::ceil (unscale<double>(maximum) * scale)) + 1;
        const size_t source_begin = static_cast<size_t>(std::clamp<long long>(raw_begin, 0, resolution));
        const size_t source_end   = static_cast<size_t>(std::clamp<long long>(raw_end,   0, resolution));
        return mirror ? std::pair<size_t, size_t>{resolution - source_end, resolution - source_begin}
                      : std::pair<size_t, size_t>{source_begin, source_end};
    };
    const auto [output_x_begin, output_x_end] = pixel_bounds(
        min_x, max_x, printer.build_width_mm, width, printer.mirror_x);
    const auto [output_y_begin, output_y_end] = pixel_bounds(
        min_y, max_y, printer.build_height_mm, height, printer.mirror_y);

    const double scaled_build_width  = static_cast<double>(scale_(printer.build_width_mm));
    const double scaled_build_height = static_cast<double>(scale_(printer.build_height_mm));
    const size_t horizontal_samples = width * samples;
    for (size_t output_y = output_y_begin; output_y < output_y_end; ++output_y) {
        if (cancel)
            cancel();
        std::vector<uint8_t> covered(width, uint8_t(0));
        const size_t source_y = printer.mirror_y ? height - 1 - output_y : output_y;
        for (size_t sample_y = 0; sample_y < samples; ++sample_y) {
            const double subpixel_y = static_cast<double>(source_y * samples + sample_y) + 0.5;
            const double model_y = subpixel_y * scaled_build_height /
                                   static_cast<double>(height * samples);
            for (const Interval &interval : exposed_intervals(layer, model_y)) {
                long long first = static_cast<long long>(std::ceil(
                    interval.first * horizontal_samples / scaled_build_width - 0.5));
                long long last = static_cast<long long>(std::ceil(
                    interval.second * horizontal_samples / scaled_build_width - 0.5));
                first = std::clamp<long long>(first, 0, horizontal_samples);
                last  = std::clamp<long long>(last, 0, horizontal_samples);
                for (long long subpixel_x = first; subpixel_x < last; ++subpixel_x) {
                    const size_t source_x = static_cast<size_t>(subpixel_x) / samples;
                    const size_t output_x = printer.mirror_x ? width - 1 - source_x : source_x;
                    ++covered[output_x];
                }
            }
        }
        for (size_t output_x = output_x_begin; output_x < output_x_end; ++output_x) {
            output.grayscale[output_y * width + output_x] = static_cast<uint8_t>(
                (size_t(covered[output_x]) * size_t(255) + sample_count / 2) / sample_count);
        }
    }
    return output;
}

} // namespace Slic3r::dlp
