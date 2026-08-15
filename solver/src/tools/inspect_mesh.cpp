// Mesh inspection helper using the CGNS mid-level API.
// Prints bases, zones, sections (element type + range), coordinates, and the
// FamilyName attached to each Elements_t section. Used to learn the supplied
// NACA0012_H2.cgns / CylinderB1.cgns layout before writing the reader.
#include <cgnslib.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char* elt_name(ElementType_t t) {
  switch (t) {
    case NODE: return "NODE";
    case BAR_2: return "BAR_2";
    case BAR_3: return "BAR_3";
    case TRI_3: return "TRI_3";
    case TRI_6: return "TRI_6";
    case QUAD_4: return "QUAD_4";
    case QUAD_8: return "QUAD_8";
    case MIXED: return "MIXED";
    default: return "OTHER";
  }
}

static const char* bctype_name(BCType_t t) {
  switch (t) {
    case BCFarfield: return "BCFarfield";
    case BCWall: return "BCWall";
    case BCWallInviscid: return "BCWallInviscid";
    case BCWallViscous: return "BCWallViscous";
    case FamilySpecified: return "FamilySpecified";
    default: return "OtherBC";
  }
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: inspect_mesh <file.cgns>\n"); return 1; }
  int fn;
  if (cg_open(argv[1], CG_MODE_READ, &fn) != CG_OK) {
    std::printf("cg_open failed: %s\n", cg_get_error()); return 1;
  }
  int nbases = 0; cg_nbases(fn, &nbases);
  std::printf("bases: %d\n", nbases);
  for (int b = 1; b <= nbases; ++b) {
    char basename[64]; int celldim = 0, physdim = 0;
    cg_base_read(fn, b, basename, &celldim, &physdim);
    std::printf("  base %d: name=%s celldim=%d physdim=%d\n", b, basename, celldim, physdim);

    int nfam = 0; cg_nfamilies(fn, b, &nfam);
    std::printf("  families: %d\n", nfam);
    for (int f = 1; f <= nfam; ++f) {
      char famname[64]; int famdir = 0, nfambc = 0;
      cg_family_read(fn, b, f, famname, &famdir, &nfambc);
      std::printf("    family %d: name=%s dir=%d nfambc=%d\n", f, famname, famdir, nfambc);
    }

    int nzones = 0; cg_nzones(fn, b, &nzones);
    std::printf("  zones: %d\n", nzones);
    for (int z = 1; z <= nzones; ++z) {
      char zonename[64]; cgsize_t sizes[9];
      for (int i = 0; i < 9; ++i) sizes[i] = 0;
      cg_zone_read(fn, b, z, zonename, sizes);
      ZoneType_t zt;
      cg_zone_type(fn, b, z, &zt);
      std::printf("    zone %d: name=%s type=%d sizes=[%lld,%lld,%lld]\n",
        z, zonename, (int)zt, (long long)sizes[0], (long long)sizes[1], (long long)sizes[2]);

      int nsec = 0; cg_nsections(fn, b, z, &nsec);
      std::printf("    sections: %d\n", nsec);
      for (int s = 1; s <= nsec; ++s) {
        char secname[64]; ElementType_t et; cgsize_t start = 0, end = 0; int nbndry = 0, parent = 0;
        cg_section_read(fn, b, z, s, secname, &et, &start, &end, &nbndry, &parent);
        char fambuf[128] = "";
        if (cg_goto(fn, b, "Zone_t", z, "Elements_t", s, "end") == CG_OK) {
          cg_famname_read(fambuf);  // reads FamilyName_t if present
        }
        std::printf("      section %d: name=%s type=%s range=[%lld,%lld] count=%lld nbndry=%d parent=%d fam=%s\n",
          s, secname, elt_name(et), (long long)start, (long long)end,
          (long long)(end - start + 1), nbndry, parent, fambuf);
      }

      int ncoords = 0; cg_ncoords(fn, b, z, &ncoords);
      std::printf("    coords: %d\n", ncoords);
      for (int c = 1; c <= ncoords; ++c) {
        char cname[64]; DataType_t dt = DataTypeNull;
        cg_coord_info(fn, b, z, c, &dt, cname);
        std::printf("      coord %d: name=%s datatype=%d\n", c, cname, (int)dt);
      }

      int nbocos = 0; cg_nbocos(fn, b, z, &nbocos);
      std::printf("    BC_t nodes: %d\n", nbocos);
      for (int bc = 1; bc <= nbocos; ++bc) {
        char bname[64]; BCType_t bctype = BCTypeNull; PointSetType_t pst = PointSetTypeNull;
        cgsize_t npnts = 0, normallist = 0; int normalindex = 0, ndataset = 0;
        DataType_t normaldt = DataTypeNull;
        cg_boco_info(fn, b, z, bc, bname, &bctype, &pst, &npnts, &normalindex,
                     &normallist, &normaldt, &ndataset);
        std::vector<cgsize_t> pnts(npnts > 0 ? npnts : 1, 0);
        if (npnts > 0) cg_boco_read(fn, b, z, bc, pnts.data(), pnts.data());
        std::printf("      bc %d: name=%s bctype=%s pset=%d npnts=%lld first=%lld\n",
          bc, bname, bctype_name(bctype), (int)pst, (long long)npnts,
          npnts > 0 ? (long long)pnts[0] : -1LL);
      }
    }
  }
  cg_close(fn);
  return 0;
}
