// Tiny software renderer (z-buffered triangles + lines -> PNG). Used by the
// demo to produce verification images without any third-party dependency.
#pragma once

#include "Shapes.h"
#include "rtp/PolyMesh.h"
#include "rtp/Polyline.h"

#include <string>
#include <vector>

namespace rtp {
namespace kit {

struct Rgb {
    double r = 0, g = 0, b = 0;
};

class Canvas {
public:
    explicit Canvas(const Camera& cam);

    void clearGradient(const Rgb& top, const Rgb& bottom);
    // Shaded opaque mesh (writes depth).
    void drawSurface(const TriMesh& mesh, const Rgb& base);
    // Translucent polygon fill (depth tested against the surface, no depth write).
    void drawPolyFill(const PolyMesh& mesh, const Rgb& color, double alpha);
    // Wireframe of a polygon mesh.
    void drawPolyWire(const PolyMesh& mesh, const Rgb& color, double width);
    void drawPolyline(const Polyline& line, const Rgb& color, double width, bool depthTest = true);
    void drawPoint(const Vec3& p, const Rgb& color, double radius, bool depthTest = true);
    void drawLine(const Vec3& a, const Vec3& b, const Rgb& color, double width, bool depthTest);

    bool writePng(const std::string& path) const;

private:
    void blend(int x, int y, const Rgb& c, double alpha);
    void disk(double cx, double cy, double depth, const Rgb& c, double radius, bool depthTest);

    Camera cam_;
    int w_, h_;
    std::vector<Rgb> color_;
    std::vector<double> depth_;
    double depthBias_;
};

bool writePng(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb);

}  // namespace kit
}  // namespace rtp
