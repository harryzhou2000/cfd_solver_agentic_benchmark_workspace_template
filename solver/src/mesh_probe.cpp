#include <cgnslib.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: mesh_probe <mesh.cgns>\n";
        return 1;
    }
    std::string filename = argv[1];
    int fn;
    if (cg_open(filename.c_str(), CG_MODE_READ, &fn) != CG_OK) {
        cg_error_print();
        return 1;
    }
    int nbases;
    cg_nbases(fn, &nbases);
    std::cout << "bases: " << nbases << "\n";
    for (int B = 1; B <= nbases; B++) {
        char basename[128];
        int cell_dim, phys_dim;
        cg_base_read(fn, B, basename, &cell_dim, &phys_dim);
        std::cout << "  base " << B << ": '" << basename << "' cell_dim=" << cell_dim
                  << " phys_dim=" << phys_dim << "\n";
        int nzones;
        cg_nzones(fn, B, &nzones);
        for (int Z = 1; Z <= nzones; Z++) {
            char zname[128];
            cgsize_t sizes[9];
            cg_zone_read(fn, B, Z, zname, sizes);
            std::cout << "    zone " << Z << ": '" << zname << "'"
                      << " verts=" << sizes[0] << " cells=" << sizes[1]
                      << " bnd=" << sizes[2] << "\n";
            int nsections;
            cg_nsections(fn, B, Z, &nsections);
            for (int S = 1; S <= nsections; S++) {
                char sname[128];
                CGNS_ENUMT(ElementType_t) etype;
                cgsize_t start, end;
                int nbndry, pflag;
                cg_section_read(fn, B, Z, S, sname, &etype, &start, &end, &nbndry, &pflag);
                char fam[128] = {0};
                if (cg_goto(fn, B, "Zone_t", Z, "Elements_t", S, nullptr) == CG_OK) {
                    cg_famname_read(fam);
                }
                std::cout << "      sect " << S << ": '" << sname << "'"
                          << " type=" << (int)etype
                          << " range=" << start << "-" << end
                          << " fam='" << (fam[0] ? fam : "") << "'\n";
            }
            int nfam;
            cg_nfamilies(fn, B, &nfam);
            for (int F = 1; F <= nfam; F++) {
                char famname[128];
                int nfambc, nfamcc;
                cg_family_read(fn, B, F, famname, &nfambc, &nfamcc);
                std::cout << "      family " << F << ": '" << famname << "'\n";
            }
            int nconns;
            cg_nconns(fn, B, Z, &nconns);
            for (int ic = 1; ic <= nconns; ic++) {
                char cname[128], donor[128];
                CGNS_ENUMT(GridLocation_t) loc;
                CGNS_ENUMT(GridConnectivityType_t) ctype;
                CGNS_ENUMT(PointSetType_t) ptype, donor_ptype;
                CGNS_ENUMT(ZoneType_t) donor_ztype;
                CGNS_ENUMT(DataType_t) dtype;
                cgsize_t npnts, ndata;
                cg_conn_info(fn, B, Z, ic, cname, &loc, &ctype, &ptype,
                             &npnts, donor, &donor_ztype, &donor_ptype,
                             &dtype, &ndata);
                std::cout << "      conn " << ic << ": '" << cname << "' donor='"
                          << donor << "' type=" << (int)ctype << " npnts=" << npnts << "\n";
            }
        }
    }
    cg_close(fn);
    return 0;
}
