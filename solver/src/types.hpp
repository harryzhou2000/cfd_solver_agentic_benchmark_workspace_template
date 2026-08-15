#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

namespace cfd {

// --- Numeric types ---
using Real = double;
// Signed index type: keeps -1 sentinels and "x < 0" checks well-defined.
// Mesh sizes are O(10^4) cells, so int64_t is more than sufficient.
using Int = std::int64_t;

// --- Sentinel index ---
// Central sentinel for "no such index" (unmapped cell, boundary face with no
// right cell, patch not found, ...). Signed Int makes this well-defined.
constexpr Int INVALID_INDEX = -1;

// --- Small vectors ---
template <typename T, int N>
struct Vec {
    std::array<T, N> v{};
    Vec() = default;
    explicit Vec(T val) { v.fill(val); }
    Vec(std::initializer_list<T> il) {
        int i = 0;
        for (auto x : il) { if (i < N) v[i++] = x; }
    }
    T& operator[](int i) { return v[i]; }
    const T& operator[](int i) const { return v[i]; }
    T* data() { return v.data(); }
    const T* data() const { return v.data(); }
    Vec operator+(const Vec& o) const { Vec r; for (int i=0;i<N;++i) r[i]=v[i]+o[i]; return r; }
    Vec operator-(const Vec& o) const { Vec r; for (int i=0;i<N;++i) r[i]=v[i]-o[i]; return r; }
    Vec operator*(T s) const { Vec r; for (int i=0;i<N;++i) r[i]=v[i]*s; return r; }
    Vec operator/(T s) const { Vec r; for (int i=0;i<N;++i) r[i]=v[i]/s; return r; }
    Vec& operator+=(const Vec& o) { for (int i=0;i<N;++i) v[i]+=o[i]; return *this; }
    Vec& operator-=(const Vec& o) { for (int i=0;i<N;++i) v[i]-=o[i]; return *this; }
    Vec& operator*=(T s) { for (int i=0;i<N;++i) v[i]*=s; return *this; }
    T dot(const Vec& o) const { T s=0; for(int i=0;i<N;++i)s+=v[i]*o[i]; return s; }
    T norm() const { return std::sqrt(dot(*this)); }
    T squaredNorm() const { return dot(*this); }
};

template <typename T, int N>
inline Vec<T,N> operator*(T s, const Vec<T,N>& v) { return v * s; }

using Vec2 = Vec<Real, 2>;
using Vec3 = Vec<Real, 3>;
using Vec4 = Vec<Real, 4>;

// --- Conservative state (2D): [rho, rho*u, rho*v, rho*E] ---
using Conserved = Vec4;

// --- Primitive state (2D): [rho, u, v, p] ---
using Primitive = Vec4;

// --- Face normal components ---
struct FaceNormal {
    Real nx, ny;  // unit normal components (pointing from left to right cell)
    Real area;    // face area/length
};

} // namespace cfd
