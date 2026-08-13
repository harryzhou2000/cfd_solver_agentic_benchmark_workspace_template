#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cfd {

constexpr int kDim = 2;          // spatial dimension of the solved equations
constexpr int kNC = 4;           // conservative variables per cell
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Small vector helpers
// ---------------------------------------------------------------------------
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) {
        x += o.x;
        y += o.y;
        return *this;
    }
    Vec2& operator-=(const Vec2& o) {
        x -= o.x;
        y -= o.y;
        return *this;
    }

    double dot(const Vec2& o) const { return x * o.x + y * o.y; }
    double cross(const Vec2& o) const { return x * o.y - y * o.x; }
    double norm2() const { return x * x + y * y; }
    double norm() const { return std::sqrt(norm2()); }
    Vec2 normalized() const {
        const double n = norm();
        return n > 0 ? Vec2{x / n, y / n} : Vec2{0.0, 0.0};
    }
};

inline Vec2 operator*(double s, const Vec2& v) { return v * s; }

// Column vector of length n (n = 4 for the conservative state).
struct VecN {
    double v[kNC] = {0.0, 0.0, 0.0, 0.0};

    double& operator[](int i) { return v[i]; }
    const double& operator[](int i) const { return v[i]; }
    VecN operator+(const VecN& o) const {
        VecN r;
        for (int i = 0; i < kNC; ++i) r.v[i] = v[i] + o.v[i];
        return r;
    }
    VecN operator-(const VecN& o) const {
        VecN r;
        for (int i = 0; i < kNC; ++i) r.v[i] = v[i] - o.v[i];
        return r;
    }
    VecN operator*(double s) const {
        VecN r;
        for (int i = 0; i < kNC; ++i) r.v[i] = v[i] * s;
        return r;
    }
    double dot(const VecN& o) const {
        double s = 0.0;
        for (int i = 0; i < kNC; ++i) s += v[i] * o.v[i];
        return s;
    }
    double norm2() const { return dot(*this); }
};

// Dense n x n matrix, n = kNC.
struct MatN {
    double m[kNC][kNC] = {{0}};

    double* operator[](int i) { return m[i]; }
    const double* operator[](int i) const { return m[i]; }

    MatN operator*(const MatN& o) const {
        MatN r;
        for (int i = 0; i < kNC; ++i)
            for (int k = 0; k < kNC; ++k)
                for (int j = 0; j < kNC; ++j) r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }
    VecN operator*(const VecN& x) const {
        VecN r;
        for (int i = 0; i < kNC; ++i)
            for (int j = 0; j < kNC; ++j) r.v[i] += m[i][j] * x.v[j];
        return r;
    }
    MatN operator+(const MatN& o) const {
        MatN r;
        for (int i = 0; i < kNC; ++i)
            for (int j = 0; j < kNC; ++j) r.m[i][j] = m[i][j] + o.m[i][j];
        return r;
    }
    MatN operator-(const MatN& o) const {
        MatN r;
        for (int i = 0; i < kNC; ++i)
            for (int j = 0; j < kNC; ++j) r.m[i][j] = m[i][j] - o.m[i][j];
        return r;
    }
    MatN operator*(double s) const {
        MatN r;
        for (int i = 0; i < kNC; ++i)
            for (int j = 0; j < kNC; ++j) r.m[i][j] = m[i][j] * s;
        return r;
    }

    // Solve A x = b with Gauss elimination and partial pivoting (4x4 only).
    VecN solve(const VecN& b) const {
        double a[kNC][kNC];
        double rhs[kNC];
        for (int i = 0; i < kNC; ++i) {
            rhs[i] = b.v[i];
            for (int j = 0; j < kNC; ++j) a[i][j] = m[i][j];
        }
        for (int col = 0; col < kNC; ++col) {
            int piv = col;
            double best = std::fabs(a[col][col]);
            for (int r = col + 1; r < kNC; ++r) {
                if (std::fabs(a[r][col]) > best) {
                    best = std::fabs(a[r][col]);
                    piv = r;
                }
            }
            if (best < 1e-300) {
                throw std::runtime_error("singular 4x4 system in MatN::solve");
            }
            if (piv != col) {
                for (int j = 0; j < kNC; ++j) std::swap(a[col][j], a[piv][j]);
                std::swap(rhs[col], rhs[piv]);
            }
            const double d = a[col][col];
            for (int r = col + 1; r < kNC; ++r) {
                const double f = a[r][col] / d;
                if (f == 0.0) continue;
                for (int j = col; j < kNC; ++j) a[r][j] -= f * a[col][j];
                rhs[r] -= f * rhs[col];
            }
        }
        double x[kNC];
        for (int r = kNC - 1; r >= 0; --r) {
            double s = rhs[r];
            for (int j = r + 1; j < kNC; ++j) s -= a[r][j] * x[j];
            x[r] = s / a[r][r];
        }
        VecN out;
        for (int i = 0; i < kNC; ++i) out.v[i] = x[i];
        return out;
    }
};

