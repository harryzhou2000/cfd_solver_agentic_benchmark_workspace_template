#include <mpi.h>
#include <omp.h>
#include <cgnslib.h>
#include <metis.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Aurora-FV is a deliberately small, self-contained cell-centred finite-volume
// implementation.  The mesh and equation interfaces are kept independent of
// the supplied cases so that a 3-D element reader, a different EOS, or a RANS
// model can be added without changing the residual/time-integration drivers.

using json = nlohmann::json;
namespace fs = std::filesystem;
constexpr double kFloor = 1.0e-12;

struct V2 {
  double x = 0.0, y = 0.0;
  V2 operator+(const V2& b) const { return {x + b.x, y + b.y}; }
  V2 operator-(const V2& b) const { return {x - b.x, y - b.y}; }
  V2 operator*(double a) const { return {x * a, y * a}; }
  V2& operator+=(const V2& b) { x += b.x; y += b.y; return *this; }
};
static double dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
static double cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
static double norm(V2 a) { return std::sqrt(dot(a, a)); }

struct U {
  double rho = 1.0, rhou = 0.0, rhov = 0.0, rhoE = 1.0;
  U& operator+=(const U& b) { rho += b.rho; rhou += b.rhou; rhov += b.rhov; rhoE += b.rhoE; return *this; }
  U& operator-=(const U& b) { rho -= b.rho; rhou -= b.rhou; rhov -= b.rhov; rhoE -= b.rhoE; return *this; }
  U operator*(double a) const { return {rho * a, rhou * a, rhov * a, rhoE * a}; }
};
static U operator+(U a, const U& b) { a += b; return a; }
static U operator-(U a, const U& b) { a -= b; return a; }
static U operator*(double a, const U& b) { return b * a; }

struct Prim { double rho = 1.0, u = 0.0, v = 0.0, p = 1.0, T = 1.0; };
struct Grad { double drx=0, dry=0, dux=0, duy=0, dvx=0, dvy=0, dTx=0, dTy=0, theta=1; };
struct Face {
  int a = -1, b = -1, n0 = -1, n1 = -1;
  V2 center, normal;
  double area = 0.0;
  std::string tag;
};
struct Cell {
  std::vector<int> nodes;
  std::vector<int> faces;
  std::vector<int> nbr;
  V2 center;
  double area = 0.0;
  // Geometry used only by the laminar wall-compatible initializer.  The
  // nearest stationary no-slip face is cached during serial preprocessing so
  // every rank can initialize all near-wall layers without replicating the
  // global mesh during iterations.
  double wall_distance = -1.0;
  V2 wall_normal;
};
struct Mesh { std::vector<V2> points; std::vector<Cell> cells; std::vector<Face> faces; };

struct EdgeKey {
  int a = -1, b = -1;
  bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
  bool operator<(const EdgeKey& o) const { return a < o.a || (a == o.a && b < o.b); }
};
static EdgeKey edge_key(int a, int b) { return a < b ? EdgeKey{a,b} : EdgeKey{b,a}; }
struct EdgeHash { std::size_t operator()(const EdgeKey& k) const { return (std::size_t)k.a * 1000003u ^ (std::size_t)k.b; } };
struct PointKey {
  std::int64_t x = 0, y = 0;
  bool operator==(const PointKey& o) const { return x == o.x && y == o.y; }
};
struct PointHash { std::size_t operator()(const PointKey& k) const { return (std::size_t)k.x * 1000003u ^ (std::size_t)k.y; } };

struct Case {
  json raw;
  std::string id, mode;
  double gamma=1.4, R=1.0, Pr=0.72, rhoInf=1.0, UInf=1.0, pInf=1.0;
  double aoa=0.0, Re=0.0, refL=1.0, refA=1.0, cx=0.0, cy=0.0;
  bool transient=false;
  double dt=0.0, finalTime=0.0, cfl0=1.0, cflMax=100.0;
  int maxSteps=1000, rampSteps=1000, minInner=3, maxInner=50;
  double outerTarget=4.0, innerTarget=1.0e-2, steadyOmega=0.10;
  // A force-stable residual plateau is useful diagnostic information, but it
  // is not a substitute for the case-specified residual target.  Production
  // steady solves therefore consume their supplied step budget by default;
  // this opt-in is reserved for short exploratory runs.
  bool steadyPlateauStop=false;
  // Optional frozen 4x4 local residual block for steady pseudo-time
  // relaxation.  The default zero preserves the scalar Jacobi path; a
  // positive blend may be selected for diagnostics or difficult viscous
  // steady solves without changing the transient production operator.
  double steadyBlockCoupling=0.0;
  // A transient production solve uses the same bounded piecewise-linear
  // reconstruction as the stated second-order spatial method.  A reduced
  // slope may be selected explicitly for diagnostic continuation, but must
  // not silently become the benchmark operator.
  double transientSlope=1.0, transientGain=0.4, transientBlockCoupling=0.0;
  double transientSeed=1.0e-4;
  // Boundary tags are queried on every residual face.  Keep a native lookup
  // table alongside the source JSON so the hot loop does not repeatedly walk
  // nlohmann::json objects.
  std::shared_ptr<const std::unordered_map<std::string,std::string>> boundary_types;
  double rusanovScale=1.0;
  // At Mach 0.1, an acoustic-speed Rusanov signal is an order of magnitude
  // larger than the material wave speed.  This cutoff applies only to the
  // transient low-Mach path and scales the artificial acoustic diffusion;
  // the physical flux and the user-supplied Rusanov scale remain unchanged.
  double transientLowMachCutoff=0.40;
  std::string inviscidFlux="rusanov";
  // During a difficult HLLC transient startup, the nonlinear inner solve may
  // follow a bounded Rusanov-to-HLLC flux homotopy.  The requested physical
  // residual is always reassembled with this factor equal to one before a
  // step can be accepted; zero/partial values are continuation operators
  // only.
  double hllcFluxBlend=1.0;
  // The strict global residual Armijo safeguard is the production default.
  // A positivity-only diagnostic mode may be selected explicitly to separate
  // globalization effects from the final physical-step acceptance criterion.
  bool transientLineSearch=true;
  bool initializeWallState=true;
};

static void cgns_check(int code, const char* where) {
  if (code) throw std::runtime_error(std::string(where) + ": " + cg_get_error());
}
static PointKey point_key(V2 p) {
  // The two cylinder zones use matching coordinates but distinct local node
  // numbers.  Quantisation merges those interface nodes without assuming zone
  // names or connectivity-section numbering.
  return {static_cast<std::int64_t>(std::llround(p.x * 1.0e10)),
          static_cast<std::int64_t>(std::llround(p.y * 1.0e10))};
}

static Case read_case(const fs::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open case file " + path.string());
  Case c; in >> c.raw;
  if (c.raw.value("schema_version", 0) != 1) throw std::runtime_error("only schema_version 1 is supported");
  c.id = c.raw.at("case_id").get<std::string>();
  c.mode = c.raw.at("physics").at("mode").get<std::string>();
  const auto g = c.raw.at("gas"), q = c.raw.at("freestream"), r = c.raw.at("reference");
  const auto phy = c.raw.at("physics"), rc = c.raw.at("run_control");
  c.gamma = g.at("gamma"); c.R = g.at("R"); c.Pr = g.at("prandtl");
  c.rhoInf = q.at("rho"); c.UInf = q.at("velocity_magnitude"); c.pInf = q.at("pressure");
  c.aoa = q.at("aoa_degrees").get<double>() * M_PI / 180.0;
  c.refL = r.at("length"); c.refA = r.at("area");
  if (r.contains("moment_center")) { c.cx = r.at("moment_center")[0]; c.cy = r.at("moment_center")[1]; }
  c.Re = phy.value("reynolds", 0.0);
  auto boundary_lookup=std::make_shared<std::unordered_map<std::string,std::string>>();
  for (const auto& item : c.raw.at("boundary_conditions").items())
    boundary_lookup->emplace(item.key(), item.value().get<std::string>());
  c.boundary_types=std::move(boundary_lookup);
  c.transient = rc.at("type").get<std::string>() == "transient";
  c.maxSteps = rc.value("max_steps", c.transient ? 1 : 2000);
  c.dt = rc.value("time_step", 0.0); c.finalTime = rc.value("final_time", 0.0);
  if (c.transient && c.dt > 0.0 && c.finalTime > 0.0) c.maxSteps = static_cast<int>(std::llround(c.finalTime / c.dt));
  c.cfl0 = rc.value("cfl_initial", 1.0); c.cflMax = rc.value("cfl_max", c.cfl0);
  c.rampSteps = rc.value("pseudo_cfl_ramp_steps", 1000);
  c.minInner = rc.value("min_inner_iterations", 3); c.maxInner = rc.value("max_inner_iterations", 50);
  c.outerTarget = rc.value("residual_reduction_target", c.transient ? 0.0 : 4.0);
  c.innerTarget = rc.value("inner_residual_reduction_target", c.transient ? 1.0e-3 : 1.0e-2);
  c.rusanovScale = rc.value("rusanov_dissipation_scale", 1.0);
  if (!(c.rusanovScale > 0.0) || !std::isfinite(c.rusanovScale)) throw std::runtime_error("rusanov_dissipation_scale must be finite and positive");
  if (c.mode != "inviscid" && c.mode != "laminar") throw std::runtime_error("unsupported physics.mode: " + c.mode);
  c.steadyOmega = rc.value("steady_relaxation_omega", c.mode=="laminar" ? 1.0e-2 : 5.0e-2);
  if (!(c.steadyOmega > 0.0) || !std::isfinite(c.steadyOmega)) throw std::runtime_error("steady_relaxation_omega must be finite and positive");
  c.steadyBlockCoupling = rc.value("steady_block_coupling", 0.0);
  if (!std::isfinite(c.steadyBlockCoupling) || c.steadyBlockCoupling < 0.0 || c.steadyBlockCoupling > 1.0)
    throw std::runtime_error("steady_block_coupling must be finite and in [0,1]");
  if (c.transient && (!(c.dt > 0.0) || !(c.finalTime > 0.0))) throw std::runtime_error("transient case needs positive time_step/final_time");
  if (!c.raw.contains("boundary_conditions") || !c.raw.at("boundary_conditions").is_object())
    throw std::runtime_error("boundary_conditions must be an object");
  static const std::set<std::string> supported_bc={"farfield","slip_wall","no_slip_adiabatic_wall"};
  for (const auto& item : c.raw.at("boundary_conditions").items()) {
    if (!item.value().is_string()) throw std::runtime_error("boundary condition for " + item.key() + " must be a string");
    const std::string value=item.value().get<std::string>();
    if (!supported_bc.count(value)) throw std::runtime_error("unsupported boundary condition '" + value + "' for " + item.key());
  }
  return c;
}

static Mesh read_cgns(const fs::path& path) {
  int fn = 0; cgns_check(cg_open(path.string().c_str(), CG_MODE_READ, &fn), "cg_open");
  try {
    int nb = 0; cgns_check(cg_nbases(fn, &nb), "cg_nbases");
    if (nb < 1) throw std::runtime_error("CGNS file has no base");
    int nz = 0; cgns_check(cg_nzones(fn, 1, &nz), "cg_nzones");
    Mesh m;
    std::unordered_map<PointKey,int,PointHash> point_ids;
    std::map<EdgeKey,std::string> boundary_edges;
    for (int z=1; z<=nz; ++z) {
      char zone_name[33] = {}; cgsize_t size[9] = {};
      cgns_check(cg_zone_read(fn,1,z,zone_name,size), "cg_zone_read");
      std::vector<double> x(static_cast<std::size_t>(size[0])), y(static_cast<std::size_t>(size[0]));
      cgsize_t rmin[3]={1,1,1}, rmax[3]={size[0],1,1};
      cgns_check(cg_coord_read(fn,1,z,"CoordinateX",RealDouble,rmin,rmax,x.data()), "CoordinateX");
      cgns_check(cg_coord_read(fn,1,z,"CoordinateY",RealDouble,rmin,rmax,y.data()), "CoordinateY");
      std::vector<int> local_to_global(x.size());
      for (std::size_t i=0; i<x.size(); ++i) {
        V2 p{x[i],y[i]}; PointKey key = point_key(p); auto it = point_ids.find(key);
        if (it == point_ids.end()) { int id = static_cast<int>(m.points.size()); point_ids.emplace(key,id); m.points.push_back(p); local_to_global[i] = id; }
        else local_to_global[i] = it->second;
      }
      int ns = 0; cgns_check(cg_nsections(fn,1,z,&ns), "cg_nsections");
      for (int s=1; s<=ns; ++s) {
        char section_name[33] = {}; ElementType_t et = ElementTypeNull; cgsize_t start=0,end=0; int boundary=0, parent=0;
        cgns_check(cg_section_read(fn,1,z,s,section_name,&et,&start,&end,&boundary,&parent), "cg_section_read");
        int npe = 0; cg_npe(et,&npe); if (npe <= 0 || end < start) continue;
        const std::size_t ne = static_cast<std::size_t>(end-start+1);
        std::vector<cgsize_t> conn(ne*static_cast<std::size_t>(npe));
        cgns_check(cg_elements_read(fn,1,z,s,conn.data(),nullptr), "cg_elements_read");
        const std::string sn(section_name);
        if (et == TRI_3 || et == QUAD_4) {
          for (std::size_t e=0; e<ne; ++e) {
            Cell cell; int nloc = et == TRI_3 ? 3 : 4; cell.nodes.reserve(nloc);
            for (int k=0; k<nloc; ++k) cell.nodes.push_back(local_to_global[static_cast<std::size_t>(conn[e*npe+k]-1)]);
            m.cells.push_back(std::move(cell));
          }
        } else if (et == BAR_2) {
          // Volume/interface connector sections are named con-* in the
          // supplied cylinder.  They are intentionally left untagged; the
          // coincident volume edges are paired geometrically below.
          if (sn.rfind("con-",0) != 0) {
            for (std::size_t e=0; e<ne; ++e) {
              int a = local_to_global[static_cast<std::size_t>(conn[e*npe]-1)];
              int b = local_to_global[static_cast<std::size_t>(conn[e*npe+1]-1)];
              boundary_edges[edge_key(a,b)] = sn;
            }
          }
        }
      }
    }
    cgns_check(cg_close(fn), "cg_close"); fn = 0;
    if (m.cells.empty()) throw std::runtime_error("CGNS mesh contains no TRI_3/QUAD_4 volume cells");

    std::unordered_map<EdgeKey,int,EdgeHash> edge_to_face;
    for (int ci=0; ci<static_cast<int>(m.cells.size()); ++ci) {
      Cell& c = m.cells[ci];
      for (std::size_t k=0; k<c.nodes.size(); ++k) {
        int a=c.nodes[k], b=c.nodes[(k+1)%c.nodes.size()]; EdgeKey key=edge_key(a,b);
        auto it=edge_to_face.find(key);
        if (it==edge_to_face.end()) {
          Face f; f.a=ci; f.n0=a; f.n1=b; f.center=(m.points[a]+m.points[b])*0.5; f.tag=boundary_edges[key];
          int id=static_cast<int>(m.faces.size()); m.faces.push_back(f); edge_to_face.emplace(key,id);
        } else {
          Face& f=m.faces[it->second]; if (f.b < 0) f.b=ci;
        }
      }
    }
    for (Cell& c : m.cells) {
      V2 sum{}; for (int n:c.nodes) sum += m.points[n]; c.center=sum*(1.0/static_cast<double>(c.nodes.size()));
      double twice=0; for (std::size_t k=0;k<c.nodes.size();++k) twice += cross(m.points[c.nodes[k]],m.points[c.nodes[(k+1)%c.nodes.size()]]);
      c.area=std::max(0.5*std::abs(twice),1.0e-14);
    }
    for (int fi=0; fi<static_cast<int>(m.faces.size()); ++fi) {
      Face& f=m.faces[fi]; f.center=(m.points[f.n0]+m.points[f.n1])*0.5; V2 d=m.points[f.n1]-m.points[f.n0]; f.normal={d.y,-d.x}; f.area=norm(f.normal);
      if (dot(f.normal,f.center-m.cells[f.a].center)<0) f.normal=f.normal*(-1.0);
      m.cells[f.a].faces.push_back(fi);
      if (f.b>=0) { m.cells[f.a].nbr.push_back(f.b); m.cells[f.b].faces.push_back(fi); m.cells[f.b].nbr.push_back(f.a); }
    }
    for (Cell& c:m.cells) { std::sort(c.nbr.begin(),c.nbr.end()); c.nbr.erase(std::unique(c.nbr.begin(),c.nbr.end()),c.nbr.end()); }
    return m;
  } catch (...) { if (fn) cg_close(fn); throw; }
}

static Prim primitive(U q, const Case& c) {
  q.rho=std::max(q.rho,kFloor); Prim w; w.rho=q.rho; w.u=q.rhou/q.rho; w.v=q.rhov/q.rho;
  w.p=std::max((c.gamma-1.0)*(q.rhoE-0.5*q.rho*(w.u*w.u+w.v*w.v)),kFloor);
  w.T=std::max(w.p/(w.rho*c.R),kFloor); return w;
}
static U conservative(Prim w, const Case& c) { return {w.rho,w.rho*w.u,w.rho*w.v,w.p/(c.gamma-1.0)+0.5*w.rho*(w.u*w.u+w.v*w.v)}; }
static U freestream(const Case& c) { return conservative({c.rhoInf,c.UInf*std::cos(c.aoa),c.UInf*std::sin(c.aoa),c.pInf,c.pInf/(c.rhoInf*c.R)},c); }
static void enforce_positive(U& q, const Case& c) {
  if (!std::isfinite(q.rho) || !std::isfinite(q.rhou) || !std::isfinite(q.rhov) || !std::isfinite(q.rhoE)) {
    q = freestream(c);
    return;
  }
  // Keep the update conservative.  Only repair a genuinely inadmissible
  // trial state; do not project valid shocks or boundary layers onto
  // arbitrary freestream-relative boxes.
  const double rho=std::max(q.rho,kFloor);
  const double kinetic=0.5*(q.rhou*q.rhou+q.rhov*q.rhov)/rho;
  const double pressure=(c.gamma-1.0)*(q.rhoE-kinetic);
  if (rho==q.rho && std::isfinite(pressure) && pressure>kFloor) return;
  const double p=std::max(pressure,kFloor);
  q.rho=rho;
  q.rhoE=kinetic+p/(c.gamma-1.0);
}
static U physical_flux(U q, V2 n, const Case& c) {
  Prim w=primitive(q,c); double vn=w.u*n.x+w.v*n.y;
  return {q.rho*vn, q.rhou*vn+w.p*n.x, q.rhov*vn+w.p*n.y, (q.rhoE+w.p)*vn};
}
// Frozen Rusanov signal used both by the residual and by the optional local
// block preconditioner.  Keeping this calculation in one place is important:
// a block solve based on a different acoustic radius can easily over-correct
// the low-Mach pressure modes that dominate the transient inner iteration.
static double rusanov_signal(U L, U R, V2 n, const Case& c) {
  double A=std::max(norm(n),kFloor), invA=1.0/A; V2 nh=n*invA; Prim l=primitive(L,c),r=primitive(R,c);
  double al=std::sqrt(c.gamma*l.p/l.rho), ar=std::sqrt(c.gamma*r.p/r.rho);
  const double un_l=std::abs(l.u*nh.x+l.v*nh.y), un_r=std::abs(r.u*nh.x+r.v*nh.y);
  // Use an all-speed Rusanov signal for the low-Mach transient while keeping
  // the supplied dissipation-scale control literal.  The Mach cutoff limits
  // only the artificial acoustic part of the signal; the physical flux is
  // still the conservative Rusanov/Lax--Friedrichs flux.
  double acoustic_factor=1.0;
  if(c.transient){
    const double ml=std::sqrt(l.u*l.u+l.v*l.v)/std::max(al,kFloor);
    const double mr=std::sqrt(r.u*r.u+r.v*r.v)/std::max(ar,kFloor);
    acoustic_factor=std::clamp(std::max({ml,mr,c.transientLowMachCutoff}),c.transientLowMachCutoff,1.0);
  }
  return std::max(un_l+c.rusanovScale*acoustic_factor*al,un_r+c.rusanovScale*acoustic_factor*ar);
}
static U rusanov(U L, U R, V2 n, const Case& c) {
  const double A=std::max(norm(n),kFloor);
  const double s=rusanov_signal(L,R,n,c);
  return 0.5*(physical_flux(L,n,c)+physical_flux(R,n,c))-0.5*s*A*(R-L);
}

// HLLC is retained as an explicit, bounded alternative for low-Mach
// transient wake work.  Rusanov remains the default and is used whenever the
// contact-wave estimate is degenerate or produces a non-admissible star state.
static U hllc(U L, U R, V2 n, const Case& c) {
  const double A=std::max(norm(n),kFloor);
  const V2 nh=n*(1.0/A);
  const Prim l=primitive(L,c), r=primitive(R,c);
  const double al=std::sqrt(c.gamma*l.p/std::max(l.rho,kFloor));
  const double ar=std::sqrt(c.gamma*r.p/std::max(r.rho,kFloor));
  const double un_l=l.u*nh.x+l.v*nh.y, un_r=r.u*nh.x+r.v*nh.y;
  const double sl=std::min(un_l-al,un_r-ar), sr=std::max(un_l+al,un_r+ar);
  const double denom=l.rho*(sl-un_l)-r.rho*(sr-un_r);
  if(!std::isfinite(denom)||std::abs(denom)<1.0e-12*std::max({l.rho,r.rho,1.0})) return rusanov(L,R,n,c);
  const double sm=(r.p-l.p+l.rho*un_l*(sl-un_l)-r.rho*un_r*(sr-un_r))/denom;
  const double pstar=l.p+l.rho*(sl-un_l)*(sm-un_l);
  if(!std::isfinite(sm)||!std::isfinite(pstar)||pstar<=1.0e-10*std::max(c.pInf,1.0)||sl>=sr) return rusanov(L,R,n,c);
  auto star=[&](U q, Prim w, double sw) {
    const double un=w.u*nh.x+w.v*nh.y;
    const double star_denominator=sw-sm;
    if(std::abs(star_denominator)<1.0e-14) return U{};
    const double rho_star=w.rho*(sw-un)/star_denominator;
    const double du=sm-(w.u*nh.x+w.v*nh.y);
    const double u_star=w.u+du*nh.x, v_star=w.v+du*nh.y;
    const double specific_e=q.rhoE/std::max(w.rho,kFloor);
    const double e_star=specific_e+du*(sm+w.p/(std::max(w.rho,kFloor)*(sw-(w.u*nh.x+w.v*nh.y))));
    return U{rho_star,rho_star*u_star,rho_star*v_star,rho_star*e_star};
  };
  const U ul_star=star(L,l,sl), ur_star=star(R,r,sr);
  auto admissible=[&](U q) {
    if(!std::isfinite(q.rho)||!std::isfinite(q.rhou)||!std::isfinite(q.rhov)||!std::isfinite(q.rhoE)||q.rho<=1.0e-10) return false;
    const double kinetic=0.5*(q.rhou*q.rhou+q.rhov*q.rhov)/q.rho;
    const double p=(c.gamma-1.0)*(q.rhoE-kinetic);
    return std::isfinite(p)&&p>1.0e-10*std::max(c.pInf,1.0);
  };
  if(!admissible(ul_star)||!admissible(ur_star)) return rusanov(L,R,n,c);
  const U fl=physical_flux(L,n,c), fr=physical_flux(R,n,c);
  if(0.0<=sl) return fl;
  if(0.0<=sm) return fl+(ul_star-L)*(sl*A);
  if(0.0<=sr) return fr+(ur_star-R)*(sr*A);
  return fr;
}

static U inviscid_flux(U L, U R, V2 n, const Case& c) {
  if(c.inviscidFlux!="hllc") return rusanov(L,R,n,c);
  const U hf=hllc(L,R,n,c);
  const double beta=std::clamp(c.hllcFluxBlend,0.0,1.0);
  if(beta>=1.0-1.0e-14) return hf;
  const U rf=rusanov(L,R,n,c);
  return rf*(1.0-beta)+hf*beta;
}

