# CGNS mesh structure report

Produced by `tools/probe_cgns.cpp` (build and run with `tools/build_probe.sh`).
Raw machine output: `probe_out/raw_report.md`. Every number below is read from
the files through the CGNS mid-level API; nothing is assumed.

Probe environment: CGNS 4.50, `sizeof(cgsize_t) = 8`. Both mesh files report
CGNS file version **3.310** and file precision **64-bit**.

---

## 0. Executive summary: the eight facts that matter for the reader

1. **`cg_n1to1()` returns 0 on CylinderB1.** The interface nodes are
   `GridConnectivity_t` (general), not `GridConnectivity1to1_t`. You must use
   `cg_nconns` / `cg_conn_info` / `cg_conn_read`. There is **no Transform**
   and no `PointRange` on these nodes.
2. **Connectivity `PointList`/`PointListDonor` are 1-based zone-local NODE
   indices**. The API reports `GridLocation = Vertex`, but no `GridLocation`
   node is actually stored in the file -- the library supplies the SIDS default.
   A raw-HDF5 reader must apply that default itself.
3. **BC `PointRange` values are ELEMENT indices**, not node indices
   (`GridLocation = EdgeCenter`, `PointSetType = PointRange`, `npnts = 2`).
   Map a boco to its section by **element-range containment**.
4. **`BCType` is useless for physics**: every boco is `FamilySpecified` and
   every `FamilyBC` is `Null`. The physical BC must come from the
   **FamilyName** string.
5. **All cells are counter-clockwise**: 0 negative signed areas out of 20816
   (NACA) and 10185 (Cylinder). No normalization pass needed.
6. **Both meshes are watertight and manifold**: 0 edges shared by 3+ cells, and
   the set of BC-named edges is exactly the set of 1-cell edges.
7. **Interface connectivity is stored twice** (mirrored in each zone), so
   650 pairs collapse to 320 unions on the cylinder. Union-find absorbs this.
8. **Dimensions differ between the two files**: NACA is `CellDim=2, PhysDim=3`
   and ships a `CoordinateZ`; Cylinder is `CellDim=2, PhysDim=2` with only X,Y.
   Read `PhysicalDimension` and `cg_ncoords`; do not hardcode.

### Family to body identification

| mesh | family | role | edges | geometric evidence |
|---|---|---|---|---|
| NACA0012_H2 | `bc-4` | **body (airfoil)** | 404 | x in [0, 1.00534], y in [-0.0600048, 0.0600048] |
| NACA0012_H2 | `bc-2` | **farfield** | 80 | all nodes at exactly r = 80 |
| CylinderB1 | `WALL` | **body (cylinder)** | 100 | all nodes at exactly r = 0.5 |
| CylinderB1 | `FAR` | **farfield** | 20 | all nodes at exactly r = 200 |

Note the element counts are counter-intuitive on NACA: the body has more
edges (404) than the farfield (80). Identify the body by geometry, not count.

---

## 1. Base-level facts

| | NACA0012_H2.cgns | CylinderB1.cgns |
|---|---|---|
| nbases | 1 | 1 |
| base name | `Base` | `Base` |
| CellDimension | 2 | 2 |
| PhysicalDimension | **3** | **2** |
| nzones | 1 | 2 |

### Families

NACA0012_H2 (3): `Unspecified` (no FamilyBC), `bc-2` (FamBC=`Null`),
`bc-4` (FamBC=`Null`).

CylinderB1 (3): `Unspecified` (no FamilyBC), `WALL` (FamBC=`Null`),
`FAR` (FamBC=`Null`).

Every `FamilyBC` BCType is `BCTypeNull`, so the family *name* is the only
carrier of boundary-condition intent.

---

## 2. Zones and coordinates

All zones are `Unstructured`. All coordinates are `RealDouble`. Every zone
carries `FamilyName = Unspecified` at the zone level (ignore it; the BC-level
FamilyName is what matters).

| mesh | zone | nodes | cells | coords | ranges |
|---|---|---|---|---|---|
| NACA | `dom-1` | 15682 | 20816 | X, Y, Z | X [-80, 80], Y [-80, 80], Z [-3.46448249171e-06, 0] |
| Cyl | `dom-1` | 7079 | 6901 | X, Y | X [-2, 10], Y [-2, 2] |
| Cyl | `dom-2` | 3236 | 3284 | X, Y | X [-200, 200], Y [-200, 200] |

