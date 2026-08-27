import sys

with open('/workspace/solver/src/SteadySolver.cpp', 'r') as f:
    content = f.read()

fixes_done = 0

# Fix 2: Add low_mach_viscous_case after viscous_shock_case definition
needle = 'const bool viscous_shock_case   = (mu > 0.0) && (cfg.freestream.mach >= 0.5) && !low_re_viscous_case;'
if needle in content:
    replacement = needle + '\n    // Fix T: low-Mach viscous (M<0.3, Re>=100) needs extended startup like shock cases\n    const bool low_mach_viscous_case = (mu > 0.0) && (cfg.freestream.mach < 0.3) && !low_re_viscous_case;'
    content = content.replace(needle, replacement, 1)
    print('F2a OK')
    fixes_done += 1
else:
    print('F2a NOT FOUND')

# Fix 2b: Update any_shock_case to include low_mach_viscous_case? No, keep it separate
# Fix 2c: Update first_order_steps for low_re_viscous_case (use ramp_steps, not max with 5000)
needle2 = 'low_re_viscous_case ? std::max(ramp_steps, 5000) :'
if needle2 in content:
    content = content.replace(needle2, 'low_re_viscous_case ? ramp_steps :', 1)
    print('F2c (Re<100 first_order_steps = ramp_steps): OK')
    fixes_done += 1
else:
    print('F2c NOT FOUND')

# Fix 2d: Update first_order_steps to include low_mach_viscous_case
needle3 = 'viscous_shock_case  ? std::max(ramp_steps, 5000) : 500)'
if needle3 in content:
    content = content.replace(needle3, '(viscous_shock_case || low_mach_viscous_case) ? std::max(ramp_steps, 5000) : 500)', 1)
    print('F2d (low_mach gets 5000 first_order): OK')
    fixes_done += 1
else:
    print('F2d NOT FOUND')

# Fix 2e: Update second_order_ramp_steps
needle4 = '(any_shock_case || low_re_viscous_case) ? 2000 : 300;'
if needle4 in content:
    content = content.replace(needle4, '(any_shock_case || low_re_viscous_case || low_mach_viscous_case) ? 2000 : 300;', 1)
    print('F2e (second_order_ramp_steps): OK')
    fixes_done += 1
else:
    print('F2e NOT FOUND')

# Fix 3: Remove in_2nd_order_ramp from Build 6
needle5 = '!in_2nd_order_ramp && step > first_order_steps && outer_res_before > 0 && outer_res_norm > 1.1 * outer_res_before'
if needle5 in content:
    content = content.replace(needle5, 'step > first_order_steps && outer_res_before > 0 && outer_res_norm > 1.1 * outer_res_before', 1)
    print('F3 (Build 6 no in_2nd_order_ramp): OK')
    fixes_done += 1
else:
    print('F3 NOT FOUND')

# Fix 4: Fix G for low_mach_viscous_case
needle6 = 'if (inviscid_shock_case || viscous_shock_case) {'
if needle6 in content:
    content = content.replace(needle6, 'if (inviscid_shock_case || viscous_shock_case || low_mach_viscous_case) {', 1)
    print('F4 (Fix G for low_mach): OK')
    fixes_done += 1
else:
    print('F4 NOT FOUND')

# Fix 5: Relax Fix E - inner_count threshold
needle7 = 'inner_count < max_inner / 2 &&'
if needle7 in content:
    content = content.replace(needle7, 'inner_count < max_inner * 3 / 4 &&', 1)
    print('F5a (Fix E inner threshold): OK')
    fixes_done += 1
else:
    print('F5a NOT FOUND')

# Fix 5b: Relax Fix E - residual threshold
needle8 = 'mu > 0.0 && outer_res_norm <= 1.5 * min_outer_res_ever'
if needle8 in content:
    content = content.replace(needle8, 'mu > 0.0 && outer_res_norm <= 2.5 * min_outer_res_ever', 1)
    print('F5b (Fix E residual threshold): OK')
    fixes_done += 1
else:
    print('F5b NOT FOUND')

print(f'Total fixes applied: {fixes_done}/7')

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.write(content)
print('SteadySolver.cpp saved')

