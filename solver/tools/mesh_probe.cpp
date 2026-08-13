// Small diagnostic tool: dumps the CGNS structure, zone sizes, element counts,
// and boundary-condition layout of a mesh. Part of solver/tools diagnostics.
#include <cgnslib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

namespace {
void fail(const char* msg) {
    fprintf(stderr, "mesh_probe: %s\n", msg);
    exit(1);
}

const char* elem_type_name(CGNS_ENUMT(ElementType_t) t) {
    switch (t) {
        case CGNS_ENUMV(NODE): return "NODE";
        case CGNS_ENUMV(BAR_2): return "BAR_2";
        case CGNS_ENUMV(TRI_3): return "TRI_3";
        case CGNS_ENUMV(QUAD_4): return "QUAD_4";
        case CGNS_ENUMV(TETRA_4): return "TETRA_4";
        case CGNS_ENUMV(PYRA_5): return "PYRA_5";
        case CGNS_ENUMV(PENTA_6): return "PENTA_6";
        case CGNS_ENUMV(HEXA_8): return "HEXA_8";
        case CGNS_ENUMV(MIXED): return "MIXED";
        default: return "OTHER";
    }
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: mesh_probe <file.cgns>\n");
        return 2;
    }
    int fid = -1;
    if (cg_open(argv[1], CG_MODE_READ, &fid) != CG_OK) fail("cg_open failed");
    int nbases = 0;
    if (cg_nbases(fid, &nbases) != CG_OK) fail("cg_nbases failed");
    printf("file=%s bases=%d\n", argv[1], nbases);
    for (int b = 1; b <= nbases; ++b) {
        char basename[65] = {0};
        int cell_dim = 0, phys_dim = 0;
        cg_base_read(fid, b, basename, &cell_dim, &phys_dim);
        printf("  base[%d] name=%s cell_dim=%d phys_dim=%d\n", b, basename, cell_dim,
               phys_dim);
        int nzones = 0;
        cg_nzones(fid, b, &nzones);
        for (int z = 1; z <= nzones; ++z) {
            char zonename[65] = {0};
            cgsize_t sizes[9] = {0};
            cg_zone_read(fid, b, z, zonename, sizes);
            CGNS_ENUMT(ZoneType_t) ztype;
            cg_zone_type(fid, b, z, &ztype);
            (void)ztype;
            int nsections = 0;
            cg_nsections(fid, b, z, &nsections);
            printf("    zone[%d] name=%s sizes=[%ld %ld %ld] nsections=%d\n", z, zonename,
                   (long)sizes[0], (long)sizes[1], (long)sizes[2], nsections);
            for (int s = 1; s <= nsections; ++s) {
                char secname[65] = {0};
                CGNS_ENUMT(ElementType_t) etype;
                cgsize_t start = 0, end = 0;
                int nbndry = 0, parent_flag = 0;
                cg_section_read(fid, b, z, s, secname, &etype, &start, &end, &nbndry,
                                &parent_flag);
                printf("      section[%d] name=%s type=%s elems=%ld..%ld n=%ld\n", s,
                       secname, elem_type_name(etype), (long)start, (long)end,
                       (long)(end - start + 1));
            }
            int nbocos = 0;
            cg_nbocos(fid, b, z, &nbocos);
            printf("      nbocos=%d\n", nbocos);
            for (int i = 1; i <= nbocos; ++i) {
                char boname[65] = {0};
                CGNS_ENUMT(BCType_t) botype;
                CGNS_ENUMT(PointSetType_t) ptset;
                cgsize_t npnts = 0;
                int normalindex = 0;
                cgsize_t normallistflag = 0;
                CGNS_ENUMT(DataType_t) normaldatatype;
                int ndataset = 0;
                cg_boco_info(fid, b, z, i, boname, &botype, &ptset, &npnts,
                             &normalindex, &normallistflag, &normaldatatype, &ndataset);
                // Read points, handling range or list point sets.
                std::vector<cgsize_t> pts;
                if (ptset == CGNS_ENUMV(PointRange)) {
                    pts.resize(2 * npnts);
                    cg_boco_read(fid, b, z, i, pts.data(), NULL);
                } else if (ptset == CGNS_ENUMV(PointList)) {
                    pts.resize(npnts);
                    cg_boco_read(fid, b, z, i, pts.data(), NULL);
                }
                printf("        boco[%d] name=%s type=%d ptset=%d npnts=%ld\n", i, boname,
                       (int)botype, (int)ptset, (long)npnts);
            }
        }
        // Family info
        int nfamilies = 0;
        cg_nfamilies(fid, b, &nfamilies);
        for (int f = 1; f <= nfamilies; ++f) {
            char famname[65] = {0};
            int nboco = 0, ngeo = 0;
            cg_family_read(fid, b, f, famname, &nboco, &ngeo);
            printf("    family[%d] name=%s nboco=%d ngeo=%d\n", f, famname, nboco, ngeo);
        }
    }
    // Interface coordinate comparison: read 1to1 PointList/PointListDonor pairs
    // and check whether donor/receiver coordinates match bitwise.
    if (nbases >= 1) {
        int nzones = 0;
        cg_nzones(fid, 1, &nzones);
        for (int z = 1; z <= nzones; ++z) {
            int nconn = 0;
            cg_nconns(fid, 1, z, &nconn);
            for (int c = 1; c <= nconn; ++c) {
                char name[65] = {0};
                CGNS_ENUMT(GridLocation_t) loc;
                CGNS_ENUMT(GridConnectivityType_t) ctype;
                CGNS_ENUMT(PointSetType_t) pset;
                cgsize_t npnts = 0;
                char donorname[65] = {0};
                CGNS_ENUMT(ZoneType_t) donortype;
                CGNS_ENUMT(PointSetType_t) dptset;
                CGNS_ENUMT(DataType_t) donor_datatype_arg;
                cgsize_t ndata_donor = 0;
                cg_conn_info(fid, 1, z, c, name, &loc, &ctype, &pset, &npnts,
                             donorname, &donortype, &dptset, &donor_datatype_arg,
                             &ndata_donor);
                printf("  conn zone[%d] %s -> %s npnts=%ld ctype=%d\n", z, name,
                       donorname, (long)npnts, (int)ctype);
                if (npnts > 0 && npnts <= 2000) {
                    std::vector<cgsize_t> pts(npnts), donpts(npnts);
                    if (cg_conn_read(fid, 1, z, c, pts.data(),
                                     CGNS_ENUMV(RealDouble), donpts.data()) == CG_OK) {
                        // Load coordinates of this zone and donor zone.
                        int dz = 0;
                        for (int zz = 1; zz <= nzones; ++zz) {
                            char zname[65] = {0};
                            cgsize_t sizes[9] = {0};
                            cg_zone_read(fid, 1, zz, zname, sizes);
                            if (strcmp(zname, donorname) == 0) {
                                dz = zz;
                                break;
                            }
                        }
                        if (dz > 0) {
                            char zname[65] = {0};
                            cgsize_t sizes[9] = {0};
                            cg_zone_read(fid, 1, z, zname, sizes);
                            int nn = (int)sizes[0];
                            cg_zone_read(fid, 1, dz, zname, sizes);
                            int nn_d = (int)sizes[0];
                            std::vector<double> x(nn), y(nn), xd(nn_d), yd(nn_d);
                            cg_coord_read(fid, 1, z, "CoordinateX", CGNS_ENUMV(RealDouble),
                                          (cgsize_t*)NULL, (cgsize_t*)NULL, x.data());
                            cg_coord_read(fid, 1, z, "CoordinateY", CGNS_ENUMV(RealDouble),
                                          (cgsize_t*)NULL, (cgsize_t*)NULL, y.data());
                            cg_coord_read(fid, 1, dz, "CoordinateX", CGNS_ENUMV(RealDouble),
                                          (cgsize_t*)NULL, (cgsize_t*)NULL, xd.data());
                            cg_coord_read(fid, 1, dz, "CoordinateY", CGNS_ENUMV(RealDouble),
                                          (cgsize_t*)NULL, (cgsize_t*)NULL, yd.data());
                            int same = 0, diff = 0;
                            for (cgsize_t k = 0; k < npnts; ++k) {
                                if (x[pts[k] - 1] == xd[donpts[k] - 1] &&
                                    y[pts[k] - 1] == yd[donpts[k] - 1])
                                    ++same;
                                else
                                    ++diff;
                            }
                            printf("    coord match: same=%d diff=%d\n", same, diff);
                        }
                    }
                }
            }
        }
    }
    cg_close(fid);
    return 0;
}