NACA's `CoordinateZ` is nominally zero but **not identically zero**: its min is
`-3.46448249171e-06`, max exactly `+0.0`. A raw scan of all 15682 values shows
**15198 nodes (96.9%) have Z != 0.0**; only 484 are exactly zero. Magnitudes are
tiny (order 1e-8 to 3.5e-6) and all non-positive, so this is a near-flat sheet
with sub-micron waviness rather than a few stray nodes. Discarding Z is correct
for a 2-D solver, but it perturbs almost every node by an amount negligible
relative to the unit chord.

Cylinder `dom-1` is the near-body block (cylinder of radius 0.5 plus wake out
to x=10); `dom-2` is the outer block extending to r=200.

---

## 3. Element sections

`nbndry = 0` and `parent_flag = 0` for **every** section in both files, so
there is no ParentData and no `cg_poly_elements_read` needed. All sections are
homogeneous fixed-stride: only `TRI_3` (npe 3), `QUAD_4` (npe 4), `BAR_2`
(npe 2). `ElementDataSize == nelem * npe` exactly in all cases.

### NACA0012_H2, zone `dom-1`

| idx | name | type | start | end | nelem | npe | referenced by |
|---|---|---|---|---|---|---|---|
| 1 | `TriElements` | TRI_3 | 1 | 10752 | 10752 | 3 | (volume) |
| 2 | `QuadElements` | QUAD_4 | 10753 | 20816 | 10064 | 4 | (volume) |
| 3 | `bc-2` | BAR_2 | 20817 | 20896 | 80 | 2 | **BC** `bc-2` (family `bc-2`) |
| 4 | `bc-4` | BAR_2 | 20897 | 21300 | 404 | 2 | **BC** `bc-4` (family `bc-4`) |

### CylinderB1, zone `dom-1`

| idx | name | type | start | end | nelem | npe | referenced by |
|---|---|---|---|---|---|---|---|
| 1 | `TriElements` | TRI_3 | 1 | 64 | 64 | 3 | (volume) |
| 2 | `QuadElements` | QUAD_4 | 65 | 6901 | 6837 | 4 | (volume) |
| 3 | `WALL` | BAR_2 | 6902 | 7001 | 100 | 2 | **BC** `WALL` (family `WALL`) |
| 4 | `con-2` | BAR_2 | 7002 | 7021 | 20 | 2 | **connectivity** to `dom-2` |
| 5 | `con-3` | BAR_2 | 7022 | 7141 | 120 | 2 | **connectivity** to `dom-2` |
| 6 | `con-4` | BAR_2 | 7142 | 7181 | 40 | 2 | **connectivity** to `dom-2` |
| 7 | `con-5` | BAR_2 | 7182 | 7301 | 120 | 2 | **connectivity** to `dom-2` |
| 8 | `con-6` | BAR_2 | 7302 | 7321 | 20 | 2 | **connectivity** to `dom-2` |

### CylinderB1, zone `dom-2`

| idx | name | type | start | end | nelem | npe | referenced by |
|---|---|---|---|---|---|---|---|
| 1 | `TriElements` | TRI_3 | 1 | 436 | 436 | 3 | (volume) |
| 2 | `QuadElements` | QUAD_4 | 437 | 3284 | 2848 | 4 | (volume) |
| 3 | `FAR` | BAR_2 | 3285 | 3304 | 20 | 2 | **BC** `FAR` (family `FAR`) |
| 4 | `con-2` | BAR_2 | 3305 | 3324 | 20 | 2 | **connectivity** to `dom-1` |
| 5 | `con-3` | BAR_2 | 3325 | 3444 | 120 | 2 | **connectivity** to `dom-1` |
| 6 | `con-4` | BAR_2 | 3445 | 3484 | 40 | 2 | **connectivity** to `dom-1` |
| 7 | `con-5` | BAR_2 | 3485 | 3604 | 120 | 2 | **connectivity** to `dom-1` |
| 8 | `con-6` | BAR_2 | 3605 | 3624 | 20 | 2 | **connectivity** to `dom-1` |

### How to classify a 1-D (BAR_2) section

