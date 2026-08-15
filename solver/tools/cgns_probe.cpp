#include <cgnslib.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(int ier, const std::string& what) {
  if (ier != CG_OK) {
    throw std::runtime_error(what + ": " + cg_get_error());
  }
}

std::string etype_name(ElementType_t t) {
  const char* name = cg_ElementTypeName(t);
  return name ? name : "unknown";
}

std::string bocotype_name(BCType_t t) {
  const char* name = cg_BCTypeName(t);
  return name ? name : "unknown";
}

std::string pointset_name(PointSetType_t t) {
  const char* name = cg_PointSetTypeName(t);
  return name ? name : "unknown";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: cgns_probe <mesh.cgns>\n";
    return 2;
  }

  try {
    int fn = 0;
    check(cg_open(argv[1], CG_MODE_READ, &fn), "cg_open");
    int nbases = 0;
    check(cg_nbases(fn, &nbases), "cg_nbases");
    std::cout << "bases " << nbases << "\n";
    for (int ib = 1; ib <= nbases; ++ib) {
      char basename[33]{};
      int celldim = 0;
      int physdim = 0;
      check(cg_base_read(fn, ib, basename, &celldim, &physdim), "cg_base_read");
      std::cout << "base " << ib << " " << basename << " celldim " << celldim << " physdim "
                << physdim << "\n";
      int nzones = 0;
      check(cg_nzones(fn, ib, &nzones), "cg_nzones");
      for (int iz = 1; iz <= nzones; ++iz) {
        char zonename[33]{};
        cgsize_t size[9]{};
        check(cg_zone_read(fn, ib, iz, zonename, size), "cg_zone_read");
        ZoneType_t zt{};
        check(cg_zone_type(fn, ib, iz, &zt), "cg_zone_type");
        std::cout << "  zone " << iz << " " << zonename << " vertices " << size[0] << " cells "
                  << size[1] << " boundary_vertices " << size[2] << " type " << zt << "\n";

        int ncoords = 0;
        check(cg_ncoords(fn, ib, iz, &ncoords), "cg_ncoords");
        std::cout << "    coords " << ncoords << "\n";
        for (int ic = 1; ic <= ncoords; ++ic) {
          DataType_t dtype{};
          char cname[33]{};
          check(cg_coord_info(fn, ib, iz, ic, &dtype, cname), "cg_coord_info");
          std::cout << "      " << ic << " " << cname << " dtype " << dtype << "\n";
        }

        int nsections = 0;
        check(cg_nsections(fn, ib, iz, &nsections), "cg_nsections");
        std::cout << "    sections " << nsections << "\n";
        for (int is = 1; is <= nsections; ++is) {
          char sname[33]{};
          ElementType_t et{};
          cgsize_t start = 0;
          cgsize_t end = 0;
          int nbndry = 0;
          int parent_flag = 0;
          check(cg_section_read(fn, ib, iz, is, sname, &et, &start, &end, &nbndry, &parent_flag),
                "cg_section_read");
          cgsize_t data_size = 0;
          check(cg_ElementDataSize(fn, ib, iz, is, &data_size), "cg_ElementDataSize");
          std::cout << "      section " << is << " " << sname << " " << etype_name(et)
                    << " range [" << start << "," << end << "] count " << (end - start + 1)
                    << " data_size " << data_size << " nbndry " << nbndry
                    << " parent " << parent_flag << "\n";
        }

        int nbocos = 0;
        check(cg_nbocos(fn, ib, iz, &nbocos), "cg_nbocos");
        std::cout << "    bocos " << nbocos << "\n";
        for (int ibc = 1; ibc <= nbocos; ++ibc) {
          char bname[33]{};
          BCType_t bctype{};
          PointSetType_t ptset{};
          cgsize_t npnts = 0;
          int normal_index[3]{};
          cgsize_t normal_list_size = 0;
          DataType_t normal_dtype{};
          int ndataset = 0;
          check(cg_boco_info(fn, ib, iz, ibc, bname, &bctype, &ptset, &npnts, normal_index,
                             &normal_list_size, &normal_dtype, &ndataset),
                "cg_boco_info");
          std::cout << "      boco " << ibc << " " << bname << " type " << bocotype_name(bctype)
                    << " pointset " << pointset_name(ptset) << " npnts " << npnts
                    << " ndataset " << ndataset << "\n";
        }

        int nconns = 0;
        if (cg_nconns(fn, ib, iz, &nconns) == CG_OK) {
          std::cout << "    conns " << nconns << "\n";
          for (int ic = 1; ic <= nconns; ++ic) {
            char cname[33]{};
            GridLocation_t loc{};
            GridConnectivityType_t ctype{};
            PointSetType_t ptype{};
            cgsize_t npnts = 0;
            char donorname[33]{};
            ZoneType_t donor_ztype{};
            PointSetType_t donor_ptype{};
            DataType_t donor_dtype{};
            cgsize_t donor_npnts = 0;
            check(cg_conn_info(fn, ib, iz, ic, cname, &loc, &ctype, &ptype, &npnts, donorname,
                               &donor_ztype, &donor_ptype, &donor_dtype, &donor_npnts),
                  "cg_conn_info");
            std::cout << "      conn " << ic << " " << cname << " donor " << donorname
                      << " npnts " << npnts << " donor_npnts " << donor_npnts << "\n";
          }
        }
      }
    }
    check(cg_close(fn), "cg_close");
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
