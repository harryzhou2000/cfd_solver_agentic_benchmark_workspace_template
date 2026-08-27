# CGNS mesh probe report

- probe built against CGNS 4.50, sizeof(cgsize_t) = 8 bytes

---

## /workspace/cfd_solver_agentic_benchmark/inputs/meshes/NACA0012_H2.cgns

- CGNS file version: 3.310, file precision: 64-bit

- nbases: 1

### Base

- name: `Base`
- CellDimension: 2
- PhysicalDimension: 3
- nzones: 1

### Families (base level)

| # | FamilyName | FamilyBC |
|---|---|---|
| 1 | `Unspecified` | (none) |
| 2 | `bc-2` | FamBC=Null |
| 3 | `bc-4` | FamBC=Null |

### Zone 1: `dom-1`

- ZoneType: Unstructured
- nodes (NVertex): 15682
- cells (NCell): 20816
- zone FamilyName: `Unspecified`
- ncoords: 3

| coord | DataType | min | max |
|---|---|---|---|
| `CoordinateX` | RealDouble | -80 | 80 |
| `CoordinateY` | RealDouble | -80 | 80 |
| `CoordinateZ` | RealDouble | -3.46448249171e-06 | 0 |

#### Sections

| idx | name | ElementType | start | end | nelem | npe | ElementDataSize | nbndry | parent_flag | referenced by |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | `TriElements` | TRI_3 | 1 | 10752 | 10752 | 3 | 32256 | 0 | 0 | - |
| 2 | `QuadElements` | QUAD_4 | 10753 | 20816 | 10064 | 4 | 40256 | 0 | 0 | - |
| 3 | `bc-2` | BAR_2 | 20817 | 20896 | 80 | 2 | 160 | 0 | 0 | BC `bc-2` (family `bc-2`) |
| 4 | `bc-4` | BAR_2 | 20897 | 21300 | 404 | 2 | 808 | 0 | 0 | BC `bc-4` (family `bc-4`) |

#### Boundary conditions (2)

- boco 1 `bc-2`
  - BCType: FamilySpecified
  - GridLocation: EdgeCenter
  - PointSetType: PointRange
  - npnts: 2
  - FamilyName: `bc-2`
  - values: 20817 20896
  - maps to section `bc-2` (BAR_2, range 20817..20896): element-range containment, 80 elements
- boco 2 `bc-4`
  - BCType: FamilySpecified
  - GridLocation: EdgeCenter
  - PointSetType: PointRange
  - npnts: 2
  - FamilyName: `bc-4`
  - values: 20897 21300
  - maps to section `bc-4` (BAR_2, range 20897..21300): element-range containment, 404 elements

#### Zone connectivity (0) [API: cg_n1to1 total=0, cg_nconns total=0 -> using none]

(none)

### Experiment: node merging, edge topology, winding

- concatenated raw global nodes: 15682 (`dom-1` offset 0, n 15682)
- connectivity node pairs applied: 0 (effective unions: 0)
- max distance between merged node pairs: 0.000000e+00
- merged pairs with coordinate mismatch > 1e-10: 0
- **unique merged nodes: 15682** (from 15682 raw, 0 collapsed)
- 2-D cells scanned: 20816 (TRI_3 10752, QUAD_4 10064)
- **unique edges: 36498**
- edges shared by exactly 2 cells (interior): **36014**
- edges used by exactly 1 cell (boundary): **484**
- edges used by 3+ cells (non-manifold): 0 (good)
- Euler check: unique = 1-cell + 2-cell + 3+ => 36498 == 36498 OK

#### Winding / signed area (shoelace on stored node order)

- first 5 TRI_3 signed areas: +4.279039e-07 +4.960116e-07 +5.095618e-07 +5.247693e-07 +5.095619e-07
- first 5 QUAD_4 signed areas: +7.755268e-07 +4.589965e-05 +1.487013e-05 +2.501693e-09 +3.276857e-09
- cells with negative signed area: **0 / 20816**
- cells with exactly zero area: 0
- smallest |area|: 1.476131e-09
- sum of signed areas: +2.008545e+04
- conclusion: stored ordering is **counter-clockwise (all positive)**