A BAR_2 section is a **boundary** section if some boco's `PointRange` falls
inside its `[start, end]`; otherwise it is an **interface** section. The probe
resolves this by range containment and it separates cleanly: on the cylinder,
`WALL`/`FAR` are claimed by bocos, and `con-2..con-6` are claimed by no boco.

A robust alternative that agrees on these files: the connectivity node is named
`1to1Connection:<section-name>`, so `con-3` pairs with
`1to1Connection:con-3`. Prefer the range-containment test; use the name only
as a cross-check.

**Important sizing trap:** boundary and interface sections extend *past* the
zone cell count. NACA declares `NCell = 20816` but sections run to element
21300. Size your cell arrays from the volume sections (TRI/QUAD) only, and note
that the volume sections happen to be contiguous from element 1 in both files.

---

## 4. Boundary conditions

Every boco in both files has the identical shape:
`BCType = FamilySpecified`, `GridLocation = EdgeCenter` (explicitly present,
not defaulted), `PointSetType = PointRange`, `npnts = 2`.

| mesh | zone | boco | FamilyName | PointRange | section | nelem |
|---|---|---|---|---|---|---|
| NACA | `dom-1` | `bc-2` | `bc-2` | [20817, 20896] | `bc-2` | 80 |
| NACA | `dom-1` | `bc-4` | `bc-4` | [20897, 21300] | `bc-4` | 404 |
| Cyl | `dom-1` | `WALL` | `WALL` | [6902, 7001] | `WALL` | 100 |
| Cyl | `dom-2` | `FAR` | `FAR` | [3285, 3304] | `FAR` | 20 |

### How a boco maps to an element section (definitive)

`GridLocation = EdgeCenter` means the two `PointRange` values are **element
indices in the zone's global element numbering**, inclusive. In these files the
range is exactly the `ElementRange` of the same-named BAR_2 section, but do
not rely on the name -- rely on containment:

```
for each boco b:
    lo = b.pnts[0], hi = b.pnts[1]      # element indices, inclusive
    find section s with s.start <= lo and hi <= s.end
    for e in [lo, hi]:
        local = e - s.start              # 0-based row in s.conn
        n0 = s.conn[local*2 + 0]         # 1-based zone-local node id
        n1 = s.conn[local*2 + 1]
        tag edge (n0, n1) with family b.family
```

To tag boundary edges by family: build your edge map from the volume cells,
then for each boco element look up the sorted node pair and attach
`b.family`. The probe verifies this lookup is exact (see section 6).

Read the FamilyName with
`cg_goto(fn, B, "Zone_t", Z, "ZoneBC_t", 1, "BC_t", BC, "end")` then
`cg_famname_read(buf)`. This succeeded for all four bocos.

---

## 5. Zone connectivity (CylinderB1 only)

**`cg_n1to1` returns 0 for both zones. `cg_nconns` returns 5 per zone (10
total).** NACA has no `ZoneGridConnectivity` at all (`cg_n1to1 = 0`,
`cg_nconns = 0`).

Every connectivity node reports
`GridConnectivityType = Abutting1to1`, `GridLocation = Vertex`,
`PointSetType = PointList`, donor `PointSetType = PointListDonor`,
and **no Transform** (the node is not a `GridConnectivity1to1_t`, so there is
no transform array to read).

Caveat on `GridLocation`, verified against the raw HDF5 tree: each of the ten
connectivity nodes has exactly four children -- ` data`,
`GridConnectivityType`, `PointList`, `PointListDonor`. **No `GridLocation`
node is stored.** The `Vertex` that `cg_conn_info` returns is the SIDS default
injected by the library. The conclusion (node indices) is unchanged and proven
independently below, but a reader bypassing the mid-level API must supply the
`Vertex` default on its own.

### Zone `dom-1` to donor `dom-2`

| conn | name | npnts | first 5 (PointList, PointListDonor) |
|---|---|---|---|
| 1 | `1to1Connection:con-2` | 21 | (114,1) (115,320) (116,319) (117,318) (118,317) |
| 2 | `1to1Connection:con-3` | 121 | (134,301) (135,300) (136,299) (137,298) (138,297) |
| 3 | `1to1Connection:con-4` | 41 | (254,181) (255,180) (256,179) (257,178) (258,177) |
| 4 | `1to1Connection:con-5` | 121 | (294,141) (295,140) (296,139) (297,138) (298,137) |
| 5 | `1to1Connection:con-6` | 21 | (414,21) (415,20) (416,19) (417,18) (418,17) |