using Matrix4 = std::array<std::array<double,4>,4>;

// Frozen-state conservative Jacobian of the physical Euler flux F(U) dot n.
// The residual uses the area-weighted normal directly, so this matrix does too.
static Matrix4 euler_flux_jacobian(U q, V2 n, const Case& c) {
  const Prim w=primitive(q,c);
  const double rho=std::max(w.rho,kFloor), u=w.u, v=w.v, un=u*n.x+v*n.y;
  const double gm1=c.gamma-1.0, kinetic=0.5*(u*u+v*v);
  const double dp[4]={gm1*kinetic,-gm1*u,-gm1*v,gm1};
  const double H=q.rhoE+w.p;
  Matrix4 a{};
  a[0]={0.0,n.x,n.y,0.0};
  a[1]={-u*un+n.x*dp[0],un+u*n.x+n.x*dp[1],u*n.y+n.x*dp[2],n.x*dp[3]};
  a[2]={-v*un+n.y*dp[0],v*n.x+n.y*dp[1],un+v*n.y+n.y*dp[2],n.y*dp[3]};
  a[3]={dp[0]*un-H*un/rho,dp[1]*un+H*n.x/rho,dp[2]*un+H*n.y/rho,(1.0+dp[3])*un};
  return a;
}

// Exact Jacobian of a stationary-wall pressure traction (0, p n_x, p n_y, 0).
static Matrix4 wall_pressure_flux_jacobian(U q, V2 n, const Case& c) {
  const Prim w=primitive(q,c);
  const double gm1=c.gamma-1.0;
  const double dp[4]={0.5*gm1*(w.u*w.u+w.v*w.v),-gm1*w.u,-gm1*w.v,gm1};
  Matrix4 a{};
  for(int k=0;k<4;++k) { a[1][k]=n.x*dp[k]; a[2][k]=n.y*dp[k]; }
  return a;
}

// Frozen face derivative used by the transient block-Jacobi preconditioner.
// Unlike the Euler/Rusanov analytic approximation, this differentiates the
// selected numerical flux itself (including HLLC's wave-speed branches and
// its per-face Rusanov fallback).  Reconstruction gradients and limiter
// branches remain frozen, as they do for the existing local block model; the
// resulting matrix is therefore a cheap, bounded approximation rather than a
// claim of an assembled global Newton matrix.
static Matrix4 numerical_inviscid_face_jacobian(U left, U right, V2 n,
                                                 const Case& c) {
  Matrix4 jac{};
  const U ref=freestream(c);
  const double scale[4]={
    std::max(std::abs(left.rho),std::abs(ref.rho)),
    std::max(std::abs(left.rhou),std::abs(ref.rhou)),
    std::max(std::abs(left.rhov),std::abs(ref.rhov)),
    std::max(std::abs(left.rhoE),std::abs(ref.rhoE))};
  for(int k=0;k<4;++k) {
    const double h=1.0e-6*std::max(scale[k],1.0e-8);
    U plus=left, minus=left;
    double* pp[4]={&plus.rho,&plus.rhou,&plus.rhov,&plus.rhoE};
    double* pm[4]={&minus.rho,&minus.rhou,&minus.rhov,&minus.rhoE};
    *pp[k]+=h; *pm[k]-=h;
    auto admissible=[&](const U& q) {
      if(!std::isfinite(q.rho)||!std::isfinite(q.rhou)||!std::isfinite(q.rhov)||
         !std::isfinite(q.rhoE)||q.rho<=1.0e-10) return false;
      const double kinetic=0.5*(q.rhou*q.rhou+q.rhov*q.rhov)/q.rho;
      const double pressure=(c.gamma-1.0)*(q.rhoE-kinetic);
      return std::isfinite(pressure)&&pressure>1.0e-10*std::max(c.pInf,1.0);
    };
    const bool ap=admissible(plus), am=admissible(minus);
    if(ap&&am) {
      const U fp=inviscid_flux(plus,right,n,c), fm=inviscid_flux(minus,right,n,c);
      const double inv=0.5/h;
      const double d[4]={(fp.rho-fm.rho)*inv,(fp.rhou-fm.rhou)*inv,
                         (fp.rhov-fm.rhov)*inv,(fp.rhoE-fm.rhoE)*inv};
      for(int row=0;row<4;++row) jac[row][k]=d[row];
    } else if(ap) {
      const U f0=inviscid_flux(left,right,n,c), fp=inviscid_flux(plus,right,n,c);
      const double inv=1.0/h;
      jac[0][k]=(fp.rho-f0.rho)*inv; jac[1][k]=(fp.rhou-f0.rhou)*inv;
      jac[2][k]=(fp.rhov-f0.rhov)*inv; jac[3][k]=(fp.rhoE-f0.rhoE)*inv;
    } else if(am) {
      const U f0=inviscid_flux(left,right,n,c), fm=inviscid_flux(minus,right,n,c);
      const double inv=1.0/h;
      jac[0][k]=(f0.rho-fm.rho)*inv; jac[1][k]=(f0.rhou-fm.rhou)*inv;
      jac[2][k]=(f0.rhov-fm.rhov)*inv; jac[3][k]=(f0.rhoE-fm.rhoE)*inv;
    }
  }
  return jac;
}

static bool solve_4x4(Matrix4 a, std::array<double,4> b, std::array<double,4>& x) {
  double norm_a=0.0;
  for(const auto& row:a) for(double value:row) {
    if(!std::isfinite(value)) return false;
    norm_a=std::max(norm_a,std::abs(value));
  }
  const double pivot_floor=1.0e-12*std::max(norm_a,1.0);
  for(int k=0;k<4;++k) {
    int pivot=k;
    for(int r=k+1;r<4;++r) if(std::abs(a[r][k])>std::abs(a[pivot][k])) pivot=r;
    if(!std::isfinite(a[pivot][k]) || std::abs(a[pivot][k])<=pivot_floor) return false;
    if(pivot!=k) { std::swap(a[pivot],a[k]); std::swap(b[pivot],b[k]); }
    for(int r=k+1;r<4;++r) {
      const double factor=a[r][k]/a[k][k];
      if(!std::isfinite(factor)) return false;
      for(int col=k+1;col<4;++col) a[r][col]-=factor*a[k][col];
      b[r]-=factor*b[k];
    }
  }
  for(int r=3;r>=0;--r) {
    double value=b[r];
    for(int col=r+1;col<4;++col) value-=a[r][col]*x[col];
    x[r]=value/a[r][r];
    if(!std::isfinite(x[r])) return false;
  }
  return true;
}

static std::string boundary_type(const Face& f, const Case& c) {
  if (c.boundary_types) {
    const auto it=c.boundary_types->find(f.tag);
    if (it!=c.boundary_types->end()) return it->second;
  }
  std::string t=f.tag; std::transform(t.begin(),t.end(),t.begin(),[](unsigned char ch){return static_cast<char>(std::tolower(ch));});
  if (t.find("far")!=std::string::npos || t.find("bc-2")!=std::string::npos) return "farfield";
  if (t.find("wall")!=std::string::npos || t.find("bc-4")!=std::string::npos) return c.mode=="laminar"?"no_slip_adiabatic_wall":"slip_wall";
  return "farfield";
}
static U ghost_state(Prim w, const Face& f, const Case& c) {
  std::string bc=boundary_type(f,c); if (bc=="farfield") return freestream(c);
  V2 n=f.normal*(1.0/std::max(f.area,kFloor)); double vn=w.u*n.x+w.v*n.y;
  if (bc=="no_slip_adiabatic_wall") { w.u=0; w.v=0; }
  else { w.u-=2.0*vn*n.x; w.v-=2.0*vn*n.y; }
  return conservative(w,c);
}

static void annotate_wall_geometry(Mesh& m, const Case& c) {
  struct WallFace { V2 center, normal, tangent; double half_length=0.0; };
  std::vector<WallFace> walls;
  for (const Face& f : m.faces) {
    if (f.b>=0 || boundary_type(f,c)!="no_slip_adiabatic_wall") continue;
    const V2 nh=f.normal*(1.0/std::max(f.area,kFloor));
    const V2 edge=m.points[f.n1]-m.points[f.n0];
    walls.push_back({f.center,nh,edge*(1.0/std::max(f.area,kFloor)),0.5*f.area});
  }
  if (walls.empty()) return;
  for (Cell& cell : m.cells) {
    double best=std::numeric_limits<double>::infinity(); V2 best_normal{};
    for (const WallFace& wall : walls) {
      const V2 d=cell.center-wall.center;
      const double normal_distance=std::abs(dot(d,wall.normal));
      const double tangent_excess=std::max(std::abs(dot(d,wall.tangent))-wall.half_length,0.0);
      const double distance=std::hypot(normal_distance,tangent_excess);
      if (distance<best) { best=distance; best_normal=wall.normal; }
    }
    cell.wall_distance=best;
    cell.wall_normal=best_normal;
  }
}

struct Partition {
  std::vector<int> owner, owned, ghosts;
  std::map<int,std::vector<int>> send, recv;
};
static Partition make_partition(const Mesh& m, int rank, int nr, const std::vector<int>* known_owner=nullptr) {
  Partition p; p.owner.resize(m.cells.size(),0);
  if (known_owner) p.owner=*known_owner;
  else if (nr==1) std::fill(p.owner.begin(),p.owner.end(),0);
  else if (rank==0) {
    std::vector<idx_t> x(m.cells.size()+1), adj; x[0]=0;
    for (std::size_t i=0;i<m.cells.size();++i) { for(int j:m.cells[i].nbr) adj.push_back(static_cast<idx_t>(j)); x[i+1]=static_cast<idx_t>(adj.size()); }
    idx_t nv=static_cast<idx_t>(m.cells.size()), ncon=1, nparts=static_cast<idx_t>(nr), edgecut=0;
    std::vector<idx_t> part(m.cells.size()); int options[METIS_NOPTIONS]; METIS_SetDefaultOptions(options); options[METIS_OPTION_SEED]=17;
    int rc=METIS_PartGraphKway(&nv,&ncon,x.data(),adj.empty()?nullptr:adj.data(),nullptr,nullptr,nullptr,&nparts,nullptr,nullptr,options,&edgecut,part.data());
    if (rc!=METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");
    for(std::size_t i=0;i<part.size();++i) p.owner[i]=static_cast<int>(part[i]);
  }
  // The production solver invokes this during rank-zero preprocessing only.
  // Callers that need a distributed view pass the resulting owner vector back
  // explicitly; no hidden collective is performed here.
  for(int i=0;i<static_cast<int>(m.cells.size());++i) if(p.owner[i]==rank) p.owned.push_back(i);
  for(int i:p.owned) for(int j:m.cells[i].nbr) if(p.owner[j]!=rank) p.ghosts.push_back(j);
  std::sort(p.ghosts.begin(),p.ghosts.end()); p.ghosts.erase(std::unique(p.ghosts.begin(),p.ghosts.end()),p.ghosts.end());
  for(int i:p.owned) for(int j:m.cells[i].nbr) if(p.owner[j]!=rank) p.recv[p.owner[j]].push_back(j);
  for(int i:p.owned) for(int j:m.cells[i].nbr) if(p.owner[j]!=rank) p.send[p.owner[j]].push_back(i);
  for(auto& kv:p.recv){auto&v=kv.second;std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());}
  for(auto& kv:p.send){auto&v=kv.second;std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());}
  return p;
}

// The solver stage consumes rank-local partition files written by rank zero
// during serial preprocessing.  Local cell indices are deliberately compact;
// global ids are retained only for final output assembly and diagnostics.
struct LocalMeshData {
  Mesh mesh;
  Partition partition;
  std::vector<int> global_ids;
  int global_cells = 0;
  int global_faces = 0;
};

template <typename T>
static void write_pod(std::ostream& out, const T& value) {
  static_assert(std::is_trivially_copyable<T>::value, "partition records must be POD");
  out.write(reinterpret_cast<const char*>(&value), sizeof(T));
  if (!out) throw std::runtime_error("failed writing partition cache");
}

template <typename T>
static T read_pod(std::istream& in) {
  static_assert(std::is_trivially_copyable<T>::value, "partition records must be POD");
  T value{};
  in.read(reinterpret_cast<char*>(&value), sizeof(T));
  if (!in) throw std::runtime_error("corrupt partition cache");
  return value;
}

static void write_int_vector(std::ostream& out, const std::vector<int>& values) {
  const std::uint64_t n = values.size(); write_pod(out, n);
  for (int value : values) write_pod(out, value);
}

static std::vector<int> read_int_vector(std::istream& in) {
  const std::uint64_t n = read_pod<std::uint64_t>(in);
  if (n > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error("partition vector too large");
  std::vector<int> values(static_cast<std::size_t>(n));
  for (int& value : values) value = read_pod<int>(in);
  return values;
}

static LocalMeshData build_local_partition(const Mesh& global, const std::vector<int>& owner, int rank, int nr) {
  if (owner.size() != global.cells.size()) throw std::runtime_error("partition owner size does not match mesh");
  LocalMeshData local;
  local.global_cells = static_cast<int>(global.cells.size());
  local.global_faces = static_cast<int>(global.faces.size());

  std::vector<char> selected(global.cells.size(), 0);
  for (int gid = 0; gid < static_cast<int>(global.cells.size()); ++gid) {
    if (owner[gid] == rank) selected[gid] = 1;
  }
  // Keep one state layer beyond every owned cell.  Owned cells are first so
  // that the existing update/reconstruction kernels can use compact local ids.
  for (int gid = 0; gid < static_cast<int>(global.cells.size()); ++gid) {
    if (owner[gid] != rank) continue;
    for (int nbr : global.cells[gid].nbr) if (nbr >= 0) selected[nbr] = 1;
  }
  for (int gid = 0; gid < static_cast<int>(global.cells.size()); ++gid) {
    if (owner[gid] == rank) local.global_ids.push_back(gid);
  }
  for (int gid = 0; gid < static_cast<int>(global.cells.size()); ++gid) {
    if (owner[gid] != rank && selected[gid]) local.global_ids.push_back(gid);
  }
  std::unordered_map<int, int> gid_to_lid;
  gid_to_lid.reserve(local.global_ids.size() * 2 + 1);
  for (int lid = 0; lid < static_cast<int>(local.global_ids.size()); ++lid) gid_to_lid.emplace(local.global_ids[lid], lid);

  std::unordered_map<int, int> global_node_to_local;
  for (int gid : local.global_ids) {
    for (int node : global.cells[gid].nodes) {
      if (global_node_to_local.find(node) == global_node_to_local.end()) {
        const int lid = static_cast<int>(local.mesh.points.size());
        global_node_to_local.emplace(node, lid);
        local.mesh.points.push_back(global.points[node]);
      }
    }
  }
  local.mesh.cells.resize(local.global_ids.size());
  local.partition.owner.resize(local.global_ids.size());
  for (std::size_t lid = 0; lid < local.global_ids.size(); ++lid) {
    const int gid = local.global_ids[lid];
    const Cell& source = global.cells[gid];
    Cell& target = local.mesh.cells[lid];
    target.center = source.center;
    target.area = source.area;
    target.wall_distance = source.wall_distance;
    target.wall_normal = source.wall_normal;
    target.nodes.reserve(source.nodes.size());
    for (int node : source.nodes) target.nodes.push_back(global_node_to_local.at(node));
    for (int nbr : source.nbr) {
      auto it = gid_to_lid.find(nbr);
      if (it != gid_to_lid.end()) target.nbr.push_back(it->second);
    }
    local.partition.owner[lid] = owner[gid];
  }

  // Copy only faces incident to an owned cell.  This includes all boundary
  // faces and the single copy of every owned/ghost interface face.
  std::unordered_map<int, int> face_to_local;
  for (std::size_t lid = 0; lid < local.global_ids.size(); ++lid) {
    const int gid = local.global_ids[lid];
    if (owner[gid] != rank) continue;
    for (int global_fid : global.cells[gid].faces) {
      if (face_to_local.find(global_fid) != face_to_local.end()) continue;
      const Face& source = global.faces[global_fid];
      Face target = source;
      target.a = gid_to_lid.at(source.a);
      target.b = source.b >= 0 ? gid_to_lid.at(source.b) : -1;
      target.n0 = global_node_to_local.at(source.n0);
      target.n1 = global_node_to_local.at(source.n1);
      const int local_fid = static_cast<int>(local.mesh.faces.size());
      face_to_local.emplace(global_fid, local_fid);
      local.mesh.faces.push_back(std::move(target));
    }
  }
  for (int fid = 0; fid < static_cast<int>(local.mesh.faces.size()); ++fid) {
    const Face& f = local.mesh.faces[fid];
    if (f.a >= 0) local.mesh.cells[f.a].faces.push_back(fid);
    if (f.b >= 0) local.mesh.cells[f.b].faces.push_back(fid);
  }

  for (int lid = 0; lid < static_cast<int>(local.global_ids.size()); ++lid) {
    if (local.partition.owner[lid] == rank) local.partition.owned.push_back(lid);
    else local.partition.ghosts.push_back(lid);
  }
  for (int lid : local.partition.owned) {
    for (int nbr : local.mesh.cells[lid].nbr) {
      const int peer = local.partition.owner[nbr];
      if (peer == rank) continue;
      local.partition.recv[peer].push_back(nbr);
      local.partition.send[peer].push_back(lid);
    }
  }
  for (auto& kv : local.partition.recv) {
    auto& ids = kv.second; std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  }
  for (auto& kv : local.partition.send) {
    auto& ids = kv.second; std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  }
  (void)nr;
  return local;
}

static void write_local_partition(const fs::path& path, const LocalMeshData& local) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot create partition cache " + path.string());
  const std::uint32_t magic = 0x41555231u; // AUR1
  write_pod(out, magic); write_pod(out, local.global_cells); write_pod(out, local.global_faces);
  const std::uint64_t np = local.mesh.points.size(); write_pod(out, np);
  for (const V2& p : local.mesh.points) { write_pod(out, p.x); write_pod(out, p.y); }
  const std::uint64_t nc = local.mesh.cells.size(); write_pod(out, nc);
  for (std::size_t lid = 0; lid < nc; ++lid) {
    write_pod(out, local.global_ids[lid]); write_pod(out, local.partition.owner[lid]);
    write_pod(out, local.mesh.cells[lid].center.x); write_pod(out, local.mesh.cells[lid].center.y); write_pod(out, local.mesh.cells[lid].area);
    write_pod(out, local.mesh.cells[lid].wall_distance); write_pod(out, local.mesh.cells[lid].wall_normal.x); write_pod(out, local.mesh.cells[lid].wall_normal.y);
    write_int_vector(out, local.mesh.cells[lid].nodes);
    write_int_vector(out, local.mesh.cells[lid].faces);
    write_int_vector(out, local.mesh.cells[lid].nbr);
  }
  const std::uint64_t nf = local.mesh.faces.size(); write_pod(out, nf);
  for (const Face& f : local.mesh.faces) {
    write_pod(out, f.a); write_pod(out, f.b); write_pod(out, f.n0); write_pod(out, f.n1);
    write_pod(out, f.center.x); write_pod(out, f.center.y); write_pod(out, f.normal.x); write_pod(out, f.normal.y); write_pod(out, f.area);
    const std::uint64_t n = f.tag.size(); write_pod(out, n); out.write(f.tag.data(), static_cast<std::streamsize>(n));
  }
}

static LocalMeshData read_local_partition(const fs::path& path, int rank) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open partition cache " + path.string());
  LocalMeshData local; const std::uint32_t magic = read_pod<std::uint32_t>(in);
  if (magic != 0x41555231u) throw std::runtime_error("invalid partition cache magic");
  local.global_cells = read_pod<int>(in); local.global_faces = read_pod<int>(in);
  const std::uint64_t np = read_pod<std::uint64_t>(in);
  local.mesh.points.resize(static_cast<std::size_t>(np));
  for (V2& p : local.mesh.points) { p.x = read_pod<double>(in); p.y = read_pod<double>(in); }
  const std::uint64_t nc = read_pod<std::uint64_t>(in);
  local.global_ids.resize(static_cast<std::size_t>(nc));
  local.partition.owner.resize(static_cast<std::size_t>(nc));
  local.mesh.cells.resize(static_cast<std::size_t>(nc));
  for (std::size_t lid = 0; lid < static_cast<std::size_t>(nc); ++lid) {
    local.global_ids[lid] = read_pod<int>(in); local.partition.owner[lid] = read_pod<int>(in);
    Cell& cell = local.mesh.cells[lid];
    cell.center.x = read_pod<double>(in); cell.center.y = read_pod<double>(in); cell.area = read_pod<double>(in);
    cell.wall_distance = read_pod<double>(in); cell.wall_normal.x = read_pod<double>(in); cell.wall_normal.y = read_pod<double>(in);
    cell.nodes = read_int_vector(in); cell.faces = read_int_vector(in); cell.nbr = read_int_vector(in);
  }
  const std::uint64_t nf = read_pod<std::uint64_t>(in);
  local.mesh.faces.resize(static_cast<std::size_t>(nf));
  for (Face& f : local.mesh.faces) {
    f.a = read_pod<int>(in); f.b = read_pod<int>(in); f.n0 = read_pod<int>(in); f.n1 = read_pod<int>(in);
    f.center.x = read_pod<double>(in); f.center.y = read_pod<double>(in); f.normal.x = read_pod<double>(in); f.normal.y = read_pod<double>(in); f.area = read_pod<double>(in);
    const std::uint64_t n = read_pod<std::uint64_t>(in);
    f.tag.resize(static_cast<std::size_t>(n)); in.read(f.tag.data(), static_cast<std::streamsize>(n));
    if (!in) throw std::runtime_error("corrupt partition cache string");
  }
  for (int lid = 0; lid < static_cast<int>(local.global_ids.size()); ++lid) {
    if (local.partition.owner[lid] == rank) local.partition.owned.push_back(lid);
    else local.partition.ghosts.push_back(lid);
  }
  for (int lid : local.partition.owned) {
    for (int nbr : local.mesh.cells[lid].nbr) {
      const int peer = local.partition.owner[nbr];
      if (peer == rank) continue;
      local.partition.recv[peer].push_back(nbr);
      local.partition.send[peer].push_back(lid);
    }
  }
  for (auto& kv : local.partition.recv) {
    auto& ids = kv.second; std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  }
  for (auto& kv : local.partition.send) {
    auto& ids = kv.second; std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  }
  if (local.mesh.cells.size() != local.global_ids.size()) throw std::runtime_error("partition cache cell count mismatch");
  return local;
}
static void halo_exchange(std::vector<U>& q, const Partition& p) {
  std::set<int> ranks; for(auto&kv:p.send)ranks.insert(kv.first); for(auto&kv:p.recv)ranks.insert(kv.first);
  std::vector<std::vector<double>> sb,rb; std::vector<std::vector<int>> rids; std::vector<MPI_Request> req;
  sb.reserve(p.send.size()); rb.reserve(p.recv.size()); rids.reserve(p.recv.size()); req.reserve(2*(p.send.size()+p.recv.size()));
  for(int r:ranks) if(p.recv.count(r)) { rids.push_back(p.recv.at(r)); rb.emplace_back(rids.back().size()*4); req.emplace_back(MPI_REQUEST_NULL); MPI_Irecv(rb.back().data(),static_cast<int>(rb.back().size()),MPI_DOUBLE,r,7401,MPI_COMM_WORLD,&req.back()); }
  std::vector<int> send_ranks; for(int r:ranks) if(p.send.count(r)) { send_ranks.push_back(r); sb.emplace_back(p.send.at(r).size()*4); auto&buf=sb.back();std::size_t k=0;for(int id:p.send.at(r)){buf[k++]=q[id].rho;buf[k++]=q[id].rhou;buf[k++]=q[id].rhov;buf[k++]=q[id].rhoE;} req.emplace_back(MPI_REQUEST_NULL); MPI_Isend(buf.data(),static_cast<int>(buf.size()),MPI_DOUBLE,r,7401,MPI_COMM_WORLD,&req.back()); }
  if(!req.empty()) MPI_Waitall(static_cast<int>(req.size()),req.data(),MPI_STATUSES_IGNORE);
  std::size_t b=0; for(std::size_t rr=0;rr<rids.size();++rr){const auto&ids=rids[rr];for(std::size_t i=0;i<ids.size();++i)q[ids[i]]={rb[rr][4*i],rb[rr][4*i+1],rb[rr][4*i+2],rb[rr][4*i+3]};++b;}
}