#### Boundary edge <-> BC section cross-check

| zone | boco | family | section | nelem | found among 1-cell edges | missing |
|---|---|---|---|---|---|---|
| `dom-1` | `bc-2` | `bc-2` | `bc-2` | 80 | 80 | 0 |
| `dom-1` | `bc-4` | `bc-4` | `bc-4` | 404 | 404 | 0 |

- total BC boundary elements: **484**
- 1-cell (boundary) edges from topology: **484**
- counts match: **YES**
- all BC elements found among 1-cell edges: **YES** (found 484, missing 0)
- distinct BC edges: 484; distinct 1-cell edges: 484; duplicate BC edges: 0
- in BC but not 1-cell: 0; in 1-cell but not BC: 0
- **SET EQUALITY (BC edges == boundary edges): YES -- every boundary edge is tagged by exactly one BC family, and no BC edge is interior**
- NEGATIVE CONTROL (2000 cell diagonals probed against the boundary-edge set): 0 false hits -- lookup is **discriminating (non-boundary edges are correctly rejected)**

#### Merge validation: connectivity sections must be interior

- connectivity (con-*) edges checked: 0
- ... now interior (2 cells): **0**
- ... still boundary (1 cell): **0** (good -- merge closed the interface)
- ... not present in edge map at all: 0

#### Merge validation: unmerged coincident nodes

- distinct quantized positions: 15682 (merged nodes: 15682)
- positions holding >1 distinct merged id (missed merges): **0** (worst multiplicity 0) (good)

#### Winding validation: quad node order is perimeter order

- QUAD_4 cells: 10064
- stored diagonals that are also mesh edges: 0 (good -- stored order is the perimeter)
- shoelace vs triangle-split area mismatch (>1e-9 rel): 0 (worst rel 1.083e-10)
- quads with a non-positive sub-triangle (concave/bowtie): 0 (all strictly convex CCW)

#### Degeneracy checks

- self-loop edges (a==b): 0
- cells with duplicate node ids: 0

#### Boundary geometry by family

| zone | boco | family | nelem | nnodes | x range | y range | r=|p| min | r max |
|---|---|---|---|---|---|---|---|---|
| `dom-1` | `bc-2` | `bc-2` | 80 | 80 | [-80, 80] | [-80, 80] | 80 | 80 |
| `dom-1` | `bc-4` | `bc-4` | 404 | 404 | [0, 1.00534] | [-0.0600048, 0.0600048] | 0 | 1.00534 |


---

## /workspace/cfd_solver_agentic_benchmark/inputs/meshes/CylinderB1.cgns

- CGNS file version: 3.310, file precision: 64-bit

- nbases: 1

### Base

- name: `Base`
- CellDimension: 2
- PhysicalDimension: 2
- nzones: 2

### Families (base level)

| # | FamilyName | FamilyBC |
|---|---|---|
| 1 | `Unspecified` | (none) |
| 2 | `WALL` | FamBC=Null |
| 3 | `FAR` | FamBC=Null |

### Zone 1: `dom-1`

- ZoneType: Unstructured
- nodes (NVertex): 7079
- cells (NCell): 6901
- zone FamilyName: `Unspecified`
- ncoords: 2

| coord | DataType | min | max |
|---|---|---|---|
| `CoordinateX` | RealDouble | -2 | 10 |
| `CoordinateY` | RealDouble | -2 | 2 |

#### Sections