### Zone `dom-2` to donor `dom-1`

| conn | name | npnts | first 5 (PointList, PointListDonor) |
|---|---|---|---|
| 1 | `1to1Connection:con-2` | 21 | (1,114) (320,115) (319,116) (318,117) (317,118) |
| 2 | `1to1Connection:con-3` | 121 | (301,134) (300,135) (299,136) (298,137) (297,138) |
| 3 | `1to1Connection:con-4` | 41 | (181,254) (180,255) (179,256) (178,257) (177,258) |
| 4 | `1to1Connection:con-5` | 121 | (141,294) (140,295) (139,296) (138,297) (137,298) |
| 5 | `1to1Connection:con-6` | 21 | (21,414) (20,415) (19,416) (18,417) (17,418) |

### Are the lists NODE or ELEMENT indices? NODE.

**They are 1-based zone-local NODE (vertex) indices.** Three independent
confirmations:

1. The API reports `GridLocation = Vertex` for all ten nodes.
   (Library-supplied SIDS default; the node itself is absent from the file.)
2. `npnts` is one greater than the matching section's element count
   (con-2: 21 points vs 20 BAR_2 elements; con-3: 121 vs 120; con-4: 41 vs 40),
   exactly the point count of an open polyline with that many segments.
3. Merging on that interpretation makes all 640 interface edges interior and
   yields exactly the expected 120 boundary edges (section 6). Any other
   interpretation fails this test.

The lists are **mirrored**: `dom-1`'s list for con-2 is `dom-2`'s donor list
and vice versa. Applying both directions gives 650 pairs but only 320 distinct
unions. Union-find makes the redundancy harmless; if you instead build an
explicit map, deduplicate or you will double-count.

The donor lists run in **reverse order** relative to the local list
(115 to 320, 116 to 319, ...), i.e. the two blocks traverse the shared
interface in opposite directions. Never assume index k pairs with index k;
always use the explicit pair (`PointList[k]`, `PointListDonor[k]`).

The 325 listed points per zone cover only **320 unique** node indices, because
adjacent `con-*` patches share corner nodes (index 134 ends con-2 and starts
con-3; index 114 appears in both con-2 and con-6). That is why 650 applied pairs
reduce to exactly 320 unions.

---

## 6. Correctness experiments

### CylinderB1: merge plus edge topology

| quantity | value |
|---|---|
| raw concatenated nodes | 10315 (`dom-1` offset 0, n 7079; `dom-2` offset 7079, n 3236) |
| connectivity pairs applied | 650 |
| effective unions | 320 |
| **unique merged nodes** | **9995** (320 collapsed) |
| **max distance between merged pairs** | **0.000000e+00** (exactly zero) |
| merged pairs differing by more than 1e-10 | **0** |
| 2-D cells | 10185 (TRI_3 500, QUAD_4 9685) |
| **unique edges** | **20180** |
| **edges shared by exactly 2 cells** | **20060** |
| **edges used by exactly 1 cell** | **120** |
| edges used by 3+ cells | 0 |

Boundary edge count **120 == WALL 100 + FAR 20**. Confirmed.

### NACA0012_H2: edge topology (single zone, no merging)

| quantity | value |
|---|---|
| nodes | 15682 (no merging; 0 collapsed) |
| 2-D cells | 20816 (TRI_3 10752, QUAD_4 10064) |
| **unique edges** | **36498** |
| **edges shared by exactly 2 cells** | **36014** |
| **edges used by exactly 1 cell** | **484** |
| edges used by 3+ cells | 0 |

Boundary edge count **484 == bc-2 80 + bc-4 404**. Confirmed.

### BC to boundary-edge cross-check

| mesh | zone | boco | family | nelem | found among 1-cell edges | missing |
|---|---|---|---|---|---|---|
| NACA | `dom-1` | `bc-2` | `bc-2` | 80 | 80 | 0 |
| NACA | `dom-1` | `bc-4` | `bc-4` | 404 | 404 | 0 |
| Cyl | `dom-1` | `WALL` | `WALL` | 100 | 100 | 0 |
| Cyl | `dom-2` | `FAR` | `FAR` | 20 | 20 | 0 |

Stronger than membership, the probe checks **set equality**:

