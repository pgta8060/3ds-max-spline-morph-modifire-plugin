// TopoWeave core - monotone 1D reparameterisation.
//
// Rails drawn across the guides pin "this point of guide A flows into that
// point of guide B". Each guide therefore gets a monotone map from the shared
// column parameter w to its own arc length parameter u. The map interpolates
// the anchors with a Fritsch-Carlson monotone cubic so the flow is smooth and
// never folds back on itself.
#pragma once

#include <vector>

namespace rtp {

class MonotoneMap {
public:
    struct Anchor {
        double w;
        double u;
    };

    MonotoneMap() { setIdentity(false); }

    void setIdentity(bool periodic);
    // Anchors may be given in any order. For open maps the end anchors (0,0)
    // and (1,1) are implied. For periodic maps u is treated modulo 1 and the
    // anchors are unwrapped automatically. Anchors that would break
    // monotonicity are discarded (first come, first kept).
    void setAnchors(std::vector<Anchor> anchors, bool periodic);

    double operator()(double w) const;
    bool periodic() const { return periodic_; }
    int anchorCount() const { return static_cast<int>(xs_.size()); }

private:
    void buildTangents();

    bool periodic_ = false;
    std::vector<double> xs_, ys_, ms_;
};

}  // namespace rtp
