#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "cgnslib.h"

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: probe <file.cgns>\n"); return 1; }
    int fn; if (cg_open(argv[1], CG_MODE_READ, &fn)) { printf("ERR %s\n", cg_get_error()); return 1; }
    int nbases; cg_nbases(fn, &nbases);
    for (int b = 1; b <= nbases; ++b) {
        char bname[128]; int cd, pd; cg_base_read(fn, b, bname, &cd, &pd);
        printf("base %d '%s' celldim=%d physdim=%d\n", b, bname, cd, pd);
        int nzones; cg_nzones(fn, b, &nzones);
        for (int z = 1; z <= nzones; ++z) {
            cgsize_t size[9]; char zname[128]; ZoneType_t zt;
            cg_zone_type(fn, b, z, &zt); cg_zone_read(fn, b, z, zname, size);
            printf(" zone %d '%s' type=%d nodes=%lld cells=%lld\n", z, zname, (int)zt, (long long)size[0], (long long)size[1]);
            char fam[128]; if (cg_famname_read(fam) == CG_OK) printf("  family: %s\n", fam);
            int nsections; cg_nsections(fn, b, z, &nsections);
            for (int s = 1; s <= nsections; ++s) {
                char sname[128]; ElementType_t et; cgsize_t st, en; int nbdry, pflag;
                cg_section_read(fn, b, z, s, sname, &et, &st, &en, &nbdry, &pflag);
                int nn = 0; if (et != MIXED) cg_npe(et, &nn);
                printf("  section %d '%s' type=%d(npe=%d) range=%lld..%lld nbdry=%d\n", s, sname, (int)et, nn, (long long)st, (long long)en, nbdry);
            }
            int nbc; cg_nbocos(fn, b, z, &nbc);
            for (int c = 1; c <= nbc; ++c) {
                char bcname[128]; BCType_t bt; PointSetType_t pst; cgsize_t npts; DataType_t ndt;
                int NormalListFlag = 0; cgsize_t ndata = 0;
                int NormalIndex=0; cgsize_t NormalListSize=0; DataType_t ndt2; int ndataset=0;
                cg_boco_info(fn, b, z, c, bcname, &bt, &pst, &npts, &NormalIndex, &NormalListSize, &ndt2, &ndataset);
                GridLocation_t gloc; cg_boco_gridlocation_read(fn, b, z, c, &gloc);
                printf("  boco %d '%s' bctype=%d pointset=%d npts=%lld gloc=%d\n", c, bcname, (int)bt, (int)pst, (long long)npts, (int)gloc);
            }
            int nconn; cg_nconns(fn, b, z, &nconn);
            for (int c = 1; c <= nconn; ++c) {
                char cname[128]; GridLocation_t gl; GridConnectivityType_t gct; PointSetType_t pst; cgsize_t npts;
                char donor[128]; ZoneType_t dzt; PointSetType_t dpst; DataType_t ddt; cgsize_t ndpts;
                cg_conn_info(fn, b, z, c, cname, &gl, &gct, &pst, &npts, donor, &dzt, &dpst, &ddt, &ndpts);
                printf("  conn %d '%s' gctype=%d npts=%lld donor='%s' ndpts=%lld\n", c, cname, (int)gct, (long long)npts, donor, (long long)ndpts);
            }
        }
        int nfam; cg_nfamilies(fn, b, &nfam);
        for (int f = 1; f <= nfam; ++f) {
            char fname[128]; int nbc2, ngeo; cg_family_read(fn, b, f, fname, &nbc2, &ngeo);
            printf(" family %d '%s' nFamBC=%d\n", f, fname, nbc2);
        }
    }
    cg_close(fn);
    return 0;
}