| | NACA | Cylinder |
|---|---|---|
| distinct BC edges | 484 | 120 |
| distinct 1-cell edges | 484 | 120 |
| duplicate BC edges | 0 | 0 |
| in BC but not 1-cell | 0 | 0 |
| in 1-cell but not BC | 0 | 0 |
| **set equality** | **YES** | **YES** |

So every boundary edge is tagged by exactly one family, and no BC edge is
accidentally interior. A negative control (2000 quad diagonals probed against
the boundary-edge set) produced **0 false hits** in both meshes, confirming the
lookup discriminates rather than matching everything.

### Merge validation (the decisive test)

Interface (`con-*`) edges must be **interior** after a correct merge; before
merging each is a 1-cell edge in its own zone.

| mesh | con edges checked | now interior (2 cells) | still boundary | absent |
|---|---|---|---|---|
| NACA | 0 (none exist) | 0 | 0 | 0 |
| Cylinder | **640** | **640** | **0** | 0 |

All 640 interface edges closed. This is what proves the node-index
interpretation and the union-find are both right.

Also, quantizing all merged node coordinates to 1e-9 finds **0 positions
holding more than one distinct merged id** in either mesh (9995 distinct
positions for 9995 merged cylinder nodes; 15682 for 15682 NACA nodes). No
coincident nodes were left unmerged, and no distinct nodes were wrongly fused.

The exact `0.0` max merge distance is genuine: Pointwise wrote bit-identical
coordinates on both sides of the interface.