static void halo_exchange_grad(std::vector<Grad>& g, const Partition& p) {
  std::set<int> ranks; for (const auto& kv : p.send) ranks.insert(kv.first); for (const auto& kv : p.recv) ranks.insert(kv.first);
  std::vector<std::vector<double>> sb, rb; std::vector<std::vector<int>> rids; std::vector<MPI_Request> req;
  sb.reserve(p.send.size()); rb.reserve(p.recv.size()); rids.reserve(p.recv.size()); req.reserve(2*(p.send.size()+p.recv.size()));
  constexpr int ncomp = 9;
  for (int peer : ranks) if (p.recv.count(peer)) {
    rids.push_back(p.recv.at(peer)); rb.emplace_back(rids.back().size() * ncomp); req.emplace_back(MPI_REQUEST_NULL);
    MPI_Irecv(rb.back().data(), static_cast<int>(rb.back().size()), MPI_DOUBLE, peer, 7402, MPI_COMM_WORLD, &req.back());
  }
  for (int peer : ranks) if (p.send.count(peer)) {
    sb.emplace_back(p.send.at(peer).size() * ncomp); auto& buf = sb.back(); std::size_t k = 0;
    for (int id : p.send.at(peer)) {
      const Grad& v = g[id]; double values[ncomp] = {v.drx,v.dry,v.dux,v.duy,v.dvx,v.dvy,v.dTx,v.dTy,v.theta};
      for (double value : values) buf[k++] = value;
    }
    req.emplace_back(MPI_REQUEST_NULL);
    MPI_Isend(buf.data(), static_cast<int>(buf.size()), MPI_DOUBLE, peer, 7402, MPI_COMM_WORLD, &req.back());
  }
  if (!req.empty()) MPI_Waitall(static_cast<int>(req.size()), req.data(), MPI_STATUSES_IGNORE);
  for (std::size_t rr = 0; rr < rids.size(); ++rr) {
    const auto& ids = rids[rr];
    for (std::size_t i = 0; i < ids.size(); ++i) {
      Grad& v = g[ids[i]]; const double* values = rb[rr].data() + i * ncomp;
      v = {values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],values[8]};
    }
  }
}

static std::vector<Grad> gradients(const Mesh& m, const Partition& p,
                                   const std::vector<U>& q, const Case& c,
                                   const std::vector<Prim>* primitive_cache=nullptr) {
  std::vector<Grad> g(m.cells.size());
  #pragma omp parallel for schedule(static)
  for(std::ptrdiff_t owned_idx=0; owned_idx<static_cast<std::ptrdiff_t>(p.owned.size()); ++owned_idx) {
    const int i=p.owned[static_cast<std::size_t>(owned_idx)];
    const Cell& ci=m.cells[i]; double a=0,b=0,d=0; std::array<double,8> sx{} , sy{};
    const Prim wi=primitive_cache?(*primitive_cache)[static_cast<std::size_t>(i)]:primitive(q[i],c);
    double mn[5]={wi.rho,wi.u,wi.v,wi.T,wi.p}, mx[5]={wi.rho,wi.u,wi.v,wi.T,wi.p};
    auto accumulate_sample = [&](V2 x, Prim wj) {
      V2 dx=x-ci.center;
      // The normal equations are deliberately unweighted.  Adding the
      // boundary image at its physical face location makes the least-squares
      // gradient satisfy the no-slip wall constraint even on thin/sliver
      // boundary cells; omitting it leaves a freestream-speed cell centre
      // paired with an enormous one-sided wall traction.
      a+=dx.x*dx.x; b+=dx.x*dx.y; d+=dx.y*dx.y;
      double z[5]={wj.rho,wj.u,wj.v,wj.T,wj.p};
      for(int k=0;k<5;++k){mn[k]=std::min(mn[k],z[k]);mx[k]=std::max(mx[k],z[k]);}
      double dv[5]={wj.rho-wi.rho,wj.u-wi.u,wj.v-wi.v,wj.T-wi.T,wj.p-wi.p};
      for(int k=0;k<5;++k){sx[k]+=dx.x*dv[k];sy[k]+=dx.y*dv[k];}
    };
    for(int j:ci.nbr) accumulate_sample(m.cells[j].center,
      primitive_cache?(*primitive_cache)[static_cast<std::size_t>(j)]:primitive(q[j],c));
    // Treat a no-slip boundary as a physical image point in the gradient
    // stencil.  Pressure, density, and temperature remain continuous while
    // velocity is prescribed to zero at the wall.  Slip/farfield faces are
    // not included because their normal state is already handled by the
    // inviscid boundary flux and does not provide a diffusive interior value.
    for(int fi:ci.faces){
      const Face& face=m.faces[fi];
      if(face.b>=0 || boundary_type(face,c)!="no_slip_adiabatic_wall") continue;
      accumulate_sample(face.center,primitive(ghost_state(wi,face,c),c));
    }
    double det=a*d-b*b; if(std::abs(det)<1e-20)det=1e-20; double theta=1.0;
    double* gx[5]={&g[i].drx,&g[i].dux,&g[i].dvx,&g[i].dTx,nullptr}; double* gy[5]={&g[i].dry,&g[i].duy,&g[i].dvy,&g[i].dTy,nullptr};
    for(int k=0;k<4;++k){*gx[k]=(d*sx[k]-b*sy[k])/det;*gy[k]=(a*sy[k]-b*sx[k])/det;}
    const double base[4]={wi.rho,wi.u,wi.v,wi.T};
    for(int k=0;k<4;++k){double val[2]={*gx[k],*gy[k]};for(int f:ci.faces){const Face& face=m.faces[f];V2 dx=face.center-ci.center;double delta=val[0]*dx.x+val[1]*dx.y;if(delta>1e-14)theta=std::min(theta,(mx[k]-base[k])/delta);else if(delta<-1e-14)theta=std::min(theta,(mn[k]-base[k])/delta);} }
    g[i].theta=std::clamp(theta,0.0,1.0);
  }
  return g;
}
static Prim reconstruct(const Mesh& m, int i, V2 x, const std::vector<U>& q,
                        const std::vector<Grad>& g, const Case& c,
                        const std::vector<Prim>* primitive_cache=nullptr) {
  Prim w=primitive_cache?(*primitive_cache)[static_cast<std::size_t>(i)]:primitive(q[i],c);
  V2 d=x-m.cells[i].center; double t=g[i].theta;
  // The low-Mach transient uses a bounded piecewise-linear slope.  Keeping a
  // fixed fraction of the limited gradient preserves the second-order
  // reconstruction form while avoiding limiter switching noise in the stiff
  // acoustic inner solve.
  if (c.transient) t*=std::clamp(c.transientSlope,0.0,1.0);
  w.rho += t*(g[i].drx*d.x+g[i].dry*d.y); w.u += t*(g[i].dux*d.x+g[i].duy*d.y); w.v += t*(g[i].dvx*d.x+g[i].dvy*d.y); w.T += t*(g[i].dTx*d.x+g[i].dTy*d.y);
  w.rho=std::max(w.rho,1e-8); w.T=std::max(w.T,1e-8); w.p=std::max(w.rho*c.R*w.T,1e-8); return w;
}

static Grad wall_gradient(const Mesh& m, const Face& face, int cell, Prim w, const Grad& base, const Case& c) {
  Grad g=base;
  if (boundary_type(face,c)=="no_slip_adiabatic_wall") {
    // Use the same one-sided wall-normal reconstruction as the viscous
    // residual.  The wall value is zero velocity and the distance is the
    // orthogonal centre-to-face distance.  A stationary no-slip wall has zero
    // tangential velocity, so its tangential derivative is not retained from
    // the unconstrained least-squares stencil.
    const V2 n=face.normal*(1.0/std::max(face.area,kFloor));
    const V2 d=face.center-m.cells[cell].center;
    const double dn=std::max(std::abs(dot(d,n)),1.0e-10);
    // A stationary no-slip wall has zero tangential velocity everywhere along
    // the face, so its tangential derivative is zero as well.  Use the
    // one-sided wall-normal derivative for each velocity component; retaining
    // an unconstrained least-squares tangential derivative would manufacture
    // a spurious wall shear on the highly stretched trailing-edge cells.
    auto impose_zero = [&](double phi, double& gx, double& gy) {
      const double normal_derivative=-phi/dn;
      gx=normal_derivative*n.x; gy=normal_derivative*n.y;
    };
    impose_zero(w.u,g.dux,g.duy);
    impose_zero(w.v,g.dvx,g.dvy);
  }
  return g;
}

struct ResidualResult { double l2=0, linf=0; std::array<double,4> mean{}; };
static ResidualResult residual_stats(const Partition& p, const std::vector<U>& res) {
  double l2=0,linf=0; long long n=0; double local0=0,local1=0,local2=0,local3=0;
  #pragma omp parallel for reduction(+:l2,n,local0,local1,local2,local3) reduction(max:linf) schedule(static)
  for(std::ptrdiff_t owned_idx=0; owned_idx<static_cast<std::ptrdiff_t>(p.owned.size()); ++owned_idx){const int i=p.owned[static_cast<std::size_t>(owned_idx)];local0+=std::abs(res[i].rho);local1+=std::abs(res[i].rhou);local2+=std::abs(res[i].rhov);local3+=std::abs(res[i].rhoE);double s=std::sqrt(res[i].rho*res[i].rho+res[i].rhou*res[i].rhou+res[i].rhov*res[i].rhov+res[i].rhoE*res[i].rhoE);l2+=s*s;linf=std::max(linf,s);++n;}
  std::array<double,4> local{local0,local1,local2,local3};
  // Pack the additive diagnostics into one collective; the maximum norm is
  // the only non-additive quantity and needs one separate reduction.
  double sums[6]={l2,static_cast<double>(n),local[0],local[1],local[2],local[3]}, global_sums[6]={};
  double glinf=0;
  MPI_Allreduce(sums,global_sums,6,MPI_DOUBLE,MPI_SUM,MPI_COMM_WORLD);
  MPI_Allreduce(&linf,&glinf,1,MPI_DOUBLE,MPI_MAX,MPI_COMM_WORLD);
  const double gn=std::max(global_sums[1],1.0);
  ResidualResult out;out.l2=std::sqrt(global_sums[0]/gn);out.linf=glinf;for(int k=0;k<4;++k)out.mean[k]=global_sums[2+k]/gn;return out;
}

// Physical-time contribution used by the transient nonlinear solve.  The
// optional blend is a nonlinear continuation between the one-step BE map and
// the requested BDF2 map; the accepted state is always reassembled with
// blend=1 before the physical histories advance.  This keeps the temporal
// discretization unchanged while giving a difficult wake interval a nearby,
// contractive starting operator.
static U transient_time_term(const U& q, const U& qn, const U& qnm1,
                             double area, double dt, bool backward_euler,
                             double bdf2_blend=1.0) {
  const U be=(q-qn)*(area/std::max(dt,1.0e-12));
  if(backward_euler) return be;
  const U bdf2=(3.0*q-4.0*qn+qnm1)*(area/(2.0*std::max(dt,1.0e-12)));
  const double b=std::clamp(bdf2_blend,0.0,1.0);
  return be*(1.0-b)+bdf2*b;
}

static double transient_time_coefficient(bool backward_euler,
                                         double bdf2_blend=1.0) {
  if(backward_euler) return 1.0;
  return 1.0+0.5*std::clamp(bdf2_blend,0.0,1.0);
}

static ResidualResult assemble(const Mesh& m, const Partition& p, std::vector<U>& q, std::vector<U>& res, std::vector<Grad>& grad, const Case& c, bool collect_stats=true, bool refresh_gradients=true) {
  halo_exchange(q,p);
  // Primitive conversion is shared by the least-squares reconstruction and
  // every face state in this residual evaluation.  Reusing it avoids several
  // square-root/EOS evaluations per neighbor while leaving the assembled
  // residual mathematically unchanged.
  std::vector<Prim> primitive_cache(q.size());
  #pragma omp parallel for schedule(static)
  for(std::ptrdiff_t i=0;i<static_cast<std::ptrdiff_t>(q.size());++i)
    primitive_cache[static_cast<std::size_t>(i)]=primitive(q[static_cast<std::size_t>(i)],c);
  if(refresh_gradients || grad.size()!=q.size()) {
    grad=gradients(m,p,q,c,&primitive_cache); halo_exchange_grad(grad,p);
  }
  std::fill(res.begin(),res.end(),U{0.0,0.0,0.0,0.0});
  const double mu=(c.mode=="laminar"&&c.Re>0)?c.rhoInf*c.UInf*c.refL/c.Re:0.0;
  #pragma omp parallel for schedule(static)
  for(std::ptrdiff_t owned_idx=0; owned_idx<static_cast<std::ptrdiff_t>(p.owned.size()); ++owned_idx) { const int i=p.owned[static_cast<std::size_t>(owned_idx)]; for(int fi:m.cells[i].faces) {
    const Face& f=m.faces[fi]; bool first=f.a==i; int j=first?f.b:f.a; V2 n=first?f.normal:f.normal*(-1.0);
    Prim wi=reconstruct(m,i,f.center,q,grad,c,&primitive_cache); U left=conservative(wi,c), right; Prim wj;
    bool wall_face=false;
    if(j>=0) { wj=reconstruct(m,j,f.center,q,grad,c,&primitive_cache); right=conservative(wj,c); }
    else {
      right=ghost_state(wi,f,c); wj=primitive(right,c);
      const std::string bc=boundary_type(f,c);
      wall_face=(bc=="slip_wall"||bc=="no_slip_adiabatic_wall");
    }
    // A stationary solid has exactly zero mass and energy flux.  Applying a
    // Rusanov flux to a no-slip (zero-velocity) ghost state would otherwise
    // create a spurious half-cell mass flux from the nonzero interior normal
    // velocity.  Use the exact wall pressure flux and retain the viscous
    // traction below for laminar cases.
    U flux=wall_face?U{0.0,wi.p*n.x,wi.p*n.y,0.0}:inviscid_flux(left,right,n,c);
    if(c.mode=="laminar" && mu>0) {
      Grad gi=grad[i], gj;
      if(j>=0) gj=grad[j];
      else if(boundary_type(f,c)=="no_slip_adiabatic_wall") {
        // A boundary face has one physical gradient, not an average of the
        // interior LS value and the wall-corrected value.  Reuse it in the
        // residual and in force/surface reporting so all three diagnostics
        // represent the same traction.
        gi=wall_gradient(m,f,i,primitive(q[i],c),gi,c); gj=gi;
      } else gj=gi;
      double ux=0.5*(gi.dux+gj.dux),uy=0.5*(gi.duy+gj.duy),vx=0.5*(gi.dvx+gj.dvx),vy=0.5*(gi.dvy+gj.dvy),Tx=0.5*(gi.dTx+gj.dTx),Ty=0.5*(gi.dTy+gj.dTy);
      double div=ux+vy, txx=mu*(2*ux-2.0*div/3.0), tyy=mu*(2*vy-2.0*div/3.0), txy=mu*(uy+vx), kappa=mu*c.gamma*c.R/(c.gamma-1.0)/c.Pr;
      V2 nn=n*(1.0/std::max(f.area,kFloor));
      // `visc` is the diffusive flux subtracted from the conservative
      // inviscid flux.  Its conductive contribution therefore uses +k grad T
      // so that the assembled energy flux contains the physical -k grad T.
      V2 conductive{kappa*Tx,kappa*Ty};
      // Use a symmetric face velocity for viscous work.  On an internal face
      // this keeps the energy contribution antisymmetric when the two
      // adjacent cells assemble the same face in opposite orientations;
      // using the current-side velocity would otherwise leave a spurious
      // energy source proportional to the velocity jump.
      const double uf=j>=0?0.5*(wi.u+wj.u):wi.u;
      const double vf=j>=0?0.5*(wi.v+wj.v):wi.v;
      U visc{0,txx*nn.x+txy*nn.y,txy*nn.x+tyy*nn.y,uf*(txx*nn.x+txy*nn.y)+vf*(txy*nn.x+tyy*nn.y)+conductive.x*nn.x+conductive.y*nn.y};
      if(j<0 && boundary_type(f,c)=="no_slip_adiabatic_wall") visc.rhoE=0.0;
      flux-=visc*f.area;
    }
    res[i]+=flux;
  } }
  return collect_stats?residual_stats(p,res):ResidualResult{};
}

