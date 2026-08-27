import sys

with open('SteadySolver.cpp', 'r') as f:
    content = f.read()

# Fix 2a: Add low_mach_viscous_case and update first_order_steps
old = (
    '    const bool viscous_shock_case   = (mu > 0.0) && (cfg.freestream.mach >= 0.5) && !low_re_viscous_case;
'
    '    const bool any_shock_case = inviscid_shock_case || viscous_shock_case;
'
    '    const int first_order_steps = (mu > 0.0)
'
    '        ? (low_re_viscous_case ? std::max(ramp_steps, 5000) :
'
    '           viscous_shock_case  ? std::max(ramp_steps, 5000) : 500)
'
    '        : (inviscid_shock_case ? std::max(ramp_steps, 5000) : 500);
'
    '    // Gradual 2nd-order limiter ramp (longer for shock/low-Re cases to avoid abrupt activation)
'
    '    const int second_order_ramp_steps = (any_shock_case || low_re_viscous_case) ? 2000 : 300;
'
)
new = (
    '    const bool viscous_shock_case   = (mu > 0.0) && (cfg.freestream.mach >= 0.5) && !low_re_viscous_case;
'
    '    // Fix T: low-Mach viscous (M<0.3, Re>=100) needs extended 1st-order startup and CFL reset
'
    '    // at 2nd-order transition to prevent CFL sticking and reconstruction instability.
'
    '    const bool low_mach_viscous_case = (mu > 0.0) && (cfg.freestream.mach < 0.3) && !low_re_viscous_case;
'
    '    const bool any_shock_case = inviscid_shock_case || viscous_shock_case;
'
    '    const int first_order_steps = (mu > 0.0)
'
    '        ? (low_re_viscous_case ? ramp_steps :  // Re<100: use case-spec ramp (no 5000 floor)
'
    '           (viscous_shock_case || low_mach_viscous_case) ? std::max(ramp_steps, 5000) : 500)
'
    '        : (inviscid_shock_case ? std::max(ramp_steps, 5000) : 500);
'
    '    // Gradual 2nd-order limiter ramp (longer for shock/low-Re/low-Mach cases)
'
    '    const int second_order_ramp_steps = (any_shock_case || low_re_viscous_case || low_mach_viscous_case) ? 2000 : 300;
'
)
if old in content:
    content = content.replace(old, new)
    print("Fix 2 (low_mach_viscous + first_order_steps): OK")
else:
    print("Fix 2: NOT FOUND")
    sys.exit(1)

# Fix 3: Remove !in_2nd_order_ramp from Build 6 condition
old3 = '        if (!in_2nd_order_ramp && step > first_order_steps && outer_res_before > 0 && outer_res_norm > 1.1 * outer_res_before) {
'
new3 = '        if (step > first_order_steps && outer_res_before > 0 && outer_res_norm > 1.1 * outer_res_before) {
'
if old3 in content:
    content = content.replace(old3, new3)
    print("Fix 3 (Build 6 in_2nd_order_ramp removed): OK")
else:
    print("Fix 3: NOT FOUND")

# Fix 4: Add low_mach_viscous_case to Fix G
old4 = '            if (inviscid_shock_case || viscous_shock_case) {
'
new4 = '            if (inviscid_shock_case || viscous_shock_case || low_mach_viscous_case) {
'
if old4 in content:
    content = content.replace(old4, new4)
    print("Fix 4 (Fix G for low_mach_viscous): OK")
else:
    print("Fix 4: NOT FOUND")

# Fix 5: Relax Fix E conditions (CFL recovery)
old5 = (
    '        } else if (inner_count < max_inner / 2 &&
'
    '                   (outer_res_decreased ||
'
    '                    (mu > 0.0 && outer_res_norm <= 1.5 * min_outer_res_ever))) {
'
)
new5 = (
    '        } else if (inner_count < max_inner * 3 / 4 &&
'
    '                   (outer_res_decreased ||
'
    '                    (mu > 0.0 && outer_res_norm <= 2.5 * min_outer_res_ever))) {
'
)
if old5 in content:
    content = content.replace(old5, new5)
    print("Fix 5 (Relax Fix E for CFL recovery): OK")
else:
    print("Fix 5: NOT FOUND")

with open('SteadySolver.cpp', 'w') as f:
    f.write(content)
print("All fixes written.")