That zero was re-verified at the raw byte level, independently of the CGNS API:
all 650 interface pairs are bit-identical in X and Y (e.g. `4024000000000000`
on both sides for x = 10), and the resulting distance carries IEEE-754 pattern
`0000000000000000`. The two sides come from separate coordinate arrays (7079
and 3236 doubles, matching each zone's NVertex), so this is not a
self-comparison artifact.

### Winding and signed area (shoelace on stored node order)

| | NACA | Cylinder |
|---|---|---|
| first 5 TRI_3 signed areas | +4.279039e-07 +4.960116e-07 +5.095618e-07 +5.247693e-07 +5.095619e-07 | +4.337764e-03 +1.811317e-03 +1.727465e-03 +5.077863e-03 +4.273515e-03 |
| first 5 QUAD_4 signed areas | +7.755268e-07 +4.589965e-05 +1.487013e-05 +2.501693e-09 +3.276857e-09 | +6.294793e-05 +6.294598e-05 +7.586702e-05 +6.294598e-05 +7.586702e-05 |
| **cells with negative signed area** | **0 / 20816** | **0 / 10185** |
| cells with exactly zero area | 0 | 0 |
| smallest absolute area | 1.476131e-09 | 6.294598e-05 |
| sum of signed areas | +2.008545e+04 | +1.236060e+05 |

**Stored node ordering is counter-clockwise (positive signed area) for every
cell in both meshes.** You can compute outward normals directly from the stored
order without a per-cell sign correction. Still worth asserting `area > 0` at
read time so a future mesh cannot silently break the assumption.

Validation that the quad ordering is a true perimeter walk (a shoelace can be
positive even for a mis-ordered 1-2-4-3 quad):

| | NACA | Cylinder |
|---|---|---|
| QUAD_4 cells | 10064 | 9685 |
| stored diagonals that are also mesh edges | 0 | 0 |
| shoelace vs triangle-split mismatch (>1e-9 rel) | 0 (worst 1.083e-10) | 0 (worst 3.333e-13) |
| quads with non-positive sub-triangle (concave/bowtie) | 0 | 0 |

All quads are strictly convex, CCW, and stored in perimeter order. NACA's worst
relative mismatch of 1.08e-10 is float round-off on its extremely thin
boundary-layer cells (smallest area 1.5e-09), not a topology problem.

### Degeneracy

Self-loop edges (a == b): **0** in both. Cells with duplicate node ids:
**0** in both.

---

## 7. Boundary geometry: which family is the body

| mesh | zone | boco | family | nelem | nnodes | x range | y range | r min | r max |
|---|---|---|---|---|---|---|---|---|---|
| NACA | `dom-1` | `bc-2` | `bc-2` | 80 | 80 | [-80, 80] | [-80, 80] | **80** | **80** |
| NACA | `dom-1` | `bc-4` | `bc-4` | 404 | 404 | [0, 1.00534] | [-0.0600048, 0.0600048] | 0 | 1.00534 |
| Cyl | `dom-1` | `WALL` | `WALL` | 100 | 100 | [-0.5, 0.5] | [-0.5, 0.5] | **0.5** | **0.5** |
| Cyl | `dom-2` | `FAR` | `FAR` | 20 | 20 | [-200, 200] | [-200, 200] | **200** | **200** |

**CylinderB1**: `WALL` is the body -- every one of its 100 nodes sits at
radius exactly 0.5 (min = max = 0.5), so the cylinder has **radius 0.5,
diameter 1.0**, centred on the origin. `FAR` is the farfield at radius exactly
200, a 20-segment polygonal outer boundary, giving a farfield-to-body ratio
of 400.

**NACA0012_H2**: `bc-4` is the **airfoil body** -- 404 nodes spanning
x in [0, 1.00534] with y in [-0.0600048, 0.0600048]. That is a unit chord (the
1.00534 slight overshoot is the blunt/rounded trailing-edge treatment) and a
thickness of 0.120 = 12% of chord, exactly the NACA **0012** thickness.
`bc-2` is the farfield: 80 nodes at radius exactly 80, i.e. 80 chords out.

Both node counts equal their element counts (80/80, 404/404, 100/100, 20/20),
confirming each boundary family forms a **single closed loop**.

---

## 8. Recommended reader algorithm

```
cg_open(path, CG_MODE_READ, &fn)
cg_nbases -> for each base:
  cg_base_read -> cell_dim, phys_dim        # phys_dim is 2 or 3, don't assume
  cg_nfamilies / cg_family_read             # collect family names
  cg_nzones -> for each zone:
    cg_zone_read -> nnode = size[0], ncell = size[1]
    cg_zone_type                            # expect Unstructured
    cg_ncoords / cg_coord_info / cg_coord_read(RealDouble, 1..nnode)
    cg_nsections -> for each section:
      cg_section_read -> name, type, start, end, nbndry, parent_flag
      cg_ElementDataSize; cg_npe(type)
      cg_elements_read                      # fixed stride; no poly needed
      classify: TRI_3/QUAD_4 -> volume cell; BAR_2 -> 1-D candidate
    cg_nbocos -> for each boco:
      cg_boco_info -> name, bctype, ptset(PointRange), npnts(2)
      cg_boco_read -> pnts[0..1]            # ELEMENT indices
      cg_boco_gridlocation_read             # EdgeCenter
      cg_goto(...BC_t...) + cg_famname_read # the real BC identity
      resolve section by range containment; tag its edges with the family
    cg_n1to1 -> if 0, fall back to:
    cg_nconns -> for each conn:
      cg_conn_info -> location(Vertex), gctype(Abutting1to1), npnts, donorname
      cg_conn_read -> pnts[], donor_pnts[]  # 1-based zone-local NODE ids
  # after all zones: offset-concatenate nodes, union-find over every
  # (pnts[k], donor_pnts[k]) pair, compact to merged ids, then build edges.
```

Assertions worth keeping in the production reader, all of which hold here:

- `ElementDataSize == nelem * npe` for every section (no MIXED/NGON).
- `nbndry == 0` and `parent_flag == 0` (no ParentData to honour).
- every volume cell has strictly positive signed area (CCW).
- no edge is shared by more than 2 cells.
- boundary (1-cell) edge count equals the total BC element count.
- every `con-*` interface edge becomes a 2-cell edge after merging.
- merged node pairs coincide geometrically (tolerance 1e-10; actual 0.0).

### Silent-failure warning

If you mistakenly treat a BC `PointRange` as *node* indices, Cylinder
`dom-1/WALL` (range 6902..7001) still falls **below** that zone's NVertex of
7079, so the misread indexes valid-looking nodes and yields garbage with **no
out-of-bounds error**. NACA's ranges (20817+ against 15682 nodes) would at least
trip a bounds check. Honour `GridLocation = EdgeCenter` explicitly rather than
relying on a range check to catch the mistake.

---

## 9. Reproducing

```
cd /workspace/solver/tools
./build_probe.sh                     # both benchmark meshes
./build_probe.sh /path/to/other.cgns # any other 2-D CGNS mesh
```

Writes `probe_out/raw_report.md`. The probe is read-only with respect to the
mesh files and has no dependency on the solver.