| idx | name | ElementType | start | end | nelem | npe | ElementDataSize | nbndry | parent_flag | referenced by |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | `TriElements` | TRI_3 | 1 | 64 | 64 | 3 | 192 | 0 | 0 | - |
| 2 | `QuadElements` | QUAD_4 | 65 | 6901 | 6837 | 4 | 27348 | 0 | 0 | - |
| 3 | `WALL` | BAR_2 | 6902 | 7001 | 100 | 2 | 200 | 0 | 0 | BC `WALL` (family `WALL`) |
| 4 | `con-2` | BAR_2 | 7002 | 7021 | 20 | 2 | 40 | 0 | 0 | zone connectivity `1to1Connection:con-2` -> `dom-2` |
| 5 | `con-3` | BAR_2 | 7022 | 7141 | 120 | 2 | 240 | 0 | 0 | zone connectivity `1to1Connection:con-3` -> `dom-2` |
| 6 | `con-4` | BAR_2 | 7142 | 7181 | 40 | 2 | 80 | 0 | 0 | zone connectivity `1to1Connection:con-4` -> `dom-2` |
| 7 | `con-5` | BAR_2 | 7182 | 7301 | 120 | 2 | 240 | 0 | 0 | zone connectivity `1to1Connection:con-5` -> `dom-2` |
| 8 | `con-6` | BAR_2 | 7302 | 7321 | 20 | 2 | 40 | 0 | 0 | zone connectivity `1to1Connection:con-6` -> `dom-2` |

#### Boundary conditions (1)

- boco 1 `WALL`
  - BCType: FamilySpecified
  - GridLocation: EdgeCenter
  - PointSetType: PointRange
  - npnts: 2
  - FamilyName: `WALL`
  - values: 6902 7001
  - maps to section `WALL` (BAR_2, range 6902..7001): element-range containment, 100 elements

#### Zone connectivity (5) [API: cg_n1to1 total=0, cg_nconns total=10 -> using cg_conn_read]

- conn 1 `1to1Connection:con-2`: this zone `dom-1` -> donor `dom-2`
  - read via: cg_conn_read
  - GridConnectivityType: Abutting1to1
  - GridLocation: Vertex
  - PointSetType: PointList / donor PointListDonor
  - Transform: (absent -- not a 1to1 node)
  - npnts: 21 (donor 21)
  - first 5 (PointList, PointListDonor) pairs: (114,1) (115,320) (116,319) (117,318) (118,317)
- conn 2 `1to1Connection:con-3`: this zone `dom-1` -> donor `dom-2`
  - read via: cg_conn_read
  - GridConnectivityType: Abutting1to1
  - GridLocation: Vertex
  - PointSetType: PointList / donor PointListDonor
  - Transform: (absent -- not a 1to1 node)
  - npnts: 121 (donor 121)
  - first 5 (PointList, PointListDonor) pairs: (134,301) (135,300) (136,299) (137,298) (138,297)
- conn 3 `1to1Connection:con-4`: this zone `dom-1` -> donor `dom-2`
  - read via: cg_conn_read
  - GridConnectivityType: Abutting1to1
  - GridLocation: Vertex
  - PointSetType: PointList / donor PointListDonor
  - Transform: (absent -- not a 1to1 node)
  - npnts: 41 (donor 41)
  - first 5 (PointList, PointListDonor) pairs: (254,181) (255,180) (256,179) (257,178) (258,177)
- conn 4 `1to1Connection:con-5`: this zone `dom-1` -> donor `dom-2`
  - read via: cg_conn_read
  - GridConnectivityType: Abutting1to1
  - GridLocation: Vertex
  - PointSetType: PointList / donor PointListDonor
  - Transform: (absent -- not a 1to1 node)
  - npnts: 121 (donor 121)
  - first 5 (PointList, PointListDonor) pairs: (294,141) (295,140) (296,139) (297,138) (298,137)
- conn 5 `1to1Connection:con-6`: this zone `dom-1` -> donor `dom-2`
  - read via: cg_conn_read
  - GridConnectivityType: Abutting1to1
  - GridLocation: Vertex
  - PointSetType: PointList / donor PointListDonor
  - Transform: (absent -- not a 1to1 node)
  - npnts: 21 (donor 21)
  - first 5 (PointList, PointListDonor) pairs: (414,21) (415,20) (416,19) (417,18) (418,17)

### Zone 2: `dom-2`

- ZoneType: Unstructured
- nodes (NVertex): 3236
- cells (NCell): 3284
- zone FamilyName: `Unspecified`
- ncoords: 2

| coord | DataType | min | max |
|---|---|---|---|
| `CoordinateX` | RealDouble | -200 | 200 |
| `CoordinateY` | RealDouble | -200 | 200 |

#### Sections

