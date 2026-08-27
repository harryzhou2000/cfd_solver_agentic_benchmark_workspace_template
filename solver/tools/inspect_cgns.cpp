#include <cgnslib.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <mesh.cgns>\n", argv[0]); return 1; }
    int fn;
    if (cg_open(argv[1], CG_MODE_READ, &fn) != CG_OK) { fprintf(stderr, "Cannot open %s: %s\n", argv[1], cg_get_error()); return 1; }

    int nbases;
    cg_nbases(fn, &nbases);
    printf("Number of bases: %d\n", nbases);

    for (int B = 1; B <= nbases; B++) {
        char basename[256]; int cdim, pdim;
        cg_base_read(fn, B, basename, &cdim, &pdim);
        printf("Base %d: '%s', cell_dim=%d, phys_dim=%d\n", B, basename, cdim, pdim);

        int nzones;
        cg_nzones(fn, B, &nzones);
        printf("  Zones: %d\n", nzones);

        for (int Z = 1; Z <= nzones; Z++) {
            CGNS_ENUMT(ZoneType_t) zt;
            cg_zone_type(fn, B, Z, &zt);
            char zonename[256];
            cgsize_t sizes[9];
            cg_zone_read(fn, B, Z, zonename, sizes);
            printf("  Zone %d: '%s', type=%d, sizes=[%lld, %lld, %lld]\n", Z, zonename, zt,
                   (long long)sizes[0], (long long)sizes[1], (long long)sizes[2]);

            int ncoords;
            cg_ncoords(fn, B, Z, &ncoords);
            printf("    Coords: %d\n", ncoords);
            for (int C = 1; C <= ncoords; C++) {
                char coordname[256];
                CGNS_ENUMT(DataType_t) dt;
                cg_coord_info(fn, B, Z, C, &dt, coordname);
                printf("    Coord %d: '%s' (type=%d)\n", C, coordname, dt);
            }

            int nsections;
            cg_nsections(fn, B, Z, &nsections);
            printf("    Sections: %d\n", nsections);
            for (int S = 1; S <= nsections; S++) {
                char secname[256];
                CGNS_ENUMT(ElementType_t) et;
                cgsize_t estart, eend;
                int nbnd, pflag;
                cg_section_read(fn, B, Z, S, secname, &et, &estart, &eend, &nbnd, &pflag);
                printf("    Section %d: '%s', type=%d, range=[%lld,%lld], nbndry=%d\n",
                       S, secname, et, (long long)estart, (long long)eend, nbnd);
            }

            int nfamilies;
            cg_nfamilies(fn, B, &nfamilies);
            printf("    Families in base: %d\n", nfamilies);
            for (int F = 1; F <= nfamilies; F++) {
                char famname[256]; int nfbc, ngeo;
                cg_family_read(fn, B, F, famname, &nfbc, &ngeo);
                printf("    Family %d: '%s', nbc=%d\n", F, famname, nfbc);
            }

            int nbocos;
            cg_nbocos(fn, B, Z, &nbocos);
            printf("    BCs: %d\n", nbocos);
            for (int BC = 1; BC <= nbocos; BC++) {
                char bcname[256];
                CGNS_ENUMT(BCType_t) bctype;
                CGNS_ENUMT(PointSetType_t) pstype;
                cgsize_t npts;
                int normalidx[3], ndataset;
                cgsize_t normallistsize;
                CGNS_ENUMT(DataType_t) ndt;
                cg_boco_info(fn, B, Z, BC, bcname, &bctype, &pstype, &npts, normalidx, &normallistsize, &ndt, &ndataset);
                printf("    BC %d: '%s', type=%d, pstype=%d, npts=%lld\n",
                       BC, bcname, bctype, pstype, (long long)npts);

                char famname[256];
                if (cg_goto(fn, B, "Zone_t", Z, "ZoneBC_t", 1, "BC_t", BC, "end") == CG_OK) {
                    if (cg_famname_read(famname) == CG_OK) {
                        printf("      FamilyName: '%s'\n", famname);
                    }
                }
            }
        }
    }
    cg_close(fn);
    return 0;
}