static void update_owned(const Mesh& m, const Partition& p, std::vector<U>& q,
                         const std::vector<U>& res, const std::vector<Grad>& grad,
                         const Case& c, double cfl, int physical_step,
                         double temporal_step=-1.0, double temporal_coefficient=-1.0,
                         double pseudo_gain_scale=1.0, int nonlinear_iteration=-1) {
  // A diagonal Jacobi/Richardson relaxation for the nonlinear residual.  The
  // physical-time coefficient is included in the local spectral radius so
  // that the BDF2 inner solve remains stable at the supplied low-Mach step.
  const double mu=(c.mode=="laminar"&&c.Re>0)?c.rhoInf*c.UInf*c.refL/c.Re:0.0;
  // A face finite-difference derivative is much more expensive than the
  // scalar/block update itself.  Freeze the HLLC face-block sum for a short
  // pseudo-time window and refresh it at the beginning of each physical step
  // and periodically thereafter.  The cache is built before entering the
  // OpenMP cell loop so no worker can resize or write it concurrently.
  static std::vector<Matrix4> hllc_face_cache;
  static std::vector<unsigned char> hllc_face_valid;
  static int hllc_cache_step=-1;
  // The finite-difference cache must cover the exact HLLC operator as well
  // as blended HLLC.  The previous `< 1` guard silently sent beta=1 (the
  // production HLLC setting) through the analytic Euler/Rusanov block, even
  // though the residual was using HLLC contact-wave branches.  Keep beta=0
  // on the legacy analytic path; any positive HLLC blend uses the selected
  // numerical face flux derivative.
  const bool hllc_operator=(c.transient && c.transientBlockCoupling>0.0 &&
                            c.inviscidFlux=="hllc" && c.hllcFluxBlend>1.0e-14);
  const int refresh_period=16;
  const bool refresh_hllc=hllc_operator &&
    (hllc_cache_step!=physical_step || nonlinear_iteration<=1 ||
     (nonlinear_iteration>0 && nonlinear_iteration%refresh_period==0));
  if(hllc_operator && (refresh_hllc || hllc_face_cache.size()!=q.size())) {
    hllc_face_cache.assign(q.size(),Matrix4{});
    hllc_face_valid.assign(q.size(),0);
    #pragma omp parallel for schedule(static)
    for(std::ptrdiff_t oi=0;oi<static_cast<std::ptrdiff_t>(p.owned.size());++oi) {
      const int cell=p.owned[static_cast<std::size_t>(oi)];
      Matrix4 sum{};
      for(int fi:m.cells[cell].faces) {
        const Face& face=m.faces[fi];
        const bool first=face.a==cell;
        const V2 fn=first?face.normal:face.normal*(-1.0);
        const int nbr=first?face.b:face.a;
        const bool wall_face=nbr<0 &&
          (boundary_type(face,c)=="slip_wall" || boundary_type(face,c)=="no_slip_adiabatic_wall");
        if(wall_face) continue;
        // Assemble the frozen block at the same reconstructed face states as
        // the nonlinear residual.  Differentiating cell-centre states here
        // can select a different HLLC wave/positivity branch from the one
        // actually used by `assemble`, making the preconditioner actively
        // misleading in the full-slope wake.  The gradients themselves stay
        // frozen for this local block, just as the surrounding block-Jacobi
        // approximation freezes limiter/reconstruction coupling.
        const U left=conservative(reconstruct(m,cell,face.center,q,grad,c),c);
        const U right=nbr>=0
          ? conservative(reconstruct(m,nbr,face.center,q,grad,c),c)
          : freestream(c);
        const Matrix4 face_jac=numerical_inviscid_face_jacobian(left,right,fn,c);
        for(int row=0;row<4;++row) for(int col=0;col<4;++col)
          sum[row][col]+=face_jac[row][col];
      }
      hllc_face_cache[static_cast<std::size_t>(cell)]=sum;
      hllc_face_valid[static_cast<std::size_t>(cell)]=1;
    }
    hllc_cache_step=physical_step;
  }
  #pragma omp parallel for schedule(static)
  for(std::ptrdiff_t owned_idx=0; owned_idx<static_cast<std::ptrdiff_t>(p.owned.size()); ++owned_idx){ const int i=p.owned[static_cast<std::size_t>(owned_idx)];
    Prim w=primitive(q[i],c); double waves=0;
    for(int fi:m.cells[i].faces){
      const Face&f=m.faces[fi];
      const double a=std::sqrt(c.gamma*w.p/w.rho);
      // Match the transient pseudo-time diagonal to the acoustic signal used
      // by the assembled low-Mach Rusanov residual.  The former full-
      // acoustic radius was a very conservative upper bound (about 10x at
      // Mach 0.1), shrinking every Richardson correction and making the
      // nominal 1000-iteration inner budget act as an unintended temporal
      // damping mechanism.  Keep the supplied Rusanov scale literal, just as
      // rusanov_signal does for the residual flux.
      double acoustic_factor=1.0;
      if(c.transient){
        const double mach=std::sqrt(w.u*w.u+w.v*w.v)/std::max(a, kFloor);
        acoustic_factor=std::clamp(std::max(mach,c.transientLowMachCutoff),c.transientLowMachCutoff,1.0);
      }
      // Keep the pseudo-time diagonal tied to the requested Rusanov signal
      // scale.  HLLC changes only the physical interior flux; using a
      // different hidden radius here changes the nonlinear iteration model
      // and can turn otherwise convergent BDF2 steps into expensive fallback
      // ladders.
      waves+=(std::abs(w.u*f.normal.x/f.area+w.v*f.normal.y/f.area)+c.rusanovScale*acoustic_factor*a)*f.area;
    }
    // For an anisotropic unstructured cell the diffusive diagonal scales as
    // mu * face_length / (rho * centre-to-face distance), not area/h^2.
    // The latter hides the very small wall-normal spacing of the supplied
    // boundary-layer mesh and permits an unstable Jacobi correction.
    double viscous_radius=0.0;
    if(mu>0.0) for(int fi:m.cells[i].faces){
      const Face& f=m.faces[fi];
      const V2 nh=f.normal*(1.0/std::max(f.area,kFloor));
      const int j=f.a==i?f.b:f.a;
      const V2 d=j>=0?m.cells[j].center-m.cells[i].center:f.center-m.cells[i].center;
      const double dn=std::max(std::abs(dot(d,nh)),1.0e-10);
      viscous_radius += 4.0*mu*f.area/(std::max(w.rho,1.0e-8)*dn);
    }
    // The physical-time coefficient is part of the BDF2 Jacobian estimate.
    const double preconditioner_dt=temporal_step>0.0?temporal_step:c.dt;
    const double preconditioner_coefficient=temporal_coefficient>0.0?temporal_coefficient:1.5;
    double temporal_radius=c.transient ? preconditioner_coefficient*m.cells[i].area/std::max(preconditioner_dt,1.0e-12) : 0.0;
    double spectral=std::max(waves+viscous_radius+temporal_radius,1.0e-8);
    double dtau=cfl*m.cells[i].area/(spectral*(1.0+cfl));
    const double gain=c.transient?c.transientGain*std::max(pseudo_gain_scale,1.0e-6):1.0;
    U dq;
    if(c.transient && c.transientBlockCoupling>0.0) {
      // Build a frozen local residual Jacobian rather than perturbing the
      // scalar diagonal with off-diagonal entries alone.  For an internal or
      // far-field face the local Rusanov derivative is approximated by
      // 1/2(A + s |n| I), where A is the conservative Euler flux Jacobian and
      // s is exactly the signal used by `assemble`.  Wall faces contribute
      // their pressure-traction Jacobian.  The established scalar spectral
      // radius (including viscous and physical-time terms) remains as the
      // positivity-stable base; `transientBlockCoupling` blends in this local
      // frozen block.  This is a block-Jacobi preconditioner, not an exact
      // Jacobian of the limited reconstructed residual.
      const double beta=std::clamp(c.transientBlockCoupling,0.0,1.0);
      Matrix4 block{};
      // Blend the frozen local block with the established scalar
      // preconditioner.  The scalar viscous/temporal terms are retained in
      // the local block because they are not represented by the inviscid face
      // Jacobians below.
      for(int k=0;k<4;++k) block[k][k]=(1.0-beta)*spectral+beta*(viscous_radius+temporal_radius);
      for(int fi:m.cells[i].faces) {
        const Face& f=m.faces[fi];
        const bool first=f.a==i;
        const V2 n=first?f.normal:f.normal*(-1.0);
        const int j=first?f.b:f.a;
        const bool wall=j<0 && (boundary_type(f,c)=="slip_wall" || boundary_type(f,c)=="no_slip_adiabatic_wall");
        const U neighbor_state=j>=0?q[j]:freestream(c);
        // Keep the local block consistent with the selected face operator.
        // The analytic Euler/Rusanov approximation is adequate for the
        // default scalar-Rusanov residual, but it is a poor model of HLLC's
        // contact-wave branches at low Mach.  Differentiate the actual
        // blended flux with the reconstruction held frozen on the HLLC path;
        // this is still a local preconditioner, not a claim of a global
        // Newton matrix.
        const Matrix4 jac=wall
          ? wall_pressure_flux_jacobian(q[i],n,c)
          : euler_flux_jacobian(q[i],n,c);
        // The finite-difference HLLC derivative already contains the selected
        // upwind/dissipative contribution.  Add the explicit Rusanov radius
        // only on the legacy Rusanov block path, where the analytic Euler
        // derivative is intentionally paired with a scalar LF term.
        const double signal=(wall || c.inviscidFlux=="hllc") ? 0.0 :
          rusanov_signal(q[i],neighbor_state,n,c);
        const double area=norm(n);
        const double factor=wall?1.0:0.5;
        if(!wall && hllc_operator) continue;
        for(int row=0;row<4;++row) for(int col=0;col<4;++col) {
          const double rusanov_diag=(row==col && !wall)?0.5*signal*area:0.0;
          block[row][col]+=beta*(factor*jac[row][col]+rusanov_diag);
        }
      }
      if(c.inviscidFlux=="hllc" && i>=0 &&
         static_cast<std::size_t>(i)<hllc_face_cache.size() &&
         hllc_face_valid[static_cast<std::size_t>(i)]) {
        const Matrix4& face_sum=hllc_face_cache[static_cast<std::size_t>(i)];
        for(int row=0;row<4;++row) for(int col=0;col<4;++col)
          block[row][col]+=beta*0.5*face_sum[row][col];
        // Wall pressure derivatives were skipped from the cached interior
        // sum and must still be included at the current state.
        for(int fi:m.cells[i].faces) {
          const Face& f=m.faces[fi];
          const bool first=f.a==i; const int j=first?f.b:f.a;
          if(j>=0) continue;
          const std::string bc=boundary_type(f,c);
          if(bc!="slip_wall" && bc!="no_slip_adiabatic_wall") continue;
          const V2 n=first?f.normal:f.normal*(-1.0);
          const Matrix4 wall_jac=wall_pressure_flux_jacobian(q[i],n,c);
          for(int row=0;row<4;++row) for(int col=0;col<4;++col)
            block[row][col]+=beta*wall_jac[row][col];
        }
      }
      const U qref=freestream(c);
      const std::array<double,4> scale{{
        std::max(std::abs(qref.rho),1.0e-8),
        std::max({std::abs(qref.rhou),std::abs(qref.rho*c.UInf),1.0e-8}),
        std::max({std::abs(qref.rhov),std::abs(qref.rho*c.UInf),1.0e-8}),
        std::max({std::abs(qref.rhoE),std::abs(c.pInf/(c.gamma-1.0)),1.0e-8})
      }};
      Matrix4 scaled{};
      for(int row=0;row<4;++row) for(int col=0;col<4;++col) scaled[row][col]=block[row][col]*scale[col]/scale[row];
      const std::array<double,4> rhs{{-gain*res[i].rho/scale[0],-gain*res[i].rhou/scale[1],-gain*res[i].rhov/scale[2],-gain*res[i].rhoE/scale[3]}};
      std::array<double,4> delta{};
      if(solve_4x4(scaled,rhs,delta)) dq={scale[0]*delta[0],scale[1]*delta[1],scale[2]*delta[2],scale[3]*delta[3]};
      else dq=res[i]*(-gain/spectral);
    }
    else if(c.transient) dq=res[i]*(-gain/spectral);
    else if(c.steadyBlockCoupling>0.0) {
      // Optional frozen local 4x4 block-Jacobi correction for steady
      // pseudo-time iterations.  The scalar path above remains the default.
      // For each face, freeze the neighbor and Rusanov signal and retain the
      // derivative with respect to this cell only: 1/2(A+s|n|I).  Wall faces
      // use the exact pressure-traction Jacobian.  Viscous and pseudo-time
      // contributions are retained as a scalar diagonal base, so the block
      // cannot remove the stabilizing radius used by the established update.
      const double beta=std::clamp(c.steadyBlockCoupling,0.0,1.0);
      Matrix4 block{};
      // Retain the complete scalar spectral radius as the positive diagonal
      // backbone.  The frozen flux Jacobian is an additive coupling model;
      // blending it by reducing the diagonal itself made the optional block
      // path less robust than the scalar update on highly stretched cells.
      for(int k=0;k<4;++k) block[k][k]=spectral;
      for(int fi:m.cells[i].faces) {
        const Face& f=m.faces[fi];
        const bool first=f.a==i;
        const V2 n=first?f.normal:f.normal*(-1.0);
        const int j=first?f.b:f.a;
        const bool wall=j<0 && (boundary_type(f,c)=="slip_wall" || boundary_type(f,c)=="no_slip_adiabatic_wall");
        const Matrix4 jac=wall?wall_pressure_flux_jacobian(q[i],n,c):euler_flux_jacobian(q[i],n,c);
        const double signal=wall?0.0:rusanov_signal(q[i],j>=0?q[j]:freestream(c),n,c);
        const double area=norm(n);
        const double physical_factor=wall?1.0:0.5;
        for(int row=0;row<4;++row) for(int col=0;col<4;++col) {
          // `assemble` always passes the current cell as the left state and
          // orients n outward from it (reversing f.normal when i==f.b), so
          // the local Rusanov diagonal is +1/2 s|n| for both orientations.
          const double dissipation=(row==col && !wall)?0.5*signal*area:0.0;
          block[row][col]+=beta*(physical_factor*jac[row][col]+dissipation);
        }
      }
      // Conservative variables have very different magnitudes (especially
      // rhoE versus momentum at low Mach).  Solve the same block in a
      // freestream-scaled basis to avoid pivot decisions being dominated by
      // the energy row/column, then map the correction back conservatively.
      const U qref=freestream(c);
      const std::array<double,4> scale{{
        std::max(std::abs(qref.rho),1.0e-8),
        std::max({std::abs(qref.rhou),std::abs(qref.rho*c.UInf),1.0e-8}),
        std::max({std::abs(qref.rhov),std::abs(qref.rho*c.UInf),1.0e-8}),
        std::max({std::abs(qref.rhoE),std::abs(c.pInf/(c.gamma-1.0)),1.0e-8})
      }};
      Matrix4 scaled{};
      for(int row=0;row<4;++row) for(int col=0;col<4;++col) scaled[row][col]=block[row][col]*scale[col]/scale[row];
      // Keep the scalar pseudo-time normalization exactly when beta=0: the
      // block solve only replaces the spectral diagonal, while omega and CFL
      // retain their established steady semantics.
      const double pseudo_factor=c.steadyOmega*cfl/std::max(1.0+cfl,1.0);
      const std::array<double,4> rhs{{-pseudo_factor*res[i].rho/scale[0],
                                      -pseudo_factor*res[i].rhou/scale[1],
                                      -pseudo_factor*res[i].rhov/scale[2],
                                      -pseudo_factor*res[i].rhoE/scale[3]}};
      std::array<double,4> delta{};
      if(solve_4x4(scaled,rhs,delta)) dq={scale[0]*delta[0],scale[1]*delta[1],scale[2]*delta[2],scale[3]*delta[3]};
      else dq=res[i]*(-pseudo_factor/std::max(spectral,1.0e-8));
    }
    else {
      // `dtau` already contains the bounded CFL normalization.  Applying the
      // same `(1+CFL)` factor to the relaxation weight a second time makes
      // the steady correction decay like 1/CFL as the supplied continuation
      // ramps up, effectively freezing the solve before it reaches its
      // production CFL cap.  Keep the user-controlled relaxation weight
      // independent of that local pseudo-time normalization.
      const double omega=c.steadyOmega;
      dq=res[i]*(-omega*dtau/std::max(m.cells[i].area,1.0e-14));
    }
    const U qref=freestream(c);
    // Limit the conservative correction only by a bounded local step and a
    // true admissibility line search.  Converting an inadmissible trial to
    // primitive variables (and then clipping p) is non-conservative and was
    // the source of the uniform wall-pressure floor in earlier runs.
    const double state_cap=c.transient?0.50:0.10;
    const double momentum_ref=std::max(std::abs(qref.rhou),std::abs(qref.rhov));
    const double caps[4]={state_cap*std::max(std::abs(q[i].rho),std::abs(qref.rho)),
                          state_cap*std::max(std::abs(q[i].rhou),momentum_ref),
                          state_cap*std::max(std::abs(q[i].rhov),momentum_ref),
                          state_cap*std::max(std::abs(q[i].rhoE),std::abs(qref.rhoE))};
    double alpha=1.0;
    const double* dv[4]={&dq.rho,&dq.rhou,&dq.rhov,&dq.rhoE};
    for(int k=0;k<4;++k) if(std::abs(*dv[k])>caps[k]) alpha=std::min(alpha,caps[k]/std::abs(*dv[k]));
    auto admissible=[&](const U& s) {
      if(!std::isfinite(s.rho)||!std::isfinite(s.rhou)||!std::isfinite(s.rhov)||!std::isfinite(s.rhoE)||s.rho<=1.0e-10)return false;
      const double kinetic=0.5*(s.rhou*s.rhou+s.rhov*s.rhov)/s.rho;
      const double pressure=(c.gamma-1.0)*(s.rhoE-kinetic);
      return std::isfinite(pressure)&&pressure>1.0e-10*std::max(c.pInf,1.0);
    };
    auto step_ok=[&](const U& s) {
      if(!admissible(s)) return false;
      const double kinetic=0.5*(s.rhou*s.rhou+s.rhov*s.rhov)/s.rho;
      Prim wt{s.rho,s.rhou/s.rho,s.rhov/s.rho,(c.gamma-1.0)*(s.rhoE-kinetic),0.0};
      const double primitive_cap=c.transient?1.00:0.02;
      return std::abs(wt.rho-w.rho)<=primitive_cap*std::max(std::abs(w.rho),std::abs(c.rhoInf)) &&
             std::abs(wt.u-w.u)<=primitive_cap*std::max(std::abs(c.UInf),1.0) &&
             std::abs(wt.v-w.v)<=primitive_cap*std::max(std::abs(c.UInf),1.0) &&
             std::abs(wt.p-w.p)<=primitive_cap*std::max(std::abs(w.p),std::abs(c.pInf));
    };
    U trial=q[i]+dq*alpha;
    if(!step_ok(trial)) {
      double lo=0.0, hi=alpha;
      for(int it=0;it<40;++it){const double mid=0.5*(lo+hi);if(step_ok(q[i]+dq*mid))lo=mid;else hi=mid;}
      alpha=0.95*lo; trial=q[i]+dq*alpha;
    }
    if(step_ok(trial)) q[i]=trial;
    enforce_positive(q[i],c);
  }
}

struct MatrixFreeRescueResult {
  bool accepted=false;
  int nonlinear_iterations=0;
  int krylov_products=0;
  double ratio=1.0;
};

static double mf_scaled_dot(const Partition& p, const std::vector<U>& a,
                            const std::vector<U>& b, const Case& c) {
  const double sr=std::max(std::abs(c.rhoInf),1.0e-8);
  const double sm=std::max(std::abs(c.rhoInf*c.UInf),1.0e-8);
  const double se=std::max(std::abs(c.pInf/(c.gamma-1.0)),1.0e-8);
  double local=0.0;
  #pragma omp parallel for reduction(+:local) schedule(static)
  for(std::ptrdiff_t oi=0;oi<static_cast<std::ptrdiff_t>(p.owned.size());++oi){
    const int i=p.owned[static_cast<std::size_t>(oi)];
    local+=(a[i].rho/sr)*(b[i].rho/sr)+(a[i].rhou/sm)*(b[i].rhou/sm)+
           (a[i].rhov/sm)*(b[i].rhov/sm)+(a[i].rhoE/se)*(b[i].rhoE/se);
  }
  double global=0.0; MPI_Allreduce(&local,&global,1,MPI_DOUBLE,MPI_SUM,MPI_COMM_WORLD); return global;
}

static double mf_scaled_norm(const Partition& p, const std::vector<U>& a, const Case& c) {
  return std::sqrt(std::max(mf_scaled_dot(p,a,a,c),0.0));
}

static bool mf_admissible(const Partition& p, const std::vector<U>& q, const Case& c) {
  int local_bad=0;
  #pragma omp parallel for reduction(+:local_bad) schedule(static)
  for(std::ptrdiff_t oi=0;oi<static_cast<std::ptrdiff_t>(p.owned.size());++oi){
    const U& s=q[p.owned[static_cast<std::size_t>(oi)]];
    if(!std::isfinite(s.rho)||!std::isfinite(s.rhou)||!std::isfinite(s.rhov)||!std::isfinite(s.rhoE)||s.rho<=1.0e-10){++local_bad;continue;}
    const double pval=(c.gamma-1.0)*(s.rhoE-0.5*(s.rhou*s.rhou+s.rhov*s.rhov)/s.rho);
    if(!std::isfinite(pval)||pval<=1.0e-10*std::max(c.pInf,1.0)) ++local_bad;
  }
  int global_bad=0; MPI_Allreduce(&local_bad,&global_bad,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD); return global_bad==0;
}

static ResidualResult mf_transient_residual(const Mesh& m, const Partition& p,
                                             std::vector<U>& q,
                                             const std::vector<U>& qn,
                                             const std::vector<U>& qnm1,
                                             std::vector<U>& res,
                                             std::vector<Grad>& grad,
                                             const Case& c, bool backward_euler) {
  assemble(m,p,q,res,grad,c,false);
  const double dt=std::max(c.dt,1.0e-12);
  for(int i:p.owned) res[i]+=(backward_euler
    ? (q[i]-qn[i])*(m.cells[i].area/dt)
    : (3.0*q[i]-4.0*qn[i]+qnm1[i])*(m.cells[i].area/(2.0*dt)));
  return residual_stats(p,res);
}

static void mf_build_diagonal(const Mesh& m, const Partition& p,
                              const std::vector<U>& q, const Case& c,
                              bool backward_euler, std::vector<double>& diagonal) {
  diagonal.assign(q.size(),1.0);
  const double mu=(c.mode=="laminar"&&c.Re>0.0)?c.rhoInf*c.UInf*c.refL/c.Re:0.0;
  const double coeff=backward_euler?1.0:1.5;
  #pragma omp parallel for schedule(static)
  for(std::ptrdiff_t oi=0;oi<static_cast<std::ptrdiff_t>(p.owned.size());++oi){
    const int i=p.owned[static_cast<std::size_t>(oi)]; const Prim w=primitive(q[i],c);
    double waves=0.0, visc=0.0;
    for(int fi:m.cells[i].faces){
      const Face& f=m.faces[fi]; const double sound=std::sqrt(c.gamma*w.p/std::max(w.rho,kFloor));
      const double mach=std::sqrt(w.u*w.u+w.v*w.v)/std::max(sound,kFloor);
      const double af=std::clamp(std::max(mach,c.transientLowMachCutoff),c.transientLowMachCutoff,1.0);
      waves+=(std::abs(w.u*f.normal.x/f.area+w.v*f.normal.y/f.area)+c.rusanovScale*af*sound)*f.area;
      if(mu>0.0){
        const V2 nh=f.normal*(1.0/std::max(f.area,kFloor)); const int j=f.a==i?f.b:f.a;
        const V2 d=j>=0?m.cells[j].center-m.cells[i].center:f.center-m.cells[i].center;
        visc+=4.0*mu*f.area/(std::max(w.rho,1.0e-8)*std::max(std::abs(dot(d,nh)),1.0e-10));
      }
    }
    const double temporal=coeff*m.cells[i].area/std::max(c.dt,1.0e-12);
    diagonal[static_cast<std::size_t>(i)]=std::max(waves+visc+temporal,1.0e-8);
  }
}

static MatrixFreeRescueResult matrix_free_transient_rescue(
    const Mesh& m, const Partition& p, std::vector<U>& q,
    const std::vector<U>& qn, const std::vector<U>& qnm1,
    std::vector<U>& res, std::vector<Grad>& grad, const Case& c,
  bool backward_euler) {
  MatrixFreeRescueResult result;
  const std::vector<U> origin=q, origin_res=res; const std::vector<Grad> origin_grad=grad;
  ResidualResult current=mf_transient_residual(m,p,q,qn,qnm1,res,grad,c,backward_euler);
  const double initial_l2=std::max(current.l2,kFloor);
  if(!std::isfinite(initial_l2)) return result;
  constexpr int restart_dim=8;
  // Allow the bounded residual direction to carry a difficult wake interval
  // through the nonlinear basin rather than immediately paying for the much
  // more expensive BE subdivision ladder.  The strict target is still the
  // only acceptance criterion; these are rescue iterations, not best-effort
  // physical-step promotions.
  for(int outer=0;outer<20;++outer){
    const double base_norm=mf_scaled_norm(p,res,c); if(!std::isfinite(base_norm)||base_norm<=kFloor) break;
    std::vector<double> diagonal; mf_build_diagonal(m,p,q,c,backward_euler,diagonal);
    std::vector<U> rhs(q.size(),U{}); for(int i:p.owned) rhs[i]=res[i]*(-1.0);
    const double beta=mf_scaled_norm(p,rhs,c); if(!std::isfinite(beta)||beta<=kFloor) break;
    std::vector<std::vector<U>> basis(static_cast<std::size_t>(restart_dim+1),std::vector<U>(q.size(),U{}));
    std::vector<std::vector<U>> zvec(static_cast<std::size_t>(restart_dim),std::vector<U>(q.size(),U{}));
    std::vector<std::vector<double>> h(static_cast<std::size_t>(restart_dim+1),std::vector<double>(static_cast<std::size_t>(restart_dim),0.0));
    std::array<double,restart_dim> cs{},sn{}; std::array<double,restart_dim+1> g{}; g[0]=beta;
    for(int i:p.owned) basis[0][i]=rhs[i]*(1.0/beta);
    int used=0;
    for(int k=0;k<restart_dim;++k){
      for(int i:p.owned) zvec[static_cast<std::size_t>(k)][i]=basis[static_cast<std::size_t>(k)][i]*(1.0/diagonal[static_cast<std::size_t>(i)]);
      // The reconstructed/limited residual is only piecewise smooth.  A
      // perturbation at machine-near 1e-6 of a preconditioned basis vector
      // can sit entirely inside limiter branch noise on the micron-scale wall
      // layers; use a modest relative directional difference and let the
      // admissibility loop reduce it only when needed.
      double eps=1.0e-2; std::vector<U> qeps=q; bool ok=false;
      for(int trial=0;trial<10;++trial){
        qeps=q; for(int i:p.owned) qeps[i]=q[i]+zvec[static_cast<std::size_t>(k)][i]*eps;
        if(mf_admissible(p,qeps,c)){ok=true;break;} eps*=0.5;
      }
      if(!ok) break;
      std::vector<U> peps(q.size(),U{}); std::vector<Grad> geps;
      mf_transient_residual(m,p,qeps,qn,qnm1,peps,geps,c,backward_euler);
      std::vector<U> w(q.size(),U{});
      for(int i:p.owned) w[i]=(peps[i]-res[i])*(1.0/eps);
      ++result.krylov_products; ++used;
      for(int j=0;j<=k;++j){
        const double hij=mf_scaled_dot(p,w,basis[static_cast<std::size_t>(j)],c); h[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)]=hij;
        for(int i:p.owned) w[i]-=basis[static_cast<std::size_t>(j)][i]*hij;
      }
      const double hnext=mf_scaled_norm(p,w,c); h[static_cast<std::size_t>(k+1)][static_cast<std::size_t>(k)]=hnext;
      if(hnext>1.0e-14) for(int i:p.owned) basis[static_cast<std::size_t>(k+1)][i]=w[i]*(1.0/hnext);
      for(int j=0;j<k;++j){
        const double a=h[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)], b=h[static_cast<std::size_t>(j+1)][static_cast<std::size_t>(k)];
        h[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)]=cs[static_cast<std::size_t>(j)]*a+sn[static_cast<std::size_t>(j)]*b;
        h[static_cast<std::size_t>(j+1)][static_cast<std::size_t>(k)]=-sn[static_cast<std::size_t>(j)]*a+cs[static_cast<std::size_t>(j)]*b;
      }
      const double a=h[static_cast<std::size_t>(k)][static_cast<std::size_t>(k)], b=h[static_cast<std::size_t>(k+1)][static_cast<std::size_t>(k)], den=std::hypot(a,b);
      if(!std::isfinite(den)||den<=1.0e-14) break;
      cs[static_cast<std::size_t>(k)]=a/den; sn[static_cast<std::size_t>(k)]=b/den; h[static_cast<std::size_t>(k)][static_cast<std::size_t>(k)]=den; h[static_cast<std::size_t>(k+1)][static_cast<std::size_t>(k)]=0.0;
      const double old=g[static_cast<std::size_t>(k)]; g[static_cast<std::size_t>(k)]=cs[static_cast<std::size_t>(k)]*old; g[static_cast<std::size_t>(k+1)]=-sn[static_cast<std::size_t>(k)]*old;
      if(std::abs(g[static_cast<std::size_t>(k+1)])<=1.0e-5*beta) break;
    }
    if(used==0) break;
    std::vector<double> y(static_cast<std::size_t>(used),0.0); bool solve_ok=true;
    for(int r=used-1;r>=0;--r){double value=g[static_cast<std::size_t>(r)];for(int col=r+1;col<used;++col)value-=h[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)]*y[static_cast<std::size_t>(col)];if(std::abs(h[static_cast<std::size_t>(r)][static_cast<std::size_t>(r)])<1.0e-14){solve_ok=false;break;}y[static_cast<std::size_t>(r)]=value/h[static_cast<std::size_t>(r)][static_cast<std::size_t>(r)];}
    if(!solve_ok) break;
    std::vector<U> direction(q.size(),U{}); for(int j=0;j<used;++j) for(int i:p.owned) direction[i]+=zvec[static_cast<std::size_t>(j)][i]*y[static_cast<std::size_t>(j)];
    // Use the finite-difference GMRES direction itself.  If its local
    // Jacobian model is rank-deficient, the Armijo trial below rejects it and
    // the caller retains the ordinary bounded retry/fallback ladder; silently
    // replacing it by a diagonal Richardson step would make this path a
    // diagnostic counter rather than a genuine matrix-free Newton rescue.
    double local_rel=0.0;
    #pragma omp parallel for reduction(max:local_rel) schedule(static)
    for(std::ptrdiff_t oi=0;oi<static_cast<std::ptrdiff_t>(p.owned.size());++oi){const int i=p.owned[static_cast<std::size_t>(oi)];local_rel=std::max(local_rel,std::abs(direction[i].rho)/std::max(std::abs(q[i].rho),1.0e-8));local_rel=std::max(local_rel,std::abs(direction[i].rhou)/std::max(std::abs(q[i].rhou),std::abs(c.rhoInf*c.UInf)));local_rel=std::max(local_rel,std::abs(direction[i].rhov)/std::max(std::abs(q[i].rhov),std::abs(c.rhoInf*c.UInf)));local_rel=std::max(local_rel,std::abs(direction[i].rhoE)/std::max(std::abs(q[i].rhoE),std::abs(c.pInf/(c.gamma-1.0))));}
    double global_rel=0.0; MPI_Allreduce(&local_rel,&global_rel,1,MPI_DOUBLE,MPI_MAX,MPI_COMM_WORLD); const double cap=std::min(1.0,1.0/std::max(global_rel,1.0e-12)); for(int i:p.owned) direction[i]=direction[i]*cap;
    const std::vector<U> qbase=q; const std::vector<U> rbase=res; const std::vector<Grad> gbase=grad; bool accepted=false;
    for(double sign:std::array<double,2>{{1.0,-1.0}}){
      for(double alpha:std::array<double,27>{{1.0,0.5,0.25,0.125,0.0625,0.03125,0.015625,0.0078125,0.00390625,0.001953125,0.0009765625,0.00048828125,0.000244140625,0.0001220703125,0.00006103515625,0.000030517578125,0.0000152587890625,0.00000762939453125,0.000003814697265625,0.0000019073486328125,0.00000095367431640625,0.000000476837158203125,0.0000002384185791015625,0.00000011920928955078125,0.000000059604644775390625,0.0000000298023223876953125,0.00000001490116119384765625}}){
        q=qbase; for(int i:p.owned) q[i]=qbase[i]+direction[i]*(sign*alpha);
        if(!mf_admissible(p,q,c)) continue;
        std::vector<U> trial_res(q.size(),U{}); std::vector<Grad> trial_grad; const ResidualResult trial=mf_transient_residual(m,p,q,qn,qnm1,trial_res,trial_grad,c,backward_euler); const double trial_norm=mf_scaled_norm(p,trial_res,c);
        if(std::isfinite(trial_norm)&&trial_norm<=(1.0-1.0e-4*alpha)*base_norm){res=std::move(trial_res);grad=std::move(trial_grad);current=trial;accepted=true;break;}
      }
      if(accepted) break;
    }
    if(!accepted){q=qbase;res=rbase;grad=gbase;break;}
    ++result.nonlinear_iterations; result.ratio=current.l2/initial_l2;
    if(result.ratio<=c.innerTarget){result.accepted=true;return result;}
  }
  q=origin;res=origin_res;grad=origin_grad;result.ratio=1.0;return result;
}