| idx | name | ElementType | start | end | nelem | npe | ElementDataSize | nbndry | parent_flag | referenced by |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | `TriElements` | TRI_3 | 1 | 436 | 436 | 3 | 1308 | 0 | 0 | - |
| 2 | `QuadElements` | QUAD_4 | 437 | 3284 | 2848 | 4 | 11392 | 0 | 0 | - |
| 3 | `FAR` | BAR_2 | 3285 | 3304 | 20 | 2 | 40 | 0 | 0 | BC `FAR` (family `FAR`) |
| 4 | `con-2` | BAR_2 | 3305 | 3324 | 20 | 2 | 40 | 0 | 0 | zone connectivity `1to1Connection:con-2` -> `dom-1` |
| 5 | `con-3` | BAR_2 | 3325 | 3444 | 120 | 2 | 240 | 0 | 0 | zone connectivity `1to1Connection:con-3` -> `dom-1` |
| 6 | `con-4` | BAR_2 | 3445 | 3484 | 40 | 2 | 80 | 0 | 0 | zone connectivity `1to1Connection:con-4` -> `dom-1` |
| 7 | `con-5` | BAR_2 | 3485 | 3604 | 120 | 2 | 240 | 0 | 0 | zone connectivity `1to1Connection:con-5` -> `dom-1` |
| 8 | `con-6` | BAR_2 | 3605 | 3624 | 20 | 2 | 40 | 0 | 0 | zone connectivity `1to1Connection:con-6` -> `dom-1` |

#### Boundary conditions (1)

- boco 1 `FAR`
  - BCType: FamilySpecified
  - GridLocation: EdgeCenter
  - PointSetType: PointRange
  - npnts: 2
  - FamilyName: `FAR`
  - values: 3285 3304
  - maps to section `FAR` (BAR_2, range 3285..3304): element-range containment, 20 elements

#### Zone connectivity (5) [API: cg_n1to1 total=0, cg_nconns total=10 -> using cg_conn_read]

- conn 1 `1to1Connection:con-2`: this zone `dom-2` -> donor `dom-1`
  - read via: cg_conn_read
  - GridConnectivityType: Abutting1to1
  - GridLocation: Vertex
  - PointSetType: PointList / donor PointListDonor
  - Transform: (absent -- not a 1to1 node)
  - npnts: 21 (donor 21)
  - first 5 (PointList, PointListDonor) pairs: (1,114) (320,115) (319,116) (318,117) (317,118)
- conn 2 `1to1Connection:con-3`: this zone `dom-2` -> donor `dom-1`
  - read via: cg_conn_read
  - GridConnectivityType: Abutting1to1
  - GridLocation: Vertex
  - PointSetType: PointList / donor PointListDonor
  - Transform: (absent -- not a 1to1 node)
  - npnts: 121 (donor 121)
  - first 5 (PointList, PointListDonor) pairs: (301,134) (300,135) (299,136) (298,137) (297,138)
- conn 3 `1to1Connection:con-4`: this zone `dom-2` -> donor `dom-1`
  - read via: cg_conn_read
  - GridConnectivityType: Abutting1to1
  - GridLocation: Vertex
  - PointSetType: PointList / donor PointListDonor
  - Transform: (absent -- not a 1to1 node)
  - npnts: 41 (donor 41)
  - first 5 (PointList, PointListDonor) pairs: (181,254) (180,255) (179,256) (178,257) (177,258)
- conn 4 `1to1Connection:con-5`: this zone `dom-2` -> donor `dom-1`
  - read via: cg_conn_read
  - GridConnectivityType: Abutting1to1
  - GridLocation: Vertex
  - PointSetType: PointList / donor PointListDonor
  - Transform: (absent -- not a 1to1 node)
  - npnts: 121 (donor 121)
  - first 5 (PointList, PointListDonor) pairs: (141,294) (140,295) (139,296) (138,297) (137,298)
