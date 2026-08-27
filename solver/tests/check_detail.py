import csv
# NACA surface: both sides (upper y>0 and lower y<0)
su=list(csv.DictReader(open("results/naca0012_m015_inviscid/surface.csv")))
ys=[float(r["y"]) for r in su]; xs=[float(r["x"]) for r in su]
up=sum(1 for y in ys if y>0.001); lo=sum(1 for y in ys if y<-0.001)
print("NACA surface pts:", len(su), "upper:", up, "lower:", lo, "x-range", round(min(xs),3), round(max(xs),3))
# field_final.vtk fields
hdr=open("results/naca0012_m015_inviscid/field_final.vtk").read(4000)
import re
print("== field_final.vtk SCALARS/FIELDS ==")
for m in re.findall(r"SCALARS (\w+)|FIELD \w+ (\w+)|VECTORS (\w+)", hdr):
    print(" ", [x for x in m if x])
# check field data names by scanning
names=re.findall(r"SCALARS\s+(\S+)", hdr)
print("scalar fields:", names)