struct Forces { double cl=0,cd=0,cmz=0,pd=0,vd=0,pl=0,vl=0; };
static Forces local_forces(const Mesh& m, const Partition& p, const std::vector<U>& q, const std::vector<Grad>& grad, const Case& c) {
  Forces f; double qref=0.5*c.rhoInf*c.UInf*c.UInf*c.refA; if(qref<kFloor)qref=1;
  double vals[7]={}; const double mu=(c.mode=="laminar"&&c.Re>0)?c.rhoInf*c.UInf*c.refL/c.Re:0;
  #pragma omp parallel for reduction(+:vals) schedule(static)
  for(std::ptrdiff_t owned_idx=0; owned_idx<static_cast<std::ptrdiff_t>(p.owned.size()); ++owned_idx){ const int i=p.owned[static_cast<std::size_t>(owned_idx)]; for(int fi:m.cells[i].faces){
    const Face& face=m.faces[fi]; if(face.b>=0)continue;
    std::string bc=boundary_type(face,c); if(bc!="slip_wall"&&bc!="no_slip_adiabatic_wall")continue;
    // Forces are evaluated at the same reconstructed face state used by the
    // residual, rather than at the cell centre.  This keeps the final force
    // row and surface traction tied to the written final field.
    Prim w=reconstruct(m,i,face.center,q,grad,c); V2 n=face.normal*(1.0/std::max(face.area,kFloor));
    double fx=w.p*n.x*face.area,fy=w.p*n.y*face.area; vals[0]+=fx;vals[1]+=fy;vals[2]+=(face.center.x-c.cx)*fy-(face.center.y-c.cy)*fx;
    if(mu>0&&bc=="no_slip_adiabatic_wall"){
      V2 t{-n.y,n.x}; Grad g=wall_gradient(m,face,i,primitive(q[i],c),grad[i],c); double div=g.dux+g.dvy,txx=mu*(2.0*g.dux-2.0*div/3.0),tyy=mu*(2.0*g.dvy-2.0*div/3.0),txy=mu*(g.duy+g.dvx);
      double shear=(txx*n.x+txy*n.y)*t.x+(txy*n.x+tyy*n.y)*t.y;
      // n points out of the fluid cell into the solid.  The viscous traction
      // exerted by the fluid on the body is therefore the negative of the
      // Cauchy traction used in the residual's body-on-fluid balance.
      double sfx=-shear*t.x*face.area,sfy=-shear*t.y*face.area; vals[3]+=sfx;vals[4]+=sfy;vals[5]+=(face.center.x-c.cx)*sfy-(face.center.y-c.cy)*sfx;
    }
  } }
  double all[7]={};MPI_Reduce(vals,all,7,MPI_DOUBLE,MPI_SUM,0,MPI_COMM_WORLD);if(qref>0){f.pd=all[0]/qref;f.pl=all[1]/qref;f.vd=all[3]/qref;f.vl=all[4]/qref;f.cd=f.pd+f.vd;f.cl=f.pl+f.vl;f.cmz=(all[2]+all[5])/(qref*c.refL);}return f;
}

static std::string utc_now(){auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm tm{};gmtime_r(&t,&tm);char b[32];std::strftime(b,sizeof(b),"%Y-%m-%dT%H:%M:%SZ",&tm);return b;}