- conn 5 `1to1Connection:con-6`: this zone `dom-2` -> donor `dom-1`
  - read via: cg_conn_read
  - GridConnectivityType: Abutting1to1
  - GridLocation: Vertex
  - PointSetType: PointList / donor PointListDonor
  - Transform: (absent -- not a 1to1 node)
  - npnts: 21 (donor 21)
  - first 5 (PointList, PointListDonor) pairs: (21,414) (20,415) (19,416) (18,417) (17,418)

### Experiment: node merging, edge topology, winding

- concatenated raw global nodes: 10315 (`dom-1` offset 0, n 7079) (`dom-2` offset 7079, n 3236)
- connectivity node pairs applied: 650 (effective unions: 320)
- max distance between merged node pairs: 0.000000e+00
- merged pairs with coordinate mismatch > 1e-10: 0
- **unique merged nodes: 9995** (from 10315 raw, 320 collapsed)
- 2-D cells scanned: 10185 (TRI_3 500, QUAD_4 9685)
- **unique edges: 20180**
- edges shared by exactly 2 cells (interior): **20060**
- edges used by exactly 1 cell (boundary): **120**
- edges used by 3+ cells (non-manifold): 0 (good)
- Euler check: unique = 1-cell + 2-cell + 3+ => 20180 == 20180 OK

#### Winding / signed area (shoelace on stored node order)

- first 5 TRI_3 signed areas: +4.337764e-03 +1.811317e-03 +1.727465e-03 +5.077863e-03 +4.273515e-03
- first 5 QUAD_4 signed areas: +6.294793e-05 +6.294598e-05 +7.586702e-05 +6.294598e-05 +7.586702e-05
- cells with negative signed area: **0 / 10185**
- cells with exactly zero area: 0
- smallest |area|: 6.294598e-05
- sum of signed areas: +1.236060e+05
- conclusion: stored ordering is **counter-clockwise (all positive)**

#### Boundary edge <-> BC section cross-check

| zone | boco | family | section | nelem | found among 1-cell edges | missing |
|---|---|---|---|---|---|---|
| `dom-1` | `WALL` | `WALL` | `WALL` | 100 | 100 | 0 |
| `dom-2` | `FAR` | `FAR` | `FAR` | 20 | 20 | 0 |

- total BC boundary elements: **120**
- 1-cell (boundary) edges from topology: **120**
- counts match: **YES**
- all BC elements found among 1-cell edges: **YES** (found 120, missing 0)
- distinct BC edges: 120; distinct 1-cell edges: 120; duplicate BC edges: 0
- in BC but not 1-cell: 0; in 1-cell but not BC: 0
- **SET EQUALITY (BC edges == boundary edges): YES -- every boundary edge is tagged by exactly one BC family, and no BC edge is interior**
- NEGATIVE CONTROL (2000 cell diagonals probed against the boundary-edge set): 0 false hits -- lookup is **discriminating (non-boundary edges are correctly rejected)**

#### Merge validation: connectivity sections must be interior

- connectivity (con-*) edges checked: 640
- ... now interior (2 cells): **640**
- ... still boundary (1 cell): **0** (good -- merge closed the interface)
- ... not present in edge map at all: 0

#### Merge validation: unmerged coincident nodes

- distinct quantized positions: 9995 (merged nodes: 9995)
- positions holding >1 distinct merged id (missed merges): **0** (worst multiplicity 0) (good)

#### Winding validation: quad node order is perimeter order

- QUAD_4 cells: 9685
- stored diagonals that are also mesh edges: 0 (good -- stored order is the perimeter)
- shoelace vs triangle-split area mismatch (>1e-9 rel): 0 (worst rel 3.333e-13)
- quads with a non-positive sub-triangle (concave/bowtie): 0 (all strictly convex CCW)

#### Degeneracy checks

- self-loop edges (a==b): 0
- cells with duplicate node ids: 0

#### Boundary geometry by family

| zone | boco | family | nelem | nnodes | x range | y range | r=|p| min | r max |
|---|---|---|---|---|---|---|---|---|
| `dom-1` | `WALL` | `WALL` | 100 | 100 | [-0.5, 0.5] | [-0.5, 0.5] | 0.5 | 0.5 |
| `dom-2` | `FAR` | `FAR` | 20 | 20 | [-200, 200] | [-200, 200] | 200 | 200 |