// ---------------------------------------------------------------------------
// Boundary condition tags
// ---------------------------------------------------------------------------
enum class BcType : int {
    Interior = 0,
    Farfield = 1,
    SlipWall = 2,
    NoSlipAdiabaticWall = 3,
    Unknown = 4
};

enum class FluxScheme { Rusanov, Roe };

inline BcType bc_from_string(const std::string& s) {
    if (s == "farfield") return BcType::Farfield;
    if (s == "slip_wall") return BcType::SlipWall;
    if (s == "no_slip_adiabatic_wall") return BcType::NoSlipAdiabaticWall;
    return BcType::Unknown;
}

inline const char* bc_to_string(BcType t) {
    switch (t) {
        case BcType::Farfield:
            return "farfield";
        case BcType::SlipWall:
            return "slip_wall";
        case BcType::NoSlipAdiabaticWall:
            return "no_slip_adiabatic_wall";
        case BcType::Interior:
            return "interior";
        default:
            return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Perfect gas model (configurable gamma, R, Prandtl)
// ---------------------------------------------------------------------------
struct GasModel {
    double gamma = 1.4;
    double R = 1.0;
    double prandtl = 0.72;

    double cp() const { return gamma * R / (gamma - 1.0); }
    double cv() const { return R / (gamma - 1.0); }
};

// Conservative <-> primitive conversions. U = [rho, rho*u, rho*v, rho*E].
struct Primitive {
    double rho = 1.0;
    double u = 0.0;
    double v = 0.0;
    double p = 1.0;
};

inline Primitive cons_to_prim(const VecN& u, const GasModel& gas) {
    Primitive q;
    q.rho = u[0];
    const double inv_rho = 1.0 / std::max(q.rho, 1e-300);
    q.u = u[1] * inv_rho;
    q.v = u[2] * inv_rho;
    const double e = (u[3] - 0.5 * q.rho * (q.u * q.u + q.v * q.v)) * inv_rho;
    q.p = (gas.gamma - 1.0) * q.rho * e;
    return q;
}

inline VecN prim_to_cons(const Primitive& q, const GasModel& gas) {
    VecN u;
    u[0] = q.rho;
    u[1] = q.rho * q.u;
    u[2] = q.rho * q.v;
    const double e = q.p / ((gas.gamma - 1.0) * q.rho);
    u[3] = q.rho * (e + 0.5 * (q.u * q.u + q.v * q.v));
    return u;
}

inline double sound_speed(const Primitive& q, const GasModel& gas) {
    return std::sqrt(std::max(gas.gamma * q.p / std::max(q.rho, 1e-300), 0.0));
}

inline double temperature_of(const Primitive& q, const GasModel& gas) {
    return q.p / (std::max(q.rho, 1e-300) * gas.R);
}

inline double mach_of(const Primitive& q, const GasModel& gas) {
    const double v2 = q.u * q.u + q.v * q.v;
    return std::sqrt(v2) / std::max(sound_speed(q, gas), 1e-300);
}

// Inviscid flux F(U) for the first direction (x).
inline VecN inviscid_flux_x(const Primitive& q, const GasModel& gas) {
    VecN f;
    const double rho = q.rho;
    const double p = q.p;
    f[0] = rho * q.u;
    f[1] = rho * q.u * q.u + p;
    f[2] = rho * q.u * q.v;
    f[3] = q.u * (rho * (q.p / ((gas.gamma - 1.0) * rho) +
                        0.5 * (q.u * q.u + q.v * q.v)) +
                 p);
    return f;
}

// Rotated inviscid flux: F(U) * nx + G(U) * ny.
inline VecN inviscid_flux_normal(const Primitive& q, const Vec2& n,
                                 const GasModel& gas) {
    VecN f;
    const double rho = q.rho;
    const double un = q.u * n.x + q.v * n.y;
    const double p = q.p;
    f[0] = rho * un;
    f[1] = rho * un * q.u + p * n.x;
    f[2] = rho * un * q.v + p * n.y;
    f[3] = un * (rho * (q.p / ((gas.gamma - 1.0) * rho) +
                        0.5 * (q.u * q.u + q.v * q.v)) +
                 p);
    return f;
}

inline bool positive_state(const VecN& u, const GasModel& gas) {
    if (!std::isfinite(u[0]) || !std::isfinite(u[1]) || !std::isfinite(u[2]) ||
        !std::isfinite(u[3]))
        return false;
    const Primitive q = cons_to_prim(u, gas);
    return q.rho > 0.0 && q.p > 0.0;
}

}  // namespace cfd
