///|/ Minimal binary STL slicer for the standalone DLP CLI.
#include "DLPBinarySTL.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace Slic3r::dlp {

namespace {

struct Vertex { double x, y, z; };
struct Triangle { std::array<Vertex, 3> vertices; };
using Key = std::pair<coord_t, coord_t>;
struct Segment { Key a, b; bool used { false }; };

uint32_t read_u32(std::istream &stream)
{
    std::array<unsigned char, 4> bytes {};
    stream.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
    if (! stream)
        throw std::runtime_error("Unexpected end of binary STL");
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}

float read_f32(std::istream &stream)
{
    const uint32_t bits = read_u32(stream);
    float value;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    if (! std::isfinite(value))
        throw std::runtime_error("Binary STL contains a non-finite coordinate");
    return value;
}

std::vector<Triangle> load(const std::string &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (! stream)
        throw std::runtime_error("Could not open binary STL: " + path);
    std::array<char, 80> header {};
    stream.read(header.data(), header.size());
    if (! stream)
        throw std::runtime_error("Binary STL header is incomplete");
    const uint32_t count = read_u32(stream);
    if (count > 100000000u)
        throw std::runtime_error("Binary STL facet count is unreasonable");
    std::vector<Triangle> triangles;
    triangles.reserve(count);
    for (uint32_t facet = 0; facet < count; ++facet) {
        for (int normal = 0; normal < 3; ++normal)
            (void) read_f32(stream);
        Triangle triangle;
        for (Vertex &vertex : triangle.vertices) {
            vertex.x = read_f32(stream);
            vertex.y = read_f32(stream);
            vertex.z = read_f32(stream);
        }
        std::array<char, 2> attributes {};
        stream.read(attributes.data(), attributes.size());
        if (! stream)
            throw std::runtime_error("Binary STL facet is incomplete");
        triangles.emplace_back(triangle);
    }
    return triangles;
}

Key key(double x, double y)
{
    const Point point = Point::new_scale(x, y);
    // Adjacent STL triangles may calculate their shared plane intersection a
    // few internal units apart. Snap to a 0.0001 mm grid so those endpoints
    // receive the same topology key and form a closed contour.
    constexpr coord_t snap = 100;
    const auto quantize = [snap](coord_t value) {
        return value >= 0 ? ((value + snap / 2) / snap) * snap
                          : ((value - snap / 2) / snap) * snap;
    };
    return { quantize(point.x()), quantize(point.y()) };
}

std::vector<Segment> cut(const std::vector<Triangle> &triangles, double z)
{
    std::vector<Segment> segments;
    for (const Triangle &triangle : triangles) {
        std::array<Vertex, 3> hits {};
        size_t hit_count = 0;
        for (size_t edge = 0; edge < 3; ++edge) {
            const Vertex &a = triangle.vertices[edge];
            const Vertex &b = triangle.vertices[(edge + 1) % 3];
            if (! ((a.z < z && b.z >= z) || (b.z < z && a.z >= z)))
                continue;
            const double ratio = (z - a.z) / (b.z - a.z);
            const Vertex hit { a.x + ratio * (b.x - a.x), a.y + ratio * (b.y - a.y), z };
            if (hit_count == 0 || key(hit.x, hit.y) != key(hits[0].x, hits[0].y))
                hits[hit_count++] = hit;
        }
        if (hit_count == 2) {
            Segment segment { key(hits[0].x, hits[0].y), key(hits[1].x, hits[1].y) };
            if (segment.a != segment.b)
                segments.emplace_back(segment);
        }
    }
    return segments;
}

ExPolygons loops(std::vector<Segment> segments)
{
    std::multimap<Key, size_t> adjacency;
    for (size_t index = 0; index < segments.size(); ++index) {
        adjacency.emplace(segments[index].a, index);
        adjacency.emplace(segments[index].b, index);
    }
    ExPolygons result;
    for (size_t start_segment = 0; start_segment < segments.size(); ++start_segment) {
        if (segments[start_segment].used)
            continue;
        Points points;
        Key start = segments[start_segment].a;
        Key current = start;
        size_t segment_index = start_segment;
        for (size_t guard = 0; guard <= segments.size(); ++guard) {
            Segment &segment = segments[segment_index];
            if (segment.used)
                break;
            segment.used = true;
            points.emplace_back(current.first, current.second);
            current = segment.a == current ? segment.b : segment.a;
            if (current == start)
                break;
            bool found = false;
            const auto range = adjacency.equal_range(current);
            for (auto it = range.first; it != range.second; ++it) {
                if (! segments[it->second].used) {
                    segment_index = it->second;
                    found = true;
                    break;
                }
            }
            if (! found) {
                points.clear();
                break;
            }
        }
        if (points.size() >= 3 && current == start)
            result.emplace_back(std::move(points));
    }
    return result;
}

} // namespace

std::vector<Layer> slice_binary_stl(const std::string &path, const ProcessConfig &process,
                                    const CancelFn &cancel)
{
    if (! std::isfinite(process.layer_height_mm) || process.layer_height_mm <= 0.0)
        throw std::invalid_argument("Layer height must be greater than zero");
    const std::vector<Triangle> triangles = load(path);
    double max_z = 0.0;
    for (const Triangle &triangle : triangles)
        for (const Vertex &vertex : triangle.vertices)
            max_z = std::max(max_z, vertex.z);
    const double count_d = std::ceil(max_z / process.layer_height_mm);
    if (count_d > static_cast<double>(std::numeric_limits<uint32_t>::max()))
        throw std::overflow_error("STL requires too many DLP layers");
    std::vector<Layer> layers;
    layers.reserve(static_cast<size_t>(count_d));
    for (uint32_t index = 0; index < static_cast<uint32_t>(count_d); ++index) {
        if (cancel)
            cancel();
        Layer layer;
        layer.index = index;
        layer.height_mm = (static_cast<double>(index) + 0.5) * process.layer_height_mm;
        layer.exposure_time_s = index < process.initial_layer_count ?
            process.initial_exposure_time_s : process.exposure_time_s;
        layer.model = loops(cut(triangles, layer.height_mm));
        layers.emplace_back(std::move(layer));
    }
    return layers;
}

} // namespace Slic3r::dlp
