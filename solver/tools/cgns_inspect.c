#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cgnslib.h"

int main(int argc, char** argv) {
  int fn;
  if (cg_open(argv[1], CG_MODE_READ, &fn)) { cg_error_print(); return 1; }
  int nbases; cg_nbases(fn, &nbases);
  printf("nbases=%d\n", nbases);
  for (int b = 1; b <= nbases; ++b) {
    char bname[64]; int cdim, pdim;
    cg_base_read(fn, b, bname, &cdim, &pdim);
    printf("base %d: '%s' celldim=%d physdim=%d\n", b, bname, cdim, pdim);
    int nfam; cg_nfamilies(fn, b, &nfam);
    for (int f = 1; f <= nfam; ++f) {
      char fname[64]; int nbc, ngeo;
      cg_family_read(fn, b, f, fname, &nbc, &ngeo);
      printf("  family %d: '%s' nFamBC=%d nGeo=%d\n", f, fname, nbc, ngeo);
      for (int i = 1; i <= nbc; ++i) {
        char bcn[64]; CGNS_ENUMT(BCType_t) bct;
        cg_fambc_read(fn, b, f, i, bcn, &bct);
        printf("    fambc '%s' type=%s\n", bcn, BCTypeName[bct]);
      }
    }
    int nzones; cg_nzones(fn, b, &nzones);
    printf("  nzones=%d\n", nzones);
    for (int z = 1; z <= nzones; ++z) {
      char zname[64]; cgsize_t size[9];
      cg_zone_read(fn, b, z, zname, size);
      CGNS_ENUMT(ZoneType_t) zt; cg_zone_type(fn, b, z, &zt);
      printf("  zone %d '%s' type=%s size=[%lld %lld %lld]\n", z, zname, ZoneTypeName[zt],
             (long long)size[0], (long long)size[1], (long long)size[2]);
      int ncoords; cg_ncoords(fn, b, z, &ncoords);
      for (int c = 1; c <= ncoords; ++c) {
        char cn[64]; CGNS_ENUMT(DataType_t) dt;
        cg_coord_info(fn, b, z, c, &dt, cn);
        printf("    coord %d '%s' type=%s\n", c, cn, DataTypeName[dt]);
      }
      int nsec; cg_nsections(fn, b, z, &nsec);
      for (int s = 1; s <= nsec; ++s) {
        char sn[64]; CGNS_ENUMT(ElementType_t) et; cgsize_t st, en; int nb, pf;
        cg_section_read(fn, b, z, s, sn, &et, &st, &en, &nb, &pf);
        cgsize_t ds; cg_ElementDataSize(fn, b, z, s, &ds);
        printf("    section %d '%s' type=%s range=[%lld,%lld] nbndry=%d parentflag=%d datasize=%lld\n",
               s, sn, ElementTypeName[et], (long long)st, (long long)en, nb, pf, (long long)ds);
      }
      int nbocos; cg_nbocos(fn, b, z, &nbocos);
      for (int i = 1; i <= nbocos; ++i) {
        char bn[64]; CGNS_ENUMT(BCType_t) bt; CGNS_ENUMT(PointSetType_t) pst;
        cgsize_t npnts, nrmlistflag; int nrmindex[3], ndataset; CGNS_ENUMT(DataType_t) ndt;
        cg_boco_info(fn, b, z, i, bn, &bt, &pst, &npnts, nrmindex, &nrmlistflag, &ndt, &ndataset);
        CGNS_ENUMT(GridLocation_t) gl; cg_boco_gridlocation_read(fn, b, z, i, &gl);
        char famname[64]=""; 
        cg_goto(fn, b, "Zone_t", z, "ZoneBC_t", 1, "BC_t", i, NULL);
        if (cg_famname_read(famname)) strcpy(famname, "<none>");
        cgsize_t pnts[64];
        if (npnts <= 32) cg_boco_read(fn, b, z, i, pnts, NULL);
        printf("    boco %d '%s' bctype=%s pointset=%s npnts=%lld gridloc=%s family='%s'",
               i, bn, BCTypeName[bt], PointSetTypeName[pst], (long long)npnts, GridLocationName[gl], famname);
        if (npnts <= 32) { printf(" pts="); for (cgsize_t k=0;k<npnts;k++) printf("%lld ", (long long)pnts[k]); }
        printf("\n");
      }
      int nconns; cg_nconns(fn, b, z, &nconns);
      for (int i = 1; i <= nconns; ++i) {
        char cn[64], dn[64];
        CGNS_ENUMT(GridLocation_t) loc; CGNS_ENUMT(GridConnectivityType_t) ct;
        CGNS_ENUMT(PointSetType_t) pst, dpst; CGNS_ENUMT(ZoneType_t) dzt; CGNS_ENUMT(DataType_t) ddt;
        cgsize_t npnts, ndpnts;
        cg_conn_info(fn, b, z, i, cn, &loc, &ct, &pst, &npnts, dn, &dzt, &dpst, &ddt, &ndpnts);
        printf("    conn %d '%s' loc=%s type=%s pst=%s npnts=%lld donor='%s' dpst=%s ndpnts=%lld\n",
               i, cn, GridLocationName[loc], GridConnectivityTypeName[ct], PointSetTypeName[pst],
               (long long)npnts, dn, PointSetTypeName[dpst], (long long)ndpnts);
      }
      int n1to1; cg_n1to1(fn, b, z, &n1to1);
      printf("    n1to1=%d\n", n1to1);
    }
  }
  cg_close(fn);
  return 0;
}