static std::vector<U> gather_state(const std::vector<int>& global_ids, const Partition& p, const std::vector<U>& q, int rank, int nr, int global_cells) {
  const int n = static_cast<int>(p.owned.size());
  std::vector<int> counts(rank == 0 ? nr : 0), displs(rank == 0 ? nr : 0);
  MPI_Gather(&n, 1, MPI_INT, rank == 0 ? counts.data() : nullptr, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (rank == 0) { int d = 0; for (int i = 0; i < nr; ++i) { displs[i] = d; d += counts[i]; } }
  std::vector<int> ids(n); for (int i = 0; i < n; ++i) ids[i] = global_ids[p.owned[i]];
  const int total = rank == 0 ? std::accumulate(counts.begin(), counts.end(), 0) : 0;
  std::vector<int> allids(rank == 0 ? total : 0);
  MPI_Gatherv(ids.data(), n, MPI_INT, rank == 0 ? allids.data() : nullptr, rank == 0 ? counts.data() : nullptr, rank == 0 ? displs.data() : nullptr, MPI_INT, 0, MPI_COMM_WORLD);

  std::vector<double> values(static_cast<std::size_t>(n) * 4);
  for (int i = 0; i < n; ++i) { const U& u = q[p.owned[i]]; values[4*i] = u.rho; values[4*i+1] = u.rhou; values[4*i+2] = u.rhov; values[4*i+3] = u.rhoE; }
  std::vector<int> counts4(rank == 0 ? nr : 0), displs4(rank == 0 ? nr : 0);
  if (rank == 0) for (int i = 0; i < nr; ++i) { counts4[i] = counts[i] * 4; displs4[i] = displs[i] * 4; }
  std::vector<double> allvalues(rank == 0 ? static_cast<std::size_t>(total) * 4 : 0);
  MPI_Gatherv(values.data(), n * 4, MPI_DOUBLE, rank == 0 ? allvalues.data() : nullptr, rank == 0 ? counts4.data() : nullptr, rank == 0 ? displs4.data() : nullptr, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  std::vector<U> out(rank == 0 ? static_cast<std::size_t>(global_cells) : 0);
  if (rank == 0) {
    if (total != global_cells) throw std::runtime_error("final state gather did not cover every global cell");
    std::vector<char> seen(static_cast<std::size_t>(global_cells), 0);
    for (int k = 0; k < total; ++k) {
      const int gid = allids[k];
      if (gid < 0 || gid >= global_cells || seen[gid]) throw std::runtime_error("final state gather has duplicate/invalid global cell id");
      seen[gid] = 1; out[gid] = {allvalues[4*k],allvalues[4*k+1],allvalues[4*k+2],allvalues[4*k+3]};
    }
  }
  return out;
}

static void write_vtk(const fs::path& path, const Mesh& m, const std::vector<U>& q, const std::vector<int>& owner, const Case& c) {
  std::ofstream o(path);o<<"# vtk DataFile Version 3.0\nAurora-FV cell-centred field\nASCII\nDATASET UNSTRUCTURED_GRID\nPOINTS "<<m.points.size()<<" double\n";for(auto p:m.points)o<<std::setprecision(16)<<p.x<<" "<<p.y<<" 0\n";std::size_t total=0;for(auto&c0:m.cells)total+=1+c0.nodes.size();o<<"CELLS "<<m.cells.size()<<" "<<total<<"\n";for(auto&c0:m.cells){o<<c0.nodes.size();for(int n:c0.nodes)o<<" "<<n;o<<"\n";}o<<"CELL_TYPES "<<m.cells.size()<<"\n";for(auto&c0:m.cells)o<<(c0.nodes.size()==3?5:9)<<"\n";o<<"CELL_DATA "<<m.cells.size()<<"\n";auto scalar=[&](const char*n,auto fn){o<<"SCALARS "<<n<<" double 1\nLOOKUP_TABLE default\n";for(std::size_t i=0;i<m.cells.size();++i)o<<fn(i)<<"\n";};auto omega=[&](std::size_t i){const Cell&ci=m.cells[i];double a=0,b=0,d=0,sux=0,suy=0,svx=0,svy=0;Prim wi=primitive(q[i],c);for(int j:ci.nbr){V2 dx=m.cells[j].center-ci.center;a+=dx.x*dx.x;b+=dx.x*dx.y;d+=dx.y*dx.y;Prim wj=primitive(q[j],c);sux+=dx.x*(wj.u-wi.u);suy+=dx.y*(wj.u-wi.u);svx+=dx.x*(wj.v-wi.v);svy+=dx.y*(wj.v-wi.v);}double det=a*d-b*b;if(std::abs(det)<1e-20)return 0.0;return (d*svx-b*svy)/det-(a*suy-b*sux)/det;};scalar("density",[&](std::size_t i){return primitive(q[i],c).rho;});scalar("u",[&](std::size_t i){return primitive(q[i],c).u;});scalar("v",[&](std::size_t i){return primitive(q[i],c).v;});scalar("pressure",[&](std::size_t i){return primitive(q[i],c).p;});scalar("temperature",[&](std::size_t i){return primitive(q[i],c).T;});scalar("mach",[&](std::size_t i){auto w=primitive(q[i],c);return std::sqrt(w.u*w.u+w.v*w.v)/std::sqrt(c.gamma*w.p/w.rho);});scalar("velocity_magnitude",[&](std::size_t i){auto w=primitive(q[i],c);return std::sqrt(w.u*w.u+w.v*w.v);});scalar("vorticity",omega);scalar("owner_id",[&](std::size_t i){return owner[i];});
}
static void write_surface(const fs::path& path, const Mesh& m, const std::vector<U>& q, const Case& c) {
  std::ofstream o(path);o<<"x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";double qref=0.5*c.rhoInf*c.UInf*c.UInf;const double mu=(c.mode=="laminar"&&c.Re>0)?c.rhoInf*c.UInf*c.refL/c.Re:0;
  Partition all; all.owned.resize(m.cells.size()); std::iota(all.owned.begin(),all.owned.end(),0); std::vector<Grad> grad=gradients(m,all,q,c);
  for(auto&f:m.faces)if(f.b<0){
    std::string bc=boundary_type(f,c);if(bc!="slip_wall"&&bc!="no_slip_adiabatic_wall")continue;
    int i=f.a; Prim w=reconstruct(m,i,f.center,q,grad,c); V2 n=f.normal*(1.0/std::max(f.area,kFloor));
    // Surface semantics are boundary values: no-slip velocity is zero and
    // slip-wall velocity is projected to remove only its normal component.
    if(bc=="no_slip_adiabatic_wall"){w.u=0;w.v=0;}else{double vn=w.u*n.x+w.v*n.y;w.u-=vn*n.x;w.v-=vn*n.y;}
    double mach=std::sqrt(w.u*w.u+w.v*w.v)/std::sqrt(c.gamma*w.p/w.rho);double cf=0;
    if(mu>0&&bc=="no_slip_adiabatic_wall"){V2 t{-n.y,n.x};Grad g=wall_gradient(m,f,i,primitive(q[i],c),grad[i],c);double div=g.dux+g.dvy,txx=mu*(2.0*g.dux-2.0*div/3.0),tyy=mu*(2.0*g.dvy-2.0*div/3.0),txy=mu*(g.duy+g.dvx);double shear=(txx*n.x+txy*n.y)*t.x+(txy*n.x+tyy*n.y)*t.y;cf=std::abs(shear)/std::max(qref,1.0e-12);}
    o<<std::setprecision(16)<<f.center.x<<","<<f.center.y<<","<<n.x<<","<<n.y<<","<<w.p<<","<<(w.p-c.pInf)/std::max(qref,1.0e-12)<<","<<cf<<","<<w.rho<<","<<w.u<<","<<w.v<<","<<mach<<","<<f.tag<<"\n";
  }
}

static void write_partition_diag(const fs::path& path, const Mesh& m, const std::vector<int>& owner, int nr) {
  std::ofstream o(path);o<<"rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";for(int r=0;r<nr;++r){Partition p=make_partition(m,r,nr,&owner);std::set<int> ns;for(auto&kv:p.recv)ns.insert(kv.first);int nb=0;for(int i:p.owned)for(int fi:m.cells[i].faces)if(m.faces[fi].b<0)++nb;std::ostringstream neigh;bool first=true;for(int n:ns){if(!first)neigh<<";";first=false;neigh<<n;}o<<r<<","<<p.owned.size()<<","<<p.ghosts.size()<<","<<nb<<","<<ns.size()<<",\""<<neigh.str()<<"\",";std::size_t s=0,rv=0;for(auto&kv:p.send)s+=kv.second.size();for(auto&kv:p.recv)rv+=kv.second.size();o<<s<<","<<rv<<"\n";}}

int main(int argc, char** argv) {
  MPI_Init(&argc,&argv);int rank=0,nr=1;MPI_Comm_rank(MPI_COMM_WORLD,&rank);MPI_Comm_size(MPI_COMM_WORLD,&nr);
  // The benchmark launch is MPI-first.  Respect an explicit OMP_NUM_THREADS
  // setting so production runs can use hybrid MPI/OpenMP on multi-core hosts;
  // retain the conservative one-thread default when the caller has not
  // requested threading (omp_get_max_threads()==1 in that configuration).
  // This avoids silent oversubscription while permitting a bounded threaded
  // run such as OMP_NUM_THREADS=4 with four MPI ranks.
  const char* omp_threads_env=std::getenv("OMP_NUM_THREADS");
  if(nr>1 && (!omp_threads_env || *omp_threads_env=='\0')) omp_set_num_threads(1);
  try {
    if(argc<6 || std::string(argv[1])!="solve") throw std::runtime_error("usage: aurora-fv solve --case case.json --output output-dir [--restart restart] [--resume-step N] [--report-level brief|full] [--max-steps N] [--cfl-max CFL] [--steady-omega O] [--steady-block-coupling B] [--steady-plateau-stop] [--transient-slope S] [--transient-gain G] [--transient-seed A] [--transient-cutoff M] [--transient-block-coupling B] [--inner-target R] [--rusanov-scale R] [--inviscid-flux rusanov|hllc] [--hllc-blend B] [--transient-linesearch on|off] [--no-wall-init]");
    fs::path case_path, out_path, restart_path; int max_steps_override=0, resume_step_override=0; double cfl_max_override=-1.0, steady_omega_override=-1.0, steady_block_coupling_override=-1.0, transient_slope_override=-1.0, transient_gain_override=-1.0, transient_seed_override=-1.0, transient_cutoff_override=-1.0, transient_block_coupling_override=-1.0, inner_target_override=-1.0, rusanov_scale_override=-1.0, hllc_blend_override=-1.0; std::string inviscid_flux_override; bool no_wall_init=false, steady_plateau_stop=false, transient_linesearch_override=true; bool transient_linesearch_seen=false;
    for(int i=2;i<argc;++i){std::string k=argv[i];if((k=="--case"||k=="--output"||k=="--restart"||k=="--report-level"||k=="--max-steps"||k=="--resume-step"||k=="--cfl-max"||k=="--steady-omega"||k=="--steady-block-coupling"||k=="--transient-slope"||k=="--transient-gain"||k=="--transient-seed"||k=="--transient-cutoff"||k=="--transient-block-coupling"||k=="--inner-target"||k=="--rusanov-scale"||k=="--inviscid-flux"||k=="--hllc-blend"||k=="--transient-linesearch")&&i+1<argc){if(k=="--case")case_path=argv[++i];else if(k=="--output")out_path=argv[++i];else if(k=="--max-steps")max_steps_override=std::stoi(argv[++i]);else if(k=="--restart")restart_path=argv[++i];else if(k=="--resume-step")resume_step_override=std::stoi(argv[++i]);else if(k=="--cfl-max")cfl_max_override=std::stod(argv[++i]);else if(k=="--steady-omega")steady_omega_override=std::stod(argv[++i]);else if(k=="--steady-block-coupling")steady_block_coupling_override=std::stod(argv[++i]);else if(k=="--transient-slope")transient_slope_override=std::stod(argv[++i]);else if(k=="--transient-gain")transient_gain_override=std::stod(argv[++i]);else if(k=="--transient-seed")transient_seed_override=std::stod(argv[++i]);else if(k=="--transient-cutoff")transient_cutoff_override=std::stod(argv[++i]);else if(k=="--transient-block-coupling")transient_block_coupling_override=std::stod(argv[++i]);else if(k=="--inner-target")inner_target_override=std::stod(argv[++i]);else if(k=="--rusanov-scale")rusanov_scale_override=std::stod(argv[++i]);else if(k=="--inviscid-flux")inviscid_flux_override=argv[++i];else if(k=="--hllc-blend")hllc_blend_override=std::stod(argv[++i]);else if(k=="--transient-linesearch"){const std::string value=argv[++i];if(value=="on")transient_linesearch_override=true;else if(value=="off")transient_linesearch_override=false;else throw std::runtime_error("--transient-linesearch expects on or off");transient_linesearch_seen=true;}else ++i;}else if(k=="--no-wall-init") no_wall_init=true; else if(k=="--steady-plateau-stop") steady_plateau_stop=true; else throw std::runtime_error("unknown or incomplete argument: "+k);}if(case_path.empty()||out_path.empty())throw std::runtime_error("--case and --output are required");
    Case c=read_case(case_path); if(max_steps_override>0) c.maxSteps=max_steps_override; if(cfl_max_override>0.0) c.cflMax=cfl_max_override; if(steady_omega_override>0.0) c.steadyOmega=steady_omega_override; if(steady_block_coupling_override>=0.0) c.steadyBlockCoupling=steady_block_coupling_override; if(transient_slope_override>=0.0) c.transientSlope=transient_slope_override; if(transient_gain_override>0.0) c.transientGain=transient_gain_override; if(transient_seed_override>=0.0) c.transientSeed=transient_seed_override; if(transient_cutoff_override>0.0) c.transientLowMachCutoff=transient_cutoff_override; if(transient_block_coupling_override>=0.0) c.transientBlockCoupling=transient_block_coupling_override; if(inner_target_override>0.0) c.innerTarget=inner_target_override; if(rusanov_scale_override>0.0) c.rusanovScale=rusanov_scale_override; if(hllc_blend_override>=0.0) c.hllcFluxBlend=hllc_blend_override; if(!inviscid_flux_override.empty()) c.inviscidFlux=inviscid_flux_override; if(transient_linesearch_seen) c.transientLineSearch=transient_linesearch_override; c.steadyPlateauStop=steady_plateau_stop; if(c.inviscidFlux!="rusanov" && c.inviscidFlux!="hllc") throw std::runtime_error("--inviscid-flux expects rusanov or hllc"); if(!std::isfinite(c.hllcFluxBlend) || c.hllcFluxBlend<0.0 || c.hllcFluxBlend>1.0) throw std::runtime_error("--hllc-blend must be finite and in [0,1]"); if(!std::isfinite(c.steadyBlockCoupling) || c.steadyBlockCoupling<0.0 || c.steadyBlockCoupling>1.0) throw std::runtime_error("steady block coupling must be finite and in [0,1]"); if(!std::isfinite(c.transientBlockCoupling) || c.transientBlockCoupling<0.0 || c.transientBlockCoupling>1.0) throw std::runtime_error("transient block coupling must be finite and in [0,1]"); c.initializeWallState=!no_wall_init; if(resume_step_override<0) throw std::runtime_error("--resume-step must be nonnegative"); if(!c.transient && resume_step_override>0) throw std::runtime_error("--resume-step is only supported for transient cases"); if(c.transient && !restart_path.empty() && resume_step_override<0) throw std::runtime_error("--resume-step must be nonnegative"); if(c.transient && c.innerTarget>1.0e-3+1.0e-15) throw std::runtime_error("transient inner target must be 1e-3 or stricter"); fs::path mesh_path=case_path.parent_path()/c.raw.at("mesh").at("file").get<std::string>();
    // Rank zero performs the one-time CGNS read and METIS graph partitioning,
    // then emits compact rank-local topology files.  No worker loads the full
    // mesh, and the root releases its preprocessing copy before iterations.
    Mesh global_mesh; Partition global_part; LocalMeshData local; fs::path cache_dir=out_path/".partition_cache"; int global_cells=0,global_faces=0;
    if(rank==0){
      fs::create_directories(out_path);fs::create_directories(cache_dir);global_mesh=read_cgns(mesh_path);
      const auto& configured_bc=c.raw.at("boundary_conditions");
      for(const Face& f:global_mesh.faces) if(f.b<0 && !configured_bc.contains(f.tag))
        throw std::runtime_error("mesh boundary tag '"+f.tag+"' is not configured in boundary_conditions");
      annotate_wall_geometry(global_mesh,c);
      global_part=make_partition(global_mesh,0,nr);global_cells=static_cast<int>(global_mesh.cells.size());global_faces=static_cast<int>(global_mesh.faces.size());
      for(int r=0;r<nr;++r){LocalMeshData packed=build_local_partition(global_mesh,global_part.owner,r,nr);write_local_partition(cache_dir/("rank_"+std::to_string(r)+".bin"),packed);if(r==0)local=std::move(packed);}
    }
    MPI_Bcast(&global_cells,1,MPI_INT,0,MPI_COMM_WORLD);MPI_Bcast(&global_faces,1,MPI_INT,0,MPI_COMM_WORLD);MPI_Barrier(MPI_COMM_WORLD);if(rank!=0)local=read_local_partition(cache_dir/("rank_"+std::to_string(rank)+".bin"),rank);MPI_Barrier(MPI_COMM_WORLD);if(rank==0){global_mesh=Mesh{};std::error_code ec;fs::remove_all(cache_dir,ec);}MPI_Barrier(MPI_COMM_WORLD);
    if(local.global_cells!=global_cells||local.global_faces!=global_faces)throw std::runtime_error("rank-local partition metadata mismatch");
    Mesh& m=local.mesh;Partition& part=local.partition;std::vector<U> q(m.cells.size(),freestream(c)), qn=q, qnm1=q, res(m.cells.size());std::vector<Grad> grad(m.cells.size());
    // A state-only restart necessarily performs one BE interval to reconstruct
    // the missing BDF2 history.  New checkpoints also write an optional
    // sidecar containing U^{n-1}; when it is present, a resumed run can retain
    // the exact accepted BDF2 pair instead of introducing that order reduction.
    bool restart_has_bdf2_history=false;
    if (!restart_path.empty()) {
      if (!fs::exists(restart_path)) throw std::runtime_error("restart file not found: " + restart_path.string());
      // Read the global restart once on rank zero, then scatter only each
      // rank's owned states.  Ghosts are populated by the normal halo
      // exchange before their first use; no rank receives a replicated global
      // conservative-state vector.
      std::vector<double> global_restart(rank==0?static_cast<std::size_t>(global_cells)*4:0);
      if (rank==0) {
        std::ifstream in(restart_path); if(!in) throw std::runtime_error("cannot open restart file " + restart_path.string());
        for(int gid=0;gid<global_cells;++gid) for(int k=0;k<4;++k) if(!(in>>global_restart[static_cast<std::size_t>(gid)*4+k])) throw std::runtime_error("restart file has fewer states than mesh cells");
        std::string extra; if(in>>extra) throw std::runtime_error("restart file contains more states than mesh cells");
      }
      std::vector<int> send_counts(rank==0?nr:0), send_displs(rank==0?nr:0);
      std::vector<double> send_values;
      if (rank==0) {
        int offset=0;
        for(int r=0;r<nr;++r){
          int count=0; for(int gid=0;gid<global_cells;++gid) if(global_part.owner[gid]==r) count++;
          send_counts[r]=4*count; send_displs[r]=offset; offset+=send_counts[r];
        }
        send_values.resize(static_cast<std::size_t>(offset));
        for(int r=0;r<nr;++r){int pos=send_displs[r];for(int gid=0;gid<global_cells;++gid)if(global_part.owner[gid]==r)for(int k=0;k<4;++k)send_values[static_cast<std::size_t>(pos++)]=global_restart[static_cast<std::size_t>(gid)*4+k];}
      }
      const int recv_count=static_cast<int>(part.owned.size())*4;
      std::vector<double> recv_values(static_cast<std::size_t>(recv_count));
      MPI_Scatterv(rank==0?send_values.data():nullptr,rank==0?send_counts.data():nullptr,rank==0?send_displs.data():nullptr,MPI_DOUBLE,recv_values.data(),recv_count,MPI_DOUBLE,0,MPI_COMM_WORLD);
      for(std::size_t k=0;k<part.owned.size();++k){const int li=part.owned[k];q[li]={recv_values[4*k],recv_values[4*k+1],recv_values[4*k+2],recv_values[4*k+3]};enforce_positive(q[li],c);}
      qn=q; qnm1=q;
      const fs::path history_path=restart_path.parent_path()/(restart_path.stem().string()+".history"+restart_path.extension().string());
      if(c.transient && fs::exists(history_path)) {
        std::vector<double> global_history(rank==0?static_cast<std::size_t>(global_cells)*4:0);
        if(rank==0) {
          std::ifstream in(history_path); if(!in) throw std::runtime_error("cannot open restart history " + history_path.string());
          for(int gid=0;gid<global_cells;++gid) for(int k=0;k<4;++k)
            if(!(in>>global_history[static_cast<std::size_t>(gid)*4+k])) throw std::runtime_error("restart history has fewer states than mesh cells");
          std::string extra; if(in>>extra) throw std::runtime_error("restart history contains more states than mesh cells");
        }
        std::vector<int> history_counts(rank==0?nr:0), history_displs(rank==0?nr:0);
        std::vector<double> history_send;
        if(rank==0) {
          int offset=0;
          for(int r=0;r<nr;++r){int count=0;for(int gid=0;gid<global_cells;++gid)if(global_part.owner[gid]==r)++count;history_counts[r]=4*count;history_displs[r]=offset;offset+=history_counts[r];}
          history_send.resize(static_cast<std::size_t>(offset));
          for(int r=0;r<nr;++r){int pos=history_displs[r];for(int gid=0;gid<global_cells;++gid)if(global_part.owner[gid]==r)for(int k=0;k<4;++k)history_send[static_cast<std::size_t>(pos++)]=global_history[static_cast<std::size_t>(gid)*4+k];}
        }
        const int recv_count=static_cast<int>(part.owned.size())*4;
        std::vector<double> history_recv(static_cast<std::size_t>(recv_count));
        MPI_Scatterv(rank==0?history_send.data():nullptr,rank==0?history_counts.data():nullptr,rank==0?history_displs.data():nullptr,MPI_DOUBLE,history_recv.data(),recv_count,MPI_DOUBLE,0,MPI_COMM_WORLD);
        for(std::size_t k=0;k<part.owned.size();++k){const int li=part.owned[k];qnm1[li]={history_recv[4*k],history_recv[4*k+1],history_recv[4*k+2],history_recv[4*k+3]};enforce_positive(qnm1[li],c);}
        restart_has_bdf2_history=true;
      }
    }
    if (restart_path.empty()) {
      // Start viscous cases from a smooth wall-compatible profile throughout
      // the resolved boundary-layer stack, rather than setting only the first
      // wall cell to zero and leaving the immediately adjacent micron-scale
      // layers at freestream velocity.  The cached nearest-wall geometry was
      // generated during serial preprocessing and is local to this rank.
      // The steady wall profile is a useful nonlinear initializer, but it is
      // not a consistent transient initial condition: around the cylinder it
      // would overwrite a thick annulus with a purely tangential velocity and
      // remove the freestream normal component.  Transient cases therefore
      // start from the freestream (plus the explicit phase seed below) and
      // impose no-slip through the actual wall flux/gradient treatment.
      if(!c.transient && c.initializeWallState && c.mode=="laminar" && c.Re>0.0){
        const double nu0=c.UInf*c.refL/c.Re;
        const double delta=5.0*std::sqrt(std::max(nu0*c.refL/std::max(c.UInf,1.0e-12),1.0e-12));
        const double profile_limit=4.0*delta;
        for(int i:part.owned){
          const Cell& cell=m.cells[i];
          if(!(cell.wall_distance>=0.0) || cell.wall_distance>profile_limit || norm(cell.wall_normal)<0.5) continue;
          Prim w=primitive(q[i],c); const V2 t{-cell.wall_normal.y,cell.wall_normal.x};
          const double ut=c.UInf*std::cos(c.aoa)*t.x+c.UInf*std::sin(c.aoa)*t.y;
          const double profile=std::tanh(cell.wall_distance/delta);
          w.u=ut*profile*t.x; w.v=ut*profile*t.y; q[i]=conservative(w,c);
        }
      } else if(!c.transient && c.initializeWallState) for(int i:part.owned){
        for(int fi:m.cells[i].faces) if(m.faces[fi].b<0){
          const Face& f=m.faces[fi]; const std::string bc=boundary_type(f,c);
          Prim w=primitive(q[i],c); V2 n=f.normal*(1.0/std::max(f.area,kFloor));
          if(bc=="no_slip_adiabatic_wall"){
            w.u=0.0; w.v=0.0;
          } else if(bc=="slip_wall"){
            const double vn=w.u*n.x+w.v*n.y; w.u-=vn*n.x; w.v-=vn*n.y;
          }
          q[i]=conservative(w,c); break;
        }
      }
      // A small localized even-in-y vertical seed breaks the exact reflection
      // symmetry of the freestream/cylinder state and lets the antisymmetric
      // von-Karman mode select a phase.  The Gaussian keeps the perturbation
      // near the body; it is an initial condition, not persistent forcing.
      if(c.transient && c.transientSeed>0.0)for(int i:part.owned){
        const double x=m.cells[i].center.x, y=m.cells[i].center.y;
        if(x>0.0&&x<4.0){Prim w=primitive(q[i],c);w.v+=c.transientSeed*std::exp(-0.25*(x-1.5)*(x-1.5))*std::cos(2.0*y);q[i]=conservative(w,c);}
      }
      // The initialized wall/seed state is the actual U^0 for the first BE
      // interval.  Histories were allocated from freestream before this
      // branch, so refresh them after every fresh-run initialization.
      if (c.transient) { qn=q; qnm1=q; }
    } else if (c.transient && c.transientSeed>0.0 && !restart_has_bdf2_history) {
      // A state-only restart has no stored BDF2 history.  Permit the same
      // bounded phase-selecting perturbation on that reconstructed initial
      // state when explicitly requested, then use it as both BE histories;
      // this is a one-time initial condition, not persistent forcing.
      for(int i:part.owned) {
        const double x=m.cells[i].center.x, y=m.cells[i].center.y;
        if(x>0.0&&x<4.0) {
          Prim w=primitive(q[i],c);
          w.v+=c.transientSeed*std::exp(-0.25*(x-1.5)*(x-1.5))*std::cos(2.0*y);
          q[i]=conservative(w,c);
        }
      }
      qn=q; qnm1=q;
    }
    std::ofstream residual_file, force_file;double wall_start=MPI_Wtime();double initial_norm=0,last_norm=0,last_inner_ratio=1;int final_step=resume_step_override,observed_min=c.maxInner,observed_max=0;long long target_misses=resume_step_override>0?1:0,target_converged=resume_step_override>0?std::max(0,resume_step_override-1):0,inner_sum=0,inner_steps=0,predictor_retries=0,bdf1_fallback_steps=0,be_halfstep_fallback_steps=0,be_halfsteps=0,be_anchor_steps=0,damping_retry_steps=0,damping_retry_attempts=0,be_damping_retry_attempts=0,best_effort_steps=0,matrix_free_rescue_attempts=0,matrix_free_rescue_accepts=0,matrix_free_rescue_nonlinear_iterations=0,matrix_free_rescue_krylov_products=0;double accepted_be_gain_scale=0.0;bool transient_inner_failure=false;int failed_physical_step=0;double failed_inner_ratio=0;std::string start=utc_now();
    if(rank==0){fs::create_directories(out_path);const bool append_history=resume_step_override>0;residual_file.open(out_path/"residuals.csv",append_history?std::ios::app:std::ios::out);force_file.open(out_path/"forces.csv",append_history?std::ios::app:std::ios::out);if(!append_history){residual_file<<"step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";force_file<<"step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";}}
    const int steps=(c.transient && max_steps_override<=0)?static_cast<int>(std::llround(c.finalTime/c.dt)):c.maxSteps;
    // HLLC is substantially less dissipative than the low-Mach Rusanov
    // fallback, but a full reconstruction slope applied to a freestream
    // field is a harsh nonlinear startup.  For an explicitly requested
    // full-slope HLLC transient, use a documented physical-step continuation
    // from 10% to 100% over the first 1000 dt intervals.  Each interval is
    // still solved to the strict total-residual target for the slope active on
    // that interval; once the ramp reaches one, the requested full-slope
    // operator is used for the remainder of the production horizon.  Runs
    // that request a reduced slope, Rusanov, or a steady case are unchanged.
    const bool hllc_slope_schedule=c.transient && c.inviscidFlux=="hllc" &&
      c.transientSlope>=0.999999;
    const double requested_transient_slope=c.transientSlope;
    const double configured_lowmach_cutoff=c.transient?c.transientLowMachCutoff:1.0;
    double physical_time=resume_step_override*c.dt;double previous_outer_norm=0;int plateau_count=0;std::vector<double> force_cl_history, force_cd_history;bool force_be_recovery_step=resume_step_override>0&&!restart_has_bdf2_history;bool no_extrapolation_recovery_step=false;long long be_recovery_steps=0;
    for(int step=resume_step_override+1;step<=steps;++step){
      if(c.transient && force_be_recovery_step) ++be_recovery_steps;
      if(c.transient) c.transientLowMachCutoff=configured_lowmach_cutoff;
      if(hllc_slope_schedule) {
        c.transientSlope=std::min(requested_transient_slope,
                                  0.10+0.0009*static_cast<double>(step));
      }
      double requested_cfl=c.transient?c.cfl0:std::min(c.cflMax,c.cfl0+(c.cflMax-c.cfl0)*step/std::max(1,c.rampSteps)); double cfl=requested_cfl;
      // Honor the supplied inner-iteration allowance and convergence target
      // for every case; diagnostics must not silently alter production runs.
      // The supplied allowance is an upper bound and every exhausted step is
      // recorded as a target miss for transient cases.
      const int inner_limit=c.maxInner;
      ResidualResult first_inner{}, rr{}; double ratio=1; int used_inner=inner_limit; std::vector<U> old_qn=qn;
      // A two-step BE startup supplies a genuinely resolved U^1/U^2 pair
      // before the first BDF2 interval.  This is a standard startup order
      // reduction (the physical histories are still advanced only after a
      // strict inner solve) and is materially more robust than applying the
      // two-step stencil before U^1 has any temporal curvature.
      // Only the first fresh interval is backward Euler.  Once U^1 exists,
      // the next physical step uses the requested BDF2 stencil; a recovery
      // restart may still force one explicit BE interval.
      const bool be_temporal_interval=(step==1 || force_be_recovery_step);
      const std::vector<U> retry_origin=qn;
      // A BDF2 extrapolation is the normal initial guess.  If the nonlinear
      // wake makes that guess leave the basin of the implicit solve, retry the
      // same BDF2 residual from the last accepted state.  The first two
      // attempts change only the starting iterate; the fallback below records
      // an explicitly time-consistent backward-Euler order reduction.
      // The normal path is a BDF2 predictor followed by a no-extrapolation
      // BDF2 retry.  A rare nonlinear wake stall can make both starting
      // iterates miss the inner target even though the physical state is
      // admissible.  The final retry replaces this one nominal interval with
      // a bounded sequence of converged backward-Euler substeps (two
      // half-steps first, escalating to four or eight smaller substeps).
      // Their total duration is exactly c.dt, and an unconverged substep
      // rejects the whole interval.
      // Keep the physical residual and dt fixed while trying progressively
      // smaller pseudo-time corrections.  This is a nonlinear basin retry,
      // not a change to the BDF2 discretization or a relaxation of the inner
      // target.  The final attempt is the explicit BE substep fallback.
      // Give the first physical step the same bounded recovery ladder as
      // later steps.  Its physical residual is backward-Euler by definition,
      // so retrying the pseudo-time gain (or the two-half-step BE fallback)
      // does not alter the requested BDF2 history semantics.
      // A resumed transient has the same nonlinear recovery budget as a
      // fresh run.  In particular, keep the bounded BE substep ladder
      // reachable on the first interval after a state-only restart.  The
      // BDF2 retries include reduced pseudo-time gains before paying for an
      // order-reducing BE interval: a BE recovery commonly succeeds at 0.5x
      // the nominal gain, while the old ladder tried only stronger gains and
      // therefore repeated BE damping at every subsequent wake interval.
      const int fallback_attempt=9;
      // Full-slope reconstruction is active from the first iteration of the
      // final continuation plateau (inner 61 onward).  A bounded slope
      // homotopy is used for both inviscid flux choices: it only selects a
      // basin-friendly nonlinear starting path, while convergence is still
      // checked on the requested slope=1 operator before a step advances.
      constexpr int full_slope_start_iteration=61;
      // HLLC is introduced through a short physical-flux homotopy as well as
      // the reconstruction-slope homotopy.  This is a nonlinear basin aid:
      // the accepted residual is still evaluated with blend=1, and no
      // reduced-flux state can satisfy the strict target below.
      constexpr int full_hllc_start_iteration=61;
      // Step 1 is already a backward-Euler interval, so the same converged
      // BE-substep recovery used later is time-consistent here as well.  The
      // prior one-attempt special case could reject a full-slope startup just
      // above the target even though a smaller, fully converged BE subdivision
      // of exactly the same physical interval was available.
      const int attempts=c.transient?fallback_attempt+1:1;
      int accepted_attempt=-1;
      bool fallback_halfstep_accepted=false;
      bool best_effort_accept=false;
      std::vector<U> best_retry_q;
      double best_retry_ratio=std::numeric_limits<double>::infinity();
      int step_inner_sum=used_inner,step_inner_count=1,step_inner_min=used_inner,step_inner_max=used_inner;
      // If the previous interval already needed BE recovery (including the
      // first interval after a state-only restart), start directly in the
      // time-consistent BE ladder.  Repeating four doomed full-interval BDF2
      // probes before every recovery step only multiplies cost and cannot
      // restore the missing BDF2 history.
      // Step 1 is backward Euler by definition.  Enter its converged BE
      // subdivision ladder directly, rather than spending full-interval
      // BDF2-gain probes that cannot change the startup discretization.
      // A fresh first step is already backward Euler through the temporal
      // residual branch below, so let it use the same normal nonlinear solve
      // as later steps.  In particular this keeps the full-slope homotopy
      // reachable during startup instead of sending every full-slope run
      // straight into the full-slope BE fallback.  Reduced-slope recovery
      // retains the direct BE ladder after a state-only restart or an
      // accepted recovery interval.
      const int first_attempt=(c.transient && force_be_recovery_step && c.transientSlope<0.999999)?fallback_attempt:0;
      for(int attempt=first_attempt;attempt<attempts;++attempt){
        if(attempt==fallback_attempt){
          const std::vector<U> fallback_origin=qn;
          bool fallback_ok=false;
          // A smaller pseudo-time gain or smaller BE substep can recover the
          // same nominal interval when the BDF2 map leaves the nonlinear
          // basin.  Every trial starts from the accepted BDF2 state; no
          // partially converged substep is carried into a later trial.
          // Include much smaller pseudo-time gains than the normal BDF2
          // retries.  A sudden wake/pressure change can leave the two BE
          // half-step map contractive only in a narrow, strongly damped
          // basin; smaller-substep levels provide a time-consistent recovery
          // before rejecting an otherwise recoverable interval.
          const std::array<int,7> fallback_substep_levels{{2,4,8,16,32,64,128}};
          std::array<double,10> fallback_gain_scales{{8.0,4.0,2.0,1.0,0.5,0.25,0.125,0.0625,0.03125,0.015625}};
          // Recovery gains are usually stable across adjacent wake
          // intervals.  Reuse the last accepted gain first so repeated BE
          // recovery does not rescan the whole ladder on every step.
          if(accepted_be_gain_scale>0.0) {
            for(std::size_t i=0;i<fallback_gain_scales.size();++i) {
              if(std::abs(fallback_gain_scales[i]-accepted_be_gain_scale)<1.0e-15) {
                std::swap(fallback_gain_scales[0],fallback_gain_scales[i]);
                break;
              }
            }
          }
          for(std::size_t level=0;level<fallback_substep_levels.size() && !fallback_ok;++level){
            const int fallback_substeps=fallback_substep_levels[level];
            const double fallback_dt=c.dt/static_cast<double>(fallback_substeps);
            for(std::size_t fallback_try=0;fallback_try<fallback_gain_scales.size() && !fallback_ok;++fallback_try){
              const double fallback_gain_scale=fallback_gain_scales[fallback_try];
              q=fallback_origin;
              bool pair_ok=true;
              int fallback_inner_sum=0,fallback_inner_min=inner_limit,fallback_inner_max=0;
              for(int substep=0;substep<fallback_substeps;++substep){
                const std::vector<U> substep_old=q;
                first_inner=ResidualResult{}; rr=ResidualResult{}; ratio=1.0; used_inner=inner_limit; double best_ratio=1.0; int stagnant_iterations=0;
                for(int inner=1;inner<=inner_limit;++inner){
                  rr=assemble(m,part,q,res,grad,c,!c.transient);
                  for(int i:part.owned) res[i]+=(q[i]-substep_old[i])*(m.cells[i].area/fallback_dt);
                rr=residual_stats(part,res);
                if(inner==1){first_inner=rr;if(step==1 || (resume_step_override>0 && step==resume_step_override+1))initial_norm=std::max(rr.l2,kFloor);}else ratio=rr.l2/std::max(first_inner.l2,kFloor);
                if(inner>1){if(ratio<best_ratio*0.995){best_ratio=ratio;stagnant_iterations=0;}else ++stagnant_iterations;}
                if(inner>=c.minInner&&ratio<=c.innerTarget){used_inner=inner;break;}
                // Delay the normal stagnation abort for a BE fallback: its
                // deliberately smaller physical substep and damped gain may
                // need a longer contraction tail.
          if(inner>=300 && ratio>0.5 && stagnant_iterations>=100){used_inner=inner;break;}
                // Match the BE residual's A/dt_h diagonal in the local
                // preconditioner; the normal BDF2 1.5 coefficient is not used.
                  update_owned(m,part,q,res,grad,c,cfl,step,fallback_dt,1.0,fallback_gain_scale,inner);
                }
                // The last relaxation above changes q after its residual was
                // assembled.  Rebuild the same BE operator on the promoted
                // state before deciding whether this substep converged;
                // otherwise a valid final correction can be rejected on a
                // stale pre-update ratio.
                rr=assemble(m,part,q,res,grad,c,!c.transient,true);
                for(int i:part.owned) res[i]+=(q[i]-substep_old[i])*(m.cells[i].area/fallback_dt);
                rr=residual_stats(part,res);
                ratio=rr.l2/std::max(first_inner.l2,kFloor);
                fallback_inner_sum+=used_inner;fallback_inner_min=std::min(fallback_inner_min,used_inner);fallback_inner_max=std::max(fallback_inner_max,used_inner);
                if(ratio>c.innerTarget){pair_ok=false;break;}
              }
              if(pair_ok){
                // A converged BE subdivision is a useful nonlinear basin
                // initializer for the requested BDF2 map.  Try that map from
                // the BE candidate before classifying the interval as an
                // order-reducing physical fallback.  The BE states never
                // advance qn/qnm1; only a freshly assembled strict BDF2
                // residual can leave this branch as a normal accepted step.
                const std::vector<U> be_candidate=q;
                bool bdf2_anchor_ok=false;
                int bdf2_inner_used=0;
                if(c.transient && step>1 && fallback_substeps<=16){
                  // Continue from BE to BDF2 in bounded temporal stages.  A
                  // direct jump changes the physical-time source by 50% and
                  // commonly crosses a limiter kink; the fixed-point root is
                  // unchanged, but the intermediate maps keep the nonlinear
                  // iterate in the contractive BE basin.
                  const std::array<double,4> blends{{0.25,0.50,0.75,1.0}};
                  int stage_iterations=0;
                  double final_ratio=1.0;
                  for(double blend:blends){
                    ResidualResult stage_first{},stage_rr{};
                    double stage_ratio=1.0;
                    const int stage_limit=std::min(inner_limit,200);
                    int stage_used=stage_limit;
                    for(int stage_inner=1;stage_inner<=stage_limit;++stage_inner){
                      stage_rr=assemble(m,part,q,res,grad,c,!c.transient);
                      for(int i:part.owned)
                        res[i]+=transient_time_term(q[i],qn[i],qnm1[i],m.cells[i].area,c.dt,false,blend);
                      stage_rr=residual_stats(part,res);
                      if(stage_inner==1) stage_first=stage_rr;
                      else stage_ratio=stage_rr.l2/std::max(stage_first.l2,kFloor);
                      if(stage_inner>=c.minInner && stage_ratio<=c.innerTarget){
                        stage_used=stage_inner;
                        break;
                      }
                      update_owned(m,part,q,res,grad,c,cfl,step,-1.0,
                                   transient_time_coefficient(false,blend),
                                   fallback_gain_scale,stage_inner);
                    }
                    // Rebuild after the last update, and require the strict
                    // target only on the requested blend=1 operator.
                    stage_rr=assemble(m,part,q,res,grad,c,!c.transient);
                    for(int i:part.owned)
                      res[i]+=transient_time_term(q[i],qn[i],qnm1[i],m.cells[i].area,c.dt,false,blend);
                    stage_rr=residual_stats(part,res);
                    stage_ratio=stage_rr.l2/std::max(stage_first.l2,kFloor);
                    stage_iterations+=stage_used;
                    bdf2_inner_used=stage_iterations;
                    if(blend>=1.0-1.0e-14){
                      final_ratio=stage_ratio;
                      bdf2_anchor_ok=stage_used>=c.minInner &&
                                     std::isfinite(final_ratio) && final_ratio<=c.innerTarget;
                    }
                    if(!std::isfinite(stage_ratio)) break;
                  }
                }
                if(bdf2_anchor_ok){
                  fallback_ok=true;accepted_be_gain_scale=fallback_gain_scale;
                  be_damping_retry_attempts+=static_cast<long long>(fallback_try + level);
                  ++be_anchor_steps;
                  accepted_attempt=0;fallback_halfstep_accepted=false;
                  used_inner=bdf2_inner_used;
                  step_inner_sum=fallback_inner_sum+bdf2_inner_used;
                  step_inner_count=1;
                  step_inner_min=std::min(fallback_inner_min,bdf2_inner_used);
                  step_inner_max=std::max(fallback_inner_max,bdf2_inner_used);
                  if(rank==0) std::cerr<<"transient BE nonlinear anchor accepted: step="<<step
                                       <<" substeps="<<fallback_substeps
                                       <<" gain_scale="<<fallback_gain_scale
                                       <<" bdf2_inner="<<bdf2_inner_used<<"\n";
                } else {
                  // The BDF2 probe is diagnostic-only when it misses the
                  // strict target; retain the converged BE candidate for the
                  // genuine order-reduced fallback below.
                  q=be_candidate;
                  fallback_ok=true;accepted_be_gain_scale=fallback_gain_scale;
                  be_damping_retry_attempts+=static_cast<long long>(fallback_try + level);
                  accepted_attempt=fallback_attempt;fallback_halfstep_accepted=true;
                  bdf1_fallback_steps++;be_halfstep_fallback_steps++;be_halfsteps+=fallback_substeps;
                  step_inner_sum=fallback_inner_sum;step_inner_count=fallback_substeps;
                  step_inner_min=fallback_inner_min;step_inner_max=fallback_inner_max;
                  if(rank==0) std::cerr<<"transient BE fallback accepted: step="<<step
                                       <<" substeps="<<fallback_substeps
                                       <<" gain_scale="<<fallback_gain_scale<<"\n";
                }
              }
            }
          }
          if(!fallback_ok && rank==0){
            // Keep the best admissible state diagnostic-only.  A candidate
            // that misses the requested total-residual target cannot advance
            // the physical histories: promoting it would turn an unresolved
            // nonlinear solve into an apparently accepted BDF2 interval and
            // violate the transient inner-loop contract.
            std::cerr<<"transient BE recovery exhausted; best BDF2 candidate rejected: step="
                     <<step<<" ratio="
                     <<(std::isfinite(best_retry_ratio)?best_retry_ratio:-1.0)<<"\n";
          }
          break;
        }
        const Case& attempt_case=c;
        // Every normal retry must start from the same accepted physical
        // state.  In particular, step 1 has no BDF2 predictor branch, so
        // without this reset a rejected attempt would contaminate the next
        // gain probe with its partially corrected startup field.
        bool resume_best_retry=false;
        if(c.transient && attempt<fallback_attempt) {
          // Match the archived HLLC retry path: every startup retry begins
          // from the same accepted physical state.  A rejected trial is a
          // nonlinear probe, not a partially accepted history update.
          for(int i:part.owned) q[i]=retry_origin[i];
        }
        // When a transient wake leaves the nominal scalar map only mildly
        // contractive, try stronger bounded Richardson gains before paying
        // for the multi-substep BE ladder.  The conservative line search in
        // update_owned still limits every cell update, so these are basin
        // probes rather than unconditional over-relaxation.
        // Try the last accepted BE gain first after a recovery, then a
        // bounded BDF2 ladder.  All attempts assemble the same BDF2 residual;
        // only the pseudo-time Richardson starting gain changes.
        // Match the validated HLLC retry trajectory: the no-predictor retry
        // probes a stronger gain, then the third attempt uses a conservative
        // quarter-gain basin probe before the smaller ladder.
        std::array<double,9> bdf_gain_scales{{1.0,2.0,0.25,0.2,0.15,0.125,0.1,0.0625,0.03125}};
        // Startup is already backward Euler and uses a gentler repeat; the
        // stronger no-predictor probe is reserved for ordinary BDF2 wake
        // intervals, where it is part of the validated recovery trajectory.
        if(step==1) bdf_gain_scales={{1.0,1.0,0.25,0.2,0.15,0.125,0.1,0.0625,0.03125}};
        else bdf_gain_scales={{1.0,2.0,0.25,0.2,0.15,0.125,0.1,0.0625,0.03125}};
        if(accepted_be_gain_scale>0.0) {
          std::array<double,9> ordered{{accepted_be_gain_scale,1.0,0.5,0.25,0.125,0.0625,0.03125,2.0,4.0}};
          // Build a de-duplicated ordering without allocating in the inner
          // loop.  The final slot is retained as an emergency stronger-gain
          // probe when the accepted recovery gain is already present.
          std::array<double,9> base{{1.0,0.5,0.25,0.125,0.0625,0.03125,2.0,4.0,8.0}}, rebuilt{};
          int count=0;
          for(double candidate:ordered) {
            bool duplicate=false;
            for(int k=0;k<count;++k) if(std::abs(rebuilt[static_cast<std::size_t>(k)]-candidate)<1.0e-15) { duplicate=true; break; }
            if(!duplicate && count<static_cast<int>(rebuilt.size())) rebuilt[static_cast<std::size_t>(count++)]=candidate;
          }
          for(double candidate:base) {
            bool duplicate=false;
            for(int k=0;k<count;++k) if(std::abs(rebuilt[static_cast<std::size_t>(k)]-candidate)<1.0e-15) { duplicate=true; break; }
            if(!duplicate && count<static_cast<int>(rebuilt.size())) rebuilt[static_cast<std::size_t>(count++)]=candidate;
          }
          bdf_gain_scales=rebuilt;
        }
        // A partially damped predictor remains in the basin while the full
        // pseudo-time gain is still useful for contracting the BDF2 residual.
        // Do not couple the 0.5 predictor retry to an unnecessarily halved
        // nonlinear gain; lower gains remain available in later retries.
        const double attempt_gain_scale=bdf_gain_scales[static_cast<std::size_t>(attempt)];
        if(c.transient && step>1 && !resume_best_retry){
          for(int i:part.owned){
            // Probe a bounded family of BDF2 starting predictors before
            // reducing the order or entering the BE ladder.  The accepted
            // nonlinear root is unchanged; these 1.0, 0.5, 0.25, and 0.0
            // extrapolation levels only choose a basin-friendly initial
            // iterate after a rapidly changing wake interval.
            // Match the archived successful HLLC path: the second physical
            // interval starts from U^n, and extrapolation begins only after
            // a valid two-step history exists.
            const double predictor_scale=(attempt==0 && step>2 &&
              !force_be_recovery_step && !no_extrapolation_recovery_step) ? 1.0 : 0.0;
            const U base=qn[i], delta=(qn[i]-qnm1[i])*predictor_scale;
            double alpha=1.0;
            auto ok=[&](const U& s){
              if(!std::isfinite(s.rho)||!std::isfinite(s.rhou)||!std::isfinite(s.rhov)||!std::isfinite(s.rhoE)||s.rho<=1.0e-10) return false;
              const double kinetic=0.5*(s.rhou*s.rhou+s.rhov*s.rhov)/s.rho;
              return std::isfinite((c.gamma-1.0)*(s.rhoE-kinetic)) && (c.gamma-1.0)*(s.rhoE-kinetic)>1.0e-10;
            };
            while(alpha>1.0e-8 && !ok(base+delta*alpha)) alpha*=0.5;
            q[i]=ok(base+delta*alpha)?base+delta*alpha:base;
          }
        }
        // A full-slope transient solve is the production spatial scheme.  The
        // global residual line search below keeps each requested-operator
        // correction inside the nonlinear basin, so no hidden first-order
        // continuation is needed before the accepted state is evaluated.
        const bool use_slope_homotopy=c.transient && c.transientSlope>=0.999999;
        const bool use_temporal_homotopy=false;
        // A difficult BDF2 wake interval can start outside the basin of the
        // full two-step map even when the nearby one-step BE map is strongly
        // contractive.  Contract the BE(dt) residual as an internal
        // nonlinear continuation stage before assembling the requested BDF2
        // operator.  This never advances physical history; final acceptance
        // still requires a fresh strict BDF2 residual below.
        if(false && c.transient && step>1 && !be_temporal_interval) {
          constexpr int be_anchor_limit=400;
          ResidualResult anchor_first{},anchor_rr{}; double anchor_ratio=1.0;
          int anchor_inner_used=0;
          for(int anchor_inner=1;anchor_inner<=be_anchor_limit;++anchor_inner) {
            anchor_rr=assemble(m,part,q,res,grad,c,!c.transient);
            for(int i:part.owned)
              res[i]+=(q[i]-qn[i])*(m.cells[i].area/std::max(c.dt,1.0e-12));
            anchor_rr=residual_stats(part,res);
            if(anchor_inner==1) anchor_first=anchor_rr;
            else anchor_ratio=anchor_rr.l2/std::max(anchor_first.l2,kFloor);
            anchor_inner_used=anchor_inner;
            if(anchor_inner>=c.minInner && anchor_ratio<=c.innerTarget) break;
            update_owned(m,part,q,res,grad,c,cfl,step,c.dt,1.0,
                         attempt_gain_scale,anchor_inner);
          }
          anchor_rr=assemble(m,part,q,res,grad,c,!c.transient);
          for(int i:part.owned)
            res[i]+=(q[i]-qn[i])*(m.cells[i].area/std::max(c.dt,1.0e-12));
          anchor_rr=residual_stats(part,res);
          anchor_ratio=anchor_rr.l2/std::max(anchor_first.l2,kFloor);
          if(rank==0 && step<=3)
            std::cerr<<"transient BE anchor: step="<<step
                     <<" attempt="<<attempt<<" inner="<<anchor_inner_used
                     <<" ratio="<<anchor_ratio<<"\n";
        }
        // The archived strict HLLC gate used exact HLLC throughout.  Keep
        // the continuation machinery available for future explicit use, but
        // do not insert an implicit Rusanov-to-HLLC path into this exact-HLLC
        // solve.
        const bool use_hllc_homotopy=false;
        ResidualResult target_reference{};
        if(use_slope_homotopy || use_temporal_homotopy || use_hllc_homotopy){
          Case reference_case=attempt_case; reference_case.transientSlope=1.0;
          reference_case.hllcFluxBlend=1.0;
          target_reference=assemble(m,part,q,res,grad,reference_case,!c.transient);
          for(int i:part.owned){
            res[i]+=transient_time_term(q[i],qn[i],qnm1[i],m.cells[i].area,c.dt,
                                        be_temporal_interval,1.0);
          }
          target_reference=residual_stats(part,res);
        }
        first_inner=ResidualResult{}; rr=ResidualResult{}; ratio=1.0; used_inner=inner_limit; double best_ratio=1.0; int stagnant_iterations=0;
        // A full-slope correction may be locally admissible while increasing
        // the nonlinear BDF2 residual because the frozen scalar/block
        // preconditioner is only an approximation to the limited residual.
        // Cache an accepted trial residual so its (otherwise mandatory) next
        // assembly is not repeated.  The safeguard is intentionally limited
        // to the requested full-slope operator: the earlier homotopy stages
        // retain their existing inexpensive continuation iterations.
        bool cached_full_slope_residual=false;
        ResidualResult cached_full_slope_rr{};
        for(int inner=1;inner<=inner_limit;++inner){
          Case inner_case=attempt_case;
          if(use_slope_homotopy){
            // Hold each continuation operator stationary for a block of
            // corrections.  A linearly changing operator has no chance to
            // contract to any intermediate fixed point; the final plateau
            // deliberately leaves forty iterations for the requested
            // full-slope residual itself.
            if(inner<=15) inner_case.transientSlope=0.10;
            else if(inner<=30) inner_case.transientSlope=0.25;
            else if(inner<=45) inner_case.transientSlope=0.50;
            else if(inner<=60) inner_case.transientSlope=0.75;
            else inner_case.transientSlope=1.0;
          }
          if(use_hllc_homotopy){
            if(inner<=15) inner_case.hllcFluxBlend=0.0;
            else if(inner<=30) inner_case.hllcFluxBlend=0.25;
            else if(inner<=45) inner_case.hllcFluxBlend=0.50;
            else if(inner<=60) inner_case.hllcFluxBlend=0.75;
            else inner_case.hllcFluxBlend=1.0;
          }
          const double temporal_blend=1.0;
          const bool full_slope_operator=c.transient && inner_case.transientSlope>=0.999999;
          const bool full_hllc_operator=!use_hllc_homotopy ||
            inner_case.hllcFluxBlend>=0.999999;
          const double temporal_coefficient=transient_time_coefficient(
            be_temporal_interval,temporal_blend);
          if(cached_full_slope_residual && full_slope_operator) {
            rr=cached_full_slope_rr;
            cached_full_slope_residual=false;
          } else {
            cached_full_slope_residual=false;
            // Rebuild the least-squares/limiter gradients on every nonlinear
            // correction so the strict residual is the actual requested
            // operator.  `assemble` caches primitive conversions within this
            // call, avoiding repeated EOS work without freezing any state.
            const bool refresh_inner_gradients = true;
            rr=assemble(m,part,q,res,grad,inner_case,!c.transient,
                        refresh_inner_gradients);
            if(c.transient){
              for(int i:part.owned) res[i]+=transient_time_term(
                q[i],qn[i],qnm1[i],m.cells[i].area,c.dt,
                be_temporal_interval,temporal_blend);
              rr=residual_stats(part,res);
            }
          }
          if(inner==1){first_inner=(use_slope_homotopy||use_temporal_homotopy||use_hllc_homotopy)?target_reference:rr;if(step==1 || (resume_step_override>0 && step==resume_step_override+1))initial_norm=std::max(first_inner.l2,kFloor);}else ratio=rr.l2/std::max(first_inner.l2,kFloor);
          const bool slope_ready=(!use_slope_homotopy ||
            (inner>=full_slope_start_iteration && inner>=full_hllc_start_iteration)) &&
            (!use_hllc_homotopy || inner>=full_hllc_start_iteration) &&
            (!use_temporal_homotopy || temporal_blend>=1.0-1.0e-14);
          // The residual operator changes throughout the continuation.  Do
          // not carry stagnation history across that change: a residual that
          // contracts for the 10% operator can rise as the requested slope
          // is introduced without indicating a failed full-slope solve.
          if((use_slope_homotopy || use_hllc_homotopy) &&
             (inner==16 || inner==31 || inner==46 || inner==61)){
            best_ratio=ratio; stagnant_iterations=0;
          } else if(inner>1 && slope_ready) {
            if(ratio<best_ratio*0.995){best_ratio=ratio;stagnant_iterations=0;}else ++stagnant_iterations;
          }
          if(inner>=c.minInner&&slope_ready&&ratio<=c.innerTarget){used_inner=inner;break;}
          if(step>1 && inner>=100 && slope_ready && ratio>0.5 && stagnant_iterations>=40){used_inner=inner;break;}
          // Match the temporal Jacobian used by the assembled residual.  A
          // startup or post-recovery interval is backward Euler (coefficient
          // 1), while ordinary intervals use BDF2 (coefficient 1.5).
          // Globalize every steady correction and the requested full-slope
          // transient correction.  In particular, the steady CFL continuation
          // otherwise has only a local admissibility check: on the supplied
          // low-Mach meshes it can pass that check while increasing the global
          // nonlinear residual once the CFL ramp reaches its aggressive
          // portion.  The local update retains its conservative
          // positivity/correction caps.  A convex interpolation between its
          // admissible endpoint and this iterate is also admissible; a common
          // MPI-reduced norm makes every rank select the same step length.
          bool no_descent=false;
          // Apply the same requested-operator Armijo safeguard to every
          // transient retry.  A retry changes only the starting predictor or
          // pseudo-time gain; accepting an unglobalized correction there can
          // leave the trial outside the BDF2 residual basin and force an
          // unnecessarily expensive BE subdivision ladder.
          // The first full-slope trial is globally safeguarded.  Later
          // bounded retries deliberately start from the same accepted
          // physical state with a different predictor/gain; repeating five
          // global residual assemblies for every retry can dominate the
          // entire BE recovery ladder without changing the accepted root.
          // Their local admissibility caps remain active, and the strict
          // terminal residual is still checked after every trial.
          const bool globalize_correction=!c.transient ||
            (full_slope_operator && full_hllc_operator && c.transientLineSearch &&
             !(step>1 && attempt>0));
          if(globalize_correction) {
            const std::vector<U> q_base=q;
            const std::vector<U> res_base=res;
            const std::vector<Grad> grad_base=grad;
            const ResidualResult base_rr=rr;
            const double base_l2=rr.l2;
            update_owned(m,part,q,res,grad,inner_case,cfl,step,
                         be_temporal_interval?c.dt:-1.0,
                         temporal_coefficient,
                         attempt_gain_scale,inner);
            const std::vector<U> q_full=q;
            bool accepted=false;
            for(double alpha : std::array<double,5>{{1.0,0.5,0.25,0.125,0.0625}}) {
              for(std::size_t i=0;i<q.size();++i) q[i]=q_base[i]+(q_full[i]-q_base[i])*alpha;
              ResidualResult trial=assemble(m,part,q,res,grad,inner_case,!c.transient);
              if(c.transient){
                for(int i:part.owned) res[i]+=transient_time_term(
                  q[i],qn[i],qnm1[i],m.cells[i].area,c.dt,
                  be_temporal_interval,temporal_blend);
                trial=residual_stats(part,res);
              }
              const double armijo=(1.0-1.0e-4*alpha)*base_l2;
              // Permit a small non-monotone residual increase while the
              // limiter/face branches change.  A strict Armijo test can stop
              // the HLLC continuation at a shallow kink a few times 1e-3
              // above the current norm; the physical step is still accepted
              // only after the final requested residual reaches 1e-3.
              if(std::isfinite(trial.l2) && trial.l2<=armijo) {
                rr=trial;
                cached_full_slope_rr=trial;
                cached_full_slope_residual=true;
                accepted=true;
                break;
              }
            }
            if(!accepted) {
              q=q_base;
              res=res_base;
              grad=grad_base;
              rr=base_rr;
              // The next nonlinear iteration would start from the identical
              // state and construct the same frozen correction.  Do not burn
              // the remaining inner allowance re-evaluating the same failed
              // global line search; the strict retry/fallback acceptance
              // logic below still decides whether the physical step advances.
              no_descent=true;
            }
          } else {
            update_owned(m,part,q,res,grad,inner_case,cfl,step,
                         be_temporal_interval?c.dt:-1.0,
                         temporal_coefficient,
                         attempt_gain_scale,inner);
          }
          if(no_descent) { used_inner=inner; break; }
        }
        // If the final inner iteration applied a correction, the residual
        // used above describes the previous iterate.  Reassemble the exact
        // operator that would be accepted.  For a full-slope continuation,
        // this deliberately evaluates the requested slope=1 operator even
        // when the iteration allowance was exhausted on its last update.
        Case final_case=attempt_case;
        if(use_slope_homotopy) final_case.transientSlope=1.0;
        if(use_hllc_homotopy) final_case.hllcFluxBlend=1.0;
        rr=assemble(m,part,q,res,grad,final_case,!c.transient);
        if(c.transient){
          for(int i:part.owned) res[i]+=transient_time_term(
            q[i],qn[i],qnm1[i],m.cells[i].area,c.dt,
            be_temporal_interval,1.0);
          rr=residual_stats(part,res);
        }
        ratio=rr.l2/std::max(first_inner.l2,kFloor);
        if(rank==0 && c.transient && step<=3)
          std::cerr<<"transient attempt diagnostic: step="<<step
                   <<" attempt="<<attempt<<" gain="<<attempt_gain_scale<<" inner="<<used_inner
                   <<" ratio="<<ratio<<" first="<<first_inner.l2<<"\n";
        // HLLC materially changes the nonlinear residual Jacobian compared
        // with the scalar Rusanov map.  If the first bounded Richardson
        // attempt stalls, use a strictly optional matrix-free Newton rescue
        // on the exact reconstructed/viscous/physical-time residual.  The
        // rescue is deliberately limited to the explicit HLLC experiment;
        // the production-default Rusanov path remains byte-for-byte on its
        // existing retry/fallback ladder.
        if(c.transient && c.inviscidFlux=="hllc" && c.transientSlope>=0.999999 && attempt==0 &&
           (!std::isfinite(ratio) || ratio>c.innerTarget)) {
          ++matrix_free_rescue_attempts;
          // Start from the best state reached by the bounded nonlinear
          // iteration.  Rebasing the rescue all the way to U^n discards the
          // useful contraction already obtained before a line-search kink.
          // `rr` and `res` already describe this requested BDF2 operator on
          // the current state, so the matrix-free residual can begin there.
          const MatrixFreeRescueResult rescue=matrix_free_transient_rescue(
              m,part,q,qn,qnm1,res,grad,c,be_temporal_interval);
          matrix_free_rescue_nonlinear_iterations+=rescue.nonlinear_iterations;
          matrix_free_rescue_krylov_products+=rescue.krylov_products;
          if(rescue.accepted) {
            ++matrix_free_rescue_accepts;
            used_inner=std::min(inner_limit,used_inner+rescue.nonlinear_iterations);
            rr=assemble(m,part,q,res,grad,c,!c.transient);
            if(c.transient) {
              for(int i:part.owned) res[i]+=transient_time_term(
                q[i],qn[i],qnm1[i],m.cells[i].area,c.dt,
                be_temporal_interval,1.0);
              rr=residual_stats(part,res);
            }
            ratio=rr.l2/std::max(first_inner.l2,kFloor);
          }
        }
        const bool slope_target_reached=(!use_slope_homotopy ||
          (used_inner>=full_slope_start_iteration && used_inner>=full_hllc_start_iteration)) &&
          (!use_hllc_homotopy || used_inner>=full_hllc_start_iteration);
        // A partially ramped reconstruction is not a valid full-slope retry
        // state.  Keep best-effort continuation candidates only after the
        // requested operator has been reached.
        if(slope_target_reached && std::isfinite(ratio) && ratio<best_retry_ratio){
          // `ratio` is evaluated before the final correction update.  Keep a
          // candidate only after reassembling the same full-slope operator on
          // the state that would actually be promoted by best-effort resume.
          Case candidate_case=attempt_case;
          candidate_case.transientSlope=use_slope_homotopy?1.0:attempt_case.transientSlope;
          if(use_hllc_homotopy) candidate_case.hllcFluxBlend=1.0;
          ResidualResult candidate_rr=assemble(m,part,q,res,grad,candidate_case,!c.transient);
          if(c.transient){
            for(int i:part.owned) res[i]+=transient_time_term(
              q[i],qn[i],qnm1[i],m.cells[i].area,c.dt,
              be_temporal_interval,1.0);
            candidate_rr=residual_stats(part,res);
          }
          const double candidate_ratio=candidate_rr.l2/std::max(first_inner.l2,kFloor);
          if(std::isfinite(candidate_ratio) && candidate_ratio<best_retry_ratio){best_retry_ratio=candidate_ratio;best_retry_q=q;}
        }
        if(!c.transient || (ratio<=c.innerTarget && slope_target_reached)){accepted_attempt=attempt;break;}
      }
      if(resume_step_override>0 && accepted_attempt<0 && !best_retry_q.empty() && rank==0){
        // State-only restarts use the same strict acceptance rule as fresh
        // runs.  Retain the candidate for diagnostics, but never promote an
        // unconverged interval into the restarted BDF2 history.
        std::cerr<<"transient restart candidate rejected: step="<<step
                 <<" ratio="<<best_retry_ratio<<"\n";
      }
      if(accepted_attempt>0 && accepted_attempt<fallback_attempt){ predictor_retries++; if(accepted_attempt>=2){damping_retry_steps++;damping_retry_attempts+=accepted_attempt-1;} }
      if(accepted_attempt==fallback_attempt){damping_retry_steps++;damping_retry_attempts+=2;}
      if(!fallback_halfstep_accepted){step_inner_sum=used_inner;step_inner_count=1;step_inner_min=used_inner;step_inner_max=used_inner;}
      // If the inner allowance was exhausted, the final iteration above has
      // already applied one correction after its last residual assembly.
      // Re-assemble so residuals, gradients, and forces describe the same
      // state that is gathered for final output.
      const bool accepted_hllc_target=c.transient && c.inviscidFlux=="hllc" &&
        c.hllcFluxBlend>=0.999999;
      const bool accepted_slope_target=!c.transient || c.transientSlope<0.999999 ||
        (used_inner>=full_slope_start_iteration &&
         (!accepted_hllc_target || used_inner>=full_hllc_start_iteration));
      if((ratio>c.innerTarget || !accepted_slope_target) && !fallback_halfstep_accepted){
        rr=assemble(m,part,q,res,grad,c,!c.transient);
        if(c.transient){
          for(int i:part.owned) res[i]+=transient_time_term(
            q[i],qn[i],qnm1[i],m.cells[i].area,c.dt,
            be_temporal_interval,1.0);
          rr=residual_stats(part,res);
        }
        ratio=rr.l2/std::max(first_inner.l2,kFloor);
      }
      inner_sum+=step_inner_sum;inner_steps+=step_inner_count;
      observed_min=std::min(observed_min,step_inner_min);observed_max=std::max(observed_max,step_inner_max);last_inner_ratio=std::isfinite(ratio)?std::max(ratio,1e-12):1.0e300;
      if(c.transient){
        if((accepted_attempt<0 || !std::isfinite(ratio) || ratio>c.innerTarget) && !best_effort_accept){
          // A BDF2 history is valid only after the nonlinear solve for this
          // physical step has been accepted.  Do not advance U^n/U^{n-1} or
          // write a force/residual row for an unconverged trial state.
          target_misses++; transient_inner_failure=true;
          failed_physical_step=step; failed_inner_ratio=std::isfinite(ratio)?ratio:1.0e300;
          q=qn; // final field/restart remain the last accepted physical state
          if(final_step==0) last_norm=first_inner.l2;
          break;
        }
        if(best_effort_accept){target_misses++;best_effort_steps++;} else target_converged++;
        physical_time=step*c.dt;qnm1=old_qn;qn=q;final_step=step;
        // The accepted BE substeps already advance this interval and leave a
        // valid pair of physical histories for the next BDF2 solve.  Do not
        // force an additional full backward-Euler interval: that redundant
        // order reduction damps the wake after every isolated recovery.  The
        // next interval may therefore use the ordinary BDF2 extrapolated
        // starting iterate.  Only a state-only restart needs the explicit
        // force_be_recovery_step flag above to reconstruct its first history.
        // In particular, a recovered BE interval must not disable the BDF2
        // predictor on the following step: qnm1=old_qn and qn=q are a valid
        // history pair, and suppressing the predictor can cause cascading
        // fallback intervals in an otherwise contractive wake solve.
        no_extrapolation_recovery_step=false;
        force_be_recovery_step=false;
      }
      else final_step=step;
      last_norm=rr.l2;double global_comp[4];for(int k=0;k<4;++k)global_comp[k]=rr.mean[k];Forces f=local_forces(m,part,q,grad,c);
      if(rank==0){residual_file<<step<<","<<std::setprecision(16)<<physical_time<<","<<used_inner<<","<<cfl<<","<<(c.transient?c.dt:0.0)<<","<<global_comp[0]<<","<<global_comp[1]<<","<<global_comp[2]<<","<<global_comp[3]<<","<<rr.l2<<","<<rr.linf<<"\n";force_file<<step<<","<<physical_time<<","<<f.cl<<","<<f.cd<<","<<f.cmz<<","<<f.pd<<","<<f.vd<<","<<f.pl<<","<<f.vl<<"\n";force_cl_history.push_back(f.cl);force_cd_history.push_back(f.cd);}
      // The case control specifies the requested outer residual reduction.
      // Do not silently cap it at three orders: doing so can terminate a
      // 4--5 order production case before its requested convergence test.
      if(!c.transient && step>100 && initial_norm>0 && rr.l2/initial_norm<std::pow(10.0,-c.outerTarget))break;
      if(!c.transient && step>200 && previous_outer_norm>0.0){
        const double rel_change=std::abs(rr.l2-previous_outer_norm)/std::max(previous_outer_norm,kFloor);
        plateau_count=(rel_change<5.0e-3)?plateau_count+1:0;
        // A slowly changing residual is not sufficient evidence for a
        // steady stop: viscous boundary layers can still be moving while
        // the global norm appears flat.  Permit a plateau stop only after
        // the terminal drag window is stable as well.  This is the
        // benchmark's documented alternative when the requested residual
        // target is too aggressive for the scalar relaxation.
        // Force history is written only on rank zero.  Broadcast the
        // resulting stop decision so every MPI rank exits the outer loop
        // together; otherwise rank zero could break while workers continue
        // into the next halo exchange.
        int plateau_stop=0;
        if(c.steadyPlateauStop && rank==0 && force_cd_history.size()>=100){
          const auto first=force_cd_history.end()-100;
          const double cd_min=*std::min_element(first,force_cd_history.end());
          const double cd_max=*std::max_element(first,force_cd_history.end());
          const double cd_mean=std::accumulate(first,force_cd_history.end(),0.0)/100.0;
          const bool force_stable=(cd_max-cd_min)<=std::max(0.10*std::abs(cd_mean),1.0e-3);
          plateau_stop=(plateau_count>=100 && rr.l2<=initial_norm && force_stable)?1:0;
        }
        MPI_Bcast(&plateau_stop,1,MPI_INT,0,MPI_COMM_WORLD);
        if(plateau_stop)break;
        if(step>500 && initial_norm>0.0 && rr.l2>10.0*initial_norm)break;
      }
      previous_outer_norm=rr.l2;
    }
    c.transientLowMachCutoff=configured_lowmach_cutoff;
    if(rank==0){residual_file.close();force_file.close();}
    std::vector<U> qglobal=gather_state(local.global_ids,part,q,rank,nr,global_cells);
    std::vector<U> qnm1_global;
    if(c.transient) qnm1_global=gather_state(local.global_ids,part,qnm1,rank,nr,global_cells);
    long long local_min=observed_min,local_max=observed_max,local_sum=inner_sum,local_count=inner_steps,global_min=0,global_max=0,global_sum=0,global_count=0;
    MPI_Reduce(&local_min,&global_min,1,MPI_LONG_LONG,MPI_MIN,0,MPI_COMM_WORLD);
    MPI_Reduce(&local_max,&global_max,1,MPI_LONG_LONG,MPI_MAX,0,MPI_COMM_WORLD);
    MPI_Reduce(&local_sum,&global_sum,1,MPI_LONG_LONG,MPI_SUM,0,MPI_COMM_WORLD);
    MPI_Reduce(&local_count,&global_count,1,MPI_LONG_LONG,MPI_SUM,0,MPI_COMM_WORLD);
    if(rank==0){global_mesh=read_cgns(mesh_path);if(global_part.owner.size()!=global_mesh.cells.size())throw std::runtime_error("final mesh reload differs from partitioned mesh");fs::create_directories(out_path);write_surface(out_path/"surface.csv",global_mesh,qglobal,c);write_vtk(out_path/"field_final.vtk",global_mesh,qglobal,global_part.owner,c);write_partition_diag(out_path/"partition_diagnostics.csv",global_mesh,global_part.owner,nr);std::ofstream rst(out_path/"restart_final.dat");for(auto&u:qglobal)rst<<std::setprecision(17)<<u.rho<<" "<<u.rhou<<" "<<u.rhov<<" "<<u.rhoE<<"\n";rst.close();
      if(c.transient) {
        std::ofstream history(out_path/"restart_final.history.dat");
        for(const U& u:qnm1_global) history<<std::setprecision(17)<<u.rho<<" "<<u.rhou<<" "<<u.rhov<<" "<<u.rhoE<<"\n";
      }
      double converged_fraction=(target_converged+target_misses)>0?static_cast<double>(target_converged)/(target_converged+target_misses):1.0;
      int edge_cut=0;for(std::size_t i=0;i<global_mesh.cells.size();++i)for(int j:global_mesh.cells[i].nbr)if(global_part.owner[i]!=global_part.owner[j])++edge_cut;edge_cut/=2;
      double observed_mean=global_count>0?static_cast<double>(global_sum)/static_cast<double>(global_count):0.0;
      json meta={{"case_id",c.id},{"solver_name","Aurora-FV"},{"solver_version","1.2.0"},{"git_revision",nullptr},{"mpi_ranks",nr},{"mesh_file",mesh_path.string()},{"num_cells_global",global_mesh.cells.size()},{"num_faces_global",global_mesh.faces.size()},{"num_cells_owned_local",part.owned.size()},{"num_cells_ghost_local",part.ghosts.size()},{"partitioner","METIS_PartGraphKway"},{"partition_edge_cut",edge_cut},{"halo_exchange","neighbor_isend_irecv"},{"full_state_replication_during_iterations",false},{"full_mesh_replication_during_iterations",false},{"equation_set","compressible_navier_stokes_2d"},{"inviscid_flux","Rusanov local Lax-Friedrichs"},{"entropy_fix",nullptr},{"viscous_flux",c.mode=="laminar"?"Newtonian stress plus Fourier heat flux":"disabled"},{"time_integrator",c.transient?"BDF2 dual-time with frozen histories; bounded BE substep fallback":"implicit pseudo-time local CFL"},{"implicit_solver","block-Jacobi nonlinear relaxation with local convective/viscous spectral-radius Jacobian"},{"reconstruction","least-squares piecewise-linear primitive gradients with exchanged ghost gradients"},{"limiter","Barth-Jespersen positivity limiter"},{"spatial_order_claimed",2},{"positivity_preservation","density/pressure floors with bounded conservative updates"},{"wall_boundary_output_semantics","boundary_value"},{"true_bdf2_inner_loop",c.transient},{"typical_inner_iterations",observed_mean},{"min_inner_iterations",c.minInner},{"max_inner_iterations",c.maxInner},{"observed_min_inner_iterations",global_min},{"observed_mean_inner_iterations",observed_mean},{"observed_max_inner_iterations",global_max},{"inner_residual_reduction_target",c.innerTarget},{"inner_target_misses",target_misses},{"inner_target_converged_fraction",c.transient?converged_fraction:1.0},{"last_inner_residual_ratio",last_inner_ratio},{"rank_local_partition_cache",true},{"requested_max_steps",c.maxSteps},{"actual_steps",final_step},{"requested_cfl_initial",c.cfl0},{"requested_cfl_max",c.cflMax},{"requested_cfl_ramp_steps",c.rampSteps},{"start_time_utc",start},{"end_time_utc",utc_now()},{"completed",true},{"convergence_status",c.transient?"statistically_periodic":"converged"}};std::ofstream mo(out_path/"metadata.json");mo<<meta.dump(2)<<"\n";std::ostringstream command;command<<"mpirun -np "<<nr<<" aurora-fv solve --case "<<case_path.string()<<" --output "<<out_path.string();
      if(!restart_path.empty()) command<<" --restart "<<restart_path.string();
      if(resume_step_override>0) command<<" --resume-step "<<resume_step_override;
      if(max_steps_override>0) command<<" --max-steps "<<max_steps_override;
      if(cfl_max_override>0.0) command<<" --cfl-max "<<cfl_max_override;
      if(steady_omega_override>0.0) command<<" --steady-omega "<<steady_omega_override;
      if(steady_block_coupling_override>=0.0) command<<" --steady-block-coupling "<<steady_block_coupling_override;
      if(steady_plateau_stop) command<<" --steady-plateau-stop";
      if(transient_slope_override>=0.0) command<<" --transient-slope "<<transient_slope_override;
      if(transient_gain_override>0.0) command<<" --transient-gain "<<transient_gain_override;
      if(transient_seed_override>=0.0) command<<" --transient-seed "<<transient_seed_override;
      if(transient_cutoff_override>0.0) command<<" --transient-cutoff "<<transient_cutoff_override;
      if(transient_block_coupling_override>=0.0) command<<" --transient-block-coupling "<<transient_block_coupling_override;
      if(inner_target_override>0.0) command<<" --inner-target "<<inner_target_override;
      if(rusanov_scale_override>0.0) command<<" --rusanov-scale "<<rusanov_scale_override;
      if(!inviscid_flux_override.empty()) command<<" --inviscid-flux "<<inviscid_flux_override;
      if(transient_linesearch_seen) command<<" --transient-linesearch "<<(transient_linesearch_override?"on":"off");
      if(no_wall_init) command<<" --no-wall-init";
      json status={{"case_id",c.id},{"command",command.str()},{"mpi_ranks",nr},{"wall_time_seconds",MPI_Wtime()-wall_start},{"final_step",final_step},{"final_physical_time",physical_time},{"convergence_status",c.transient?"statistically_periodic":"converged"},{"residual_reduction_orders",std::log10(std::max(initial_norm,kFloor)/std::max(last_norm,kFloor))},{"notes",c.transient?"nominal BDF2 physical steps with documented converged BE substeps replacing rare rejected intervals; fresh full residual assembly at every inner iteration and frozen histories":(!c.transient && c.steadyBlockCoupling>0.0?"steady pseudo-time run used the supplied CFL continuation and repeated frozen 4x4 block-Jacobi iterations; terminal behavior is assessed in report":"steady pseudo-time run used the supplied CFL continuation and repeated scalar Jacobi/Richardson iterations; terminal behavior is assessed in report")}};std::ofstream so(out_path/"run_status.json");so<<status.dump(2)<<"\n";std::ofstream log(out_path/"stdout.log");log<<"Aurora-FV completed case "<<c.id<<" on "<<nr<<" MPI ranks\n";log<<"cells="<<global_mesh.cells.size()<<" faces="<<global_mesh.faces.size()<<" final_step="<<final_step<<" physical_time="<<physical_time<<"\n";}
    if(rank==0){
      // Keep the emitted description aligned with the selected transient
      // correction while preserving the scalar production default.
      std::ifstream metadata_in(out_path/"metadata.json");
      json metadata; metadata_in >> metadata;
      metadata["solver_version"]="1.3.0";
      metadata["inviscid_flux"]=(c.inviscidFlux=="hllc"?"HLLC with per-face Rusanov fallback":(c.transient?"all-speed low-Mach-preconditioned Rusanov local Lax-Friedrichs":"Rusanov local Lax-Friedrichs"));
      if(c.transient) metadata["time_integrator"]="BDF2 dual-time with frozen histories; bounded BE substep fallback (2, 4, 8, 16, 32, 64, or 128 substeps) and bounded pseudo-time damping retries";
      metadata["rusanov_dissipation_scale"]=c.rusanovScale;
      metadata["steady_wall_profile_initialization"]=(!c.transient && c.mode=="laminar" && c.initializeWallState && restart_path.empty());
      // Fresh transient runs intentionally start from freestream plus the
      // one-time phase seed.  The wall profile initializer is restricted to
      // steady laminar cases; no transient wall-adjacent cells are projected
      // to a stationary profile before the physical-time solve.
      metadata["transient_wall_profile_initialization"]=false;
      metadata["transient_wall_start_stationary"]=false;
      metadata["implicit_solver"]=(c.transient && c.transientBlockCoupling>0.0)
        ? "scaled conservative 4x4 frozen local-Rusanov block-Jacobi with spectral-radius temporal/viscous base"
        : (!c.transient && c.steadyBlockCoupling>0.0
          ? "scaled conservative 4x4 frozen local-Rusanov block-Jacobi with scalar pseudo-time/viscous spectral-radius base"
          : "scalar Jacobi/Richardson relaxation with local convective/viscous/temporal spectral-radius diagonal");
      metadata["transient_reconstruction_slope_factor"]=c.transient?c.transientSlope:1.0;
      metadata["transient_reconstruction_slope_requested"]=c.transient?requested_transient_slope:1.0;
      metadata["transient_reconstruction_slope_schedule"] = hllc_slope_schedule
        ? "HLLC physical-step continuation: min(requested_slope, 0.10 + 0.0009*step), reaching full slope at step 1000; each interval uses its active slope and strict total-residual acceptance"
        : "none";
      metadata["transient_reconstruction_slope_schedule_min"] = hllc_slope_schedule?0.10:(c.transient?c.transientSlope:1.0);
      metadata["transient_low_mach_acoustic_cutoff"]=c.transient?c.transientLowMachCutoff:1.0;
      metadata["transient_scalar_correction_gain"]=c.transient?c.transientGain:1.0;
      metadata["transient_block_coupling"]=c.transient?c.transientBlockCoupling:0.0;
      metadata["transient_startup_seed"]=c.transient?c.transientSeed:0.0;
      metadata["transient_global_residual_linesearch"]=c.transient?c.transientLineSearch:true;
      metadata["primitive_state_cache"]="per-residual assembly; shared by gradients and reconstructed face states";
      metadata["transient_inner_iteration_safety_cap"]=0;
      metadata["transient_retry_stagnation_guard"]="abort a clearly stagnant trial after 100 iterations with ratio > 0.5 and 40 stagnant updates; no state is accepted";
      metadata["transient_predictor_recovery"]="fixed BDF2 extrapolation, bounded pseudo-time damping retries, then converged backward-Euler substeps (2, 4, 8, 16, 32, 64, or 128 over one nominal dt) when needed; the last accepted BE gain is reused first";
      metadata["transient_predictor_recovery_steps"]=predictor_retries;
      metadata["transient_bdf1_fallback_steps"]=bdf1_fallback_steps;
      metadata["transient_be_nonlinear_anchor_steps"]=be_anchor_steps;
      metadata["transient_be_halfstep_fallback_steps"]=be_halfstep_fallback_steps;
      metadata["transient_be_halfsteps"]=be_halfsteps;
      metadata["transient_be_substeps"]=be_halfsteps;
      metadata["transient_be_recovery_steps"]=be_recovery_steps;
      metadata["transient_damping_retry_steps"]=damping_retry_steps;
      metadata["transient_damping_retry_attempts"]=damping_retry_attempts;
      metadata["transient_be_damping_retry_attempts"]=be_damping_retry_attempts;
      metadata["transient_be_accepted_gain_scale"]=accepted_be_gain_scale;
      metadata["transient_best_effort_continuation_steps"]=best_effort_steps;
      metadata["matrix_free_rescue_attempts"]=matrix_free_rescue_attempts;
      metadata["matrix_free_rescue_accepts"]=matrix_free_rescue_accepts;
      metadata["matrix_free_rescue_nonlinear_iterations"]=matrix_free_rescue_nonlinear_iterations;
      metadata["matrix_free_rescue_krylov_products"]=matrix_free_rescue_krylov_products;
      metadata["transient_inner_failure"]=transient_inner_failure;
      metadata["failed_physical_step"]=transient_inner_failure?failed_physical_step:0;
      metadata["failed_inner_residual_ratio"]=transient_inner_failure?failed_inner_ratio:0.0;
      metadata["steady_relaxation_omega"]=c.steadyOmega;
      metadata["steady_block_coupling"]=c.steadyBlockCoupling;
      metadata["steady_plateau_stop_enabled"]=c.steadyPlateauStop;
      metadata["effective_steady_cfl_cap"]=c.cflMax;
      metadata["restart_used"]=!restart_path.empty();
      metadata["resume_step"]=resume_step_override;
      metadata["restart_bdf2_history_restored"]=restart_has_bdf2_history;
      metadata["wall_compatible_initialization"]=(!c.transient && c.initializeWallState && restart_path.empty());
      std::string terminal_status="failed", terminal_behavior="unresolved";
      const double final_converged_fraction=(target_converged+target_misses)>0?static_cast<double>(target_converged)/(target_converged+target_misses):1.0;
      if (c.transient) {
        const std::size_t begin=force_cl_history.size()>100?force_cl_history.size()-100:force_cl_history.size()/2;
        double amp=0.0; if(begin<force_cl_history.size()) amp=*std::max_element(force_cl_history.begin()+static_cast<std::ptrdiff_t>(begin),force_cl_history.end())-*std::min_element(force_cl_history.begin()+static_cast<std::ptrdiff_t>(begin),force_cl_history.end());
        // A startup transient can have a nonzero lift range without being a
        // statistically periodic wake.  Require a meaningful late-time
        // window and completion of the requested physical interval before
        // making that claim.
        const bool reached_requested_time=physical_time>=c.finalTime-0.5*c.dt;
        const bool enough_history=force_cl_history.size()>=100;
        const bool best_effort_free=best_effort_steps==0;
        const bool resolved=reached_requested_time && enough_history && amp>1.0e-5 && final_converged_fraction>=0.95 && best_effort_free;
        terminal_status=(!transient_inner_failure && resolved)?"statistically_periodic":"failed";
        terminal_behavior=transient_inner_failure?"inner_solve_not_accepted":(best_effort_steps>0?"best_effort_continuation":(resolved?"resolved_lift_oscillation":"unresolved_lift_oscillation"));
        metadata["final_window_lift_peak_to_peak"]=amp;
      } else {
        const std::size_t begin=force_cd_history.size()>100?force_cd_history.size()-100:force_cd_history.size()/2;
        double cd_range=0.0; if(begin<force_cd_history.size()) cd_range=*std::max_element(force_cd_history.begin()+static_cast<std::ptrdiff_t>(begin),force_cd_history.end())-*std::min_element(force_cd_history.begin()+static_cast<std::ptrdiff_t>(begin),force_cd_history.end());
        const double mean_cd=begin<force_cd_history.size()?std::accumulate(force_cd_history.begin()+static_cast<std::ptrdiff_t>(begin),force_cd_history.end(),0.0)/static_cast<double>(force_cd_history.size()-begin):0.0;
        const bool force_stable=cd_range<=std::max(0.10*std::abs(mean_cd),1.0e-3);
        const bool residual_bounded=initial_norm>0.0 && last_norm<=initial_norm;
        const bool target_reached=residual_bounded && std::log10(std::max(initial_norm,kFloor)/std::max(last_norm,kFloor))>=c.outerTarget;
        // A bounded, force-stable terminal window is a credible steady
        // plateau even when the requested logarithmic residual target is too
        // aggressive for the scalar relaxation.  Keep that distinction in
        // terminal_behavior/reporting rather than labelling a bounded result
        // as a numerical failure.
        const bool plateau_converged=residual_bounded&&force_stable;
        terminal_status=(target_reached||plateau_converged)?"converged":"failed";
        terminal_behavior=target_reached?"residual_target":(plateau_converged?"stable_plateau":"diverged_or_unstable");
        metadata["terminal_force_cd_range"]=cd_range;
        metadata["terminal_force_cd_mean"]=mean_cd;
        metadata["residual_target_reached"]=target_reached;
      }
      metadata["terminal_behavior"]=terminal_behavior;
      metadata["convergence_status"]=terminal_status;
      metadata["completed"]=(terminal_status!="failed");
      std::ofstream metadata_out(out_path/"metadata.json");
      metadata_out << metadata.dump(2) << "\n";
      // Keep the status description honest about the scalar Jacobi/Richardson
      // correction used by both the steady and transient inner loops.
      std::ifstream status_in(out_path/"run_status.json");
      json status; status_in >> status;
      status["convergence_status"]=terminal_status;
      if(c.transient) {
        if(transient_inner_failure) status["notes"]="true BDF2 dual-time run terminated before accepting physical step "+std::to_string(failed_physical_step)+" because the terminal retry residual ratio "+std::to_string(failed_inner_ratio)+" did not reach the inner target; histories and final field remain at the last accepted step";
        else status["notes"]="nominal BDF2 physical steps with documented converged BE substeps replacing rare rejected intervals; fresh full residual assembly at every inner iteration and frozen histories; terminal lift oscillation is classified from the final force window";
      }
      else status["notes"]=c.steadyBlockCoupling>0.0?"steady pseudo-time run used the supplied CFL continuation and repeated frozen 4x4 block-Jacobi iterations; terminal behavior is classified from residual boundedness and final force stability":"steady pseudo-time run used the supplied CFL continuation and repeated scalar Jacobi/Richardson iterations; terminal behavior is classified from residual boundedness and final force stability";
      std::ofstream status_out(out_path/"run_status.json");
      status_out << status.dump(2) << "\n";
    }
    MPI_Barrier(MPI_COMM_WORLD);MPI_Finalize();return 0;
  } catch(const std::exception& e){if(rank==0)std::cerr<<"aurora-fv error: "<<e.what()<<"\n";MPI_Abort(MPI_COMM_WORLD,2);return 2;}
}
