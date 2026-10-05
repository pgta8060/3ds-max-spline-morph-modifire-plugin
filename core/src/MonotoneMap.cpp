#include "rtp/MonotoneMap.h"

#include "rtp/Math.h"

#include <algorithm>
#include <cmath>

namespace rtp {

namespace {
constexpr double kMinGap = 1e-4;

double wrap01(double t) {
    t = std::fmod(t, 1.0);
    return t < 0 ? t + 1.0 : t;
}
double wrapHalf(double t) {
    t = wrap01(t);
    return t > 0.5 ? t - 1.0 : t;
}
}  // namespace

void MonotoneMap::setIdentity(bool periodic) {
    periodic_ = periodic;
    xs_ = {0.0, 1.0};
    ys_ = {0.0, 1.0};
    buildTangents();
}

void MonotoneMap::setAnchors(std::vector<Anchor> anchors, bool periodic) {
    periodic_ = periodic;
    if (!periodic) {
        anchors.erase(std::remove_if(anchors.begin(), anchors.end(),
                                     [](const Anchor& a) {
                                         return a.w <= kMinGap || a.w >= 1.0 - kMinGap ||
                                                a.u <= kMinGap || a.u >= 1.0 - kMinGap;
                                     }),
                      anchors.end());
        std::stable_sort(anchors.begin(), anchors.end(),
                         [](const Anchor& a, const Anchor& b) { return a.w < b.w; });
        xs_ = {0.0};
        ys_ = {0.0};
        for (const auto& a : anchors) {
            if (a.w - xs_.back() > kMinGap && a.u - ys_.back() > kMinGap) {
                xs_.push_back(a.w);
                ys_.push_back(a.u);
            }
        }
        // The implied (1,1) end anchor must remain reachable.
        while (xs_.size() > 1 && (1.0 - xs_.back() <= kMinGap || 1.0 - ys_.back() <= kMinGap)) {
            xs_.pop_back();
            ys_.pop_back();
        }
        xs_.push_back(1.0);
        ys_.push_back(1.0);
        buildTangents();
        return;
    }

    // Periodic: express every anchor as w + delta with delta in (-0.5, 0.5].
    for (auto& a : anchors) {
        a.w = wrap01(a.w);
        a.u = a.w + wrapHalf(a.u - a.w);
    }
    std::stable_sort(anchors.begin(), anchors.end(),
                     [](const Anchor& a, const Anchor& b) { return a.w < b.w; });
    std::vector<Anchor> kept;
    for (const auto& a : anchors) {
        if (kept.empty() || (a.w - kept.back().w > kMinGap && a.u - kept.back().u > kMinGap))
            kept.push_back(a);
    }
    // Close the cycle: the last anchor must stay below the first one + 1.
    while (kept.size() > 1 && (kept.front().w + 1.0 - kept.back().w <= kMinGap ||
                               kept.front().u + 1.0 - kept.back().u <= kMinGap)) {
        kept.pop_back();
    }
    if (kept.empty()) {
        setIdentity(true);
        return;
    }
    xs_.clear();
    ys_.clear();
    xs_.push_back(kept.back().w - 1.0);
    ys_.push_back(kept.back().u - 1.0);
    for (const auto& a : kept) {
        xs_.push_back(a.w);
        ys_.push_back(a.u);
    }
    xs_.push_back(kept.front().w + 1.0);
    ys_.push_back(kept.front().u + 1.0);
    buildTangents();
}

void MonotoneMap::buildTangents() {
    const size_t n = xs_.size();
    ms_.assign(n, 0.0);
    if (n < 2) return;
    std::vector<double> d(n - 1);
    for (size_t k = 0; k + 1 < n; ++k) d[k] = (ys_[k + 1] - ys_[k]) / (xs_[k + 1] - xs_[k]);

    ms_[0] = d[0];
    ms_[n - 1] = d[n - 2];
    for (size_t k = 1; k + 1 < n; ++k) ms_[k] = (d[k - 1] * d[k] <= 0) ? 0.0 : 0.5 * (d[k - 1] + d[k]);
    if (periodic_ && n > 2) {
        // The outer knots are copies of interior anchors shifted by one period:
        // giving them the same tangents keeps the map C1 across the seam.
        ms_[0] = ms_[n - 2];
        ms_[n - 1] = ms_[1];
    }
    for (size_t k = 0; k + 1 < n; ++k) {
        if (std::fabs(d[k]) < 1e-15) {
            ms_[k] = ms_[k + 1] = 0.0;
            continue;
        }
        const double a = ms_[k] / d[k];
        const double b = ms_[k + 1] / d[k];
        const double s = a * a + b * b;
        if (s > 9.0) {
            const double tau = 3.0 / std::sqrt(s);
            ms_[k] = tau * a * d[k];
            ms_[k + 1] = tau * b * d[k];
        }
    }
}

double MonotoneMap::operator()(double w) const {
    double base = 0.0;
    if (periodic_) {
        const double f = std::floor(w);
        base = f;
        w -= f;
    } else {
        w = clamp(w, 0.0, 1.0);
    }
    const size_t n = xs_.size();
    size_t k = std::upper_bound(xs_.begin(), xs_.end(), w) - xs_.begin();
    k = k == 0 ? 0 : k - 1;
    if (k >= n - 1) k = n - 2;
    const double h = xs_[k + 1] - xs_[k];
    const double t = clamp((w - xs_[k]) / h, 0.0, 1.0);
    const double t2 = t * t, t3 = t2 * t;
    const double h00 = 2 * t3 - 3 * t2 + 1;
    const double h10 = t3 - 2 * t2 + t;
    const double h01 = -2 * t3 + 3 * t2;
    const double h11 = t3 - t2;
    return base + h00 * ys_[k] + h10 * h * ms_[k] + h01 * ys_[k + 1] + h11 * h * ms_[k + 1];
}

}  // namespace rtp
