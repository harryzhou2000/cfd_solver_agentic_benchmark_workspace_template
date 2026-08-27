import re

with open('/workspace/solver/src/Residual.cpp', 'r') as f:
    src = f.read()

old = (
    '        // Inviscid flux
'
    '        StateVec flux = rusanov_flux(U_int, U_ghost, nx, ny, gamma, diss_scale);'
)
new = (
    '        // Inviscid flux
'
    '        StateVec flux;
'
    '        if (bc == BcType::NoSlipAdiabaticWall) {
'
    '            // Pressure-only flux {0, p*nx, p*ny, 0} for no-slip wall.
'
    '            // Avoids excessive Rusanov dissipation from full-velocity ghost negation
'
    '            // which destabilizes low-Mach viscous flows.
'
    '            double p_wall = pressure(U_int, gamma);
'
    '            flux = {0.0, p_wall * nx, p_wall * ny, 0.0};
'
    '        } else {
'
    '            flux = rusanov_flux(U_int, U_ghost, nx, ny, gamma, diss_scale);
'
    '        }'
)

assert old in src, "Pattern not found"
src = src.replace(old, new, 1)

with open('/workspace/solver/src/Residual.cpp', 'w') as f:
    f.write(src)
print("SUCCESS")
