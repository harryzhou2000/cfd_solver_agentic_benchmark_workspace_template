#include <cgnslib.h>
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: %s file.cgns\n", argv[0]); return 1; }
    int fn;
    if (cg_open(argv[1], CG_MODE_READ, &fn)) { printf("ERR %s\n", cg_get_error()); return 1; }
    float ver; cg_version(fn, &ver); printf("cgns version %f\n", ver);
    int nbases; cg_nbases(fn, &nbases); printf("nbases %d\n", nbases);
    for (int b = 1; b <= nbases; b++) {
        char basename[128]; int cd, pd;
        cg_base_read(fn, b, basename, &cd, &pd);
        printf("base %d '%s' cell_dim %d phys_dim %d\n", b, basename, cd, pd);
        int nzones; cg_nzones(fn, b, &nzones); printf("  nzones %d\n", nzones);
        for (int z = 1; z <= nzones; z++) {
            char zonename[128]; cgsize_t size[9];
            cg_zone_read(fn, b, z, zonename, size);
            ZoneType_t zt; cg_zone_type(fn, b, z, &zt);
            printf("  zone %d '%s' type %d verts %lld cells %lld\n", z, zonename, (int)zt,
                   (long long)size[0], (long long)size[1]);
            int nsec; cg_nsections(fn, b, z, &nsec);
            for (int s = 1; s <= nsec; s++) {
                char secname[128]; ElementType_t et; cgsize_t start, end; int nb, pf;
                cg_section_read(fn, b, z, s, secname, &et, &start, &end, &nb, &pf);
                printf("    sec %d '%s' type %s range [%lld,%lld] nboundary %d\n", s, secname,
                       cg_ElementTypeName(et), (long long)start, (long long)end, nb);
            }
            int nbc; cg_nbocos(fn, b, z, &nbc);
            for (int c = 1; c <= nbc; c++) {
                char bcname[128]; BCType_t bct; PointSetType_t pst; cgsize_t npts;
                int ndset; DataType_t ndt; cgsize_t nls; int nidx[3];
                cg_boco_info(fn, b, z, c, bcname, &bct, &pst, &npts, nidx, &nls, &ndt, &ndset);
                printf("    boco %d '%s' bctype %s pst %d npts %lld\n", c, bcname,
                       cg_BCTypeName(bct), (int)pst, (long long)npts);
                char fam[128]; fam[0]=0;
                cg_goto(fn, b, "Zone_t", z, "ZoneBC_t", 1, "BC_t", c, "end");
                if (cg_famname_read(fam) == CG_OK) printf("      family '%s'\n", fam);
            }
            int nfam; cg_nfamilies(fn, b, &nfam);
            for (int f = 1; f <= nfam; f++) {
                char famname[128]; int nfambc, ngeo;
                cg_family_read(fn, b, f, famname, &nfambc, &ngeo);
                printf("    family %d '%s' nfambc %d\n", f, famname, nfambc);
            }
        }
    }
    cg_close(fn);
    return 0;
}
