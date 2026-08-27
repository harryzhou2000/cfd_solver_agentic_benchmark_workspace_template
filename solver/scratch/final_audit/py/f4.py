
import os, re
rep='/workspace/solver/report'
pat=re.compile(r'\\cnsfig\{([^}]+)\}')
used=set()
for f in os.listdir(rep):
    if f.endswith('.tex'):
        for line in open(os.path.join(rep,f),errors='ignore'):
            for m in pat.finditer(line): used.add(m.group(1))
cases=['naca0012_m015_inviscid','naca0012_m080_inviscid','naca0012_m200_inviscid',
'naca0012_m015_laminar_re5000','naca0012_m080_laminar_re5000','naca0012_m200_laminar_re5000',
'cylinder_m010_laminar_re20','cylinder_m010_laminar_re200']
print("OUTPUT_CONTRACT required visualizations, per case (DISPLAYED in report):")
print(f"{'case':32s} {'resid':>6s} {'force':>6s} {'cp':>4s} {'mach':>5s} {'press':>6s}")
allok=True
for c in cases:
    r='residuals' ; f='forces'
    have=lambda s: ('YES' if f"{c}_{s}.png" in used else 'NO')
    row=[have('residuals'),have('forces'),have('cp'),have('mach'),have('pressure')]
    if 'NO' in row: allok=False
    print(f"{c:32s} {row[0]:>6s} {row[1]:>6s} {row[2]:>4s} {row[3]:>5s} {row[4]:>6s}")
print("all 5 mandatory per-case figure types displayed for all 8 cases:",allok)
print()
print("cylinder wake / Re200 vortex street:")
for s in ['cylinder_m010_laminar_re200_vorticity.png','cylinder_m010_laminar_re200_velocity.png','cylinder_m010_laminar_re200_spectrum.png','cylinder_m010_laminar_re20_velocity.png']:
    print("  ",s,"displayed" if s in used else "NOT displayed")

