
with open('/workspace/solver/src/SteadySolver.cpp', 'r') as f:
    content = f.read()

# Fix for Re<100: use accept_scale=0.5 during first-order phase (prevents overshoot)
# Also remove Fix R (isothermal forcing) which causes physically spurious solutions
old_accept = '''       double accept_scale;
       if (inner_count >= max_inner) {
           // Inviscid high-Mach (M>0.8): be slightly less conservative to allow shock progress
           accept_scale = (mu <= 0.0 && cfg.freestream.mach > 0.8) ? 0.4 : 0.1;
       } else {
           accept_scale = 1.0;
       }'''

new_accept = '''       double accept_scale;
       if (inner_count >= max_inner) {
           // Inviscid high-Mach (M>0.8): be slightly less conservative to allow shock progress
           accept_scale = (mu <= 0.0 && cfg.freestream.mach > 0.8) ? 0.4 : 0.1;
       } else if (low_re_viscous_case && step <= first_order_steps) {
           // Fix T: Re<100 needs gentle 0.5x corrections during BL establishment
           accept_scale = 0.5;
       } else {
           accept_scale = 1.0;
       }'''

if old_accept in content:
    content = content.replace(old_accept, new_accept, 1)
    print("accept_scale Re<100 fix: OK")
else:
    print("accept_scale: NOT FOUND - checking")
    idx = content.find("accept_scale = (mu <= 0.0")
    print(f"  idx={idx}")

# Remove Fix R (isothermal forcing) for Re<100 - causes physically spurious CD
old_fixr = '''        // Fix R: restore isothermal fix ONLY for Re<100 low-Mach cases.
        if (mu > 0.0 && cfg.freestream.mach < 0.3 && low_re_viscous_case) {
            double T_ref = p_inf / (rho_inf * R_gas);
            for (int i = 0; i < n_owned; i++) {
                double rho_i = states[i][0];
                if (rho_i < 1e-14) continue;
                double ui = states[i][1] / rho_i;
                double vi = states[i][2] / rho_i;
                double p_iso = rho_i * R_gas * T_ref;
                states[i][3] = rho_i * (p_iso / ((gamma - 1.0) * rho_i) + 0.5*(ui*ui + vi*vi));
            }
        }'''

new_fixr = '''        // Fix T: removed isothermal Fix R - caused physically spurious CD for Re<100
        // (isothermal forcing overrides rhoE each step causing artificial state drift)'''

if old_fixr in content:
    content = content.replace(old_fixr, new_fixr, 1)
    print("Fix R removal: OK")
else:
    print("Fix R: NOT FOUND")
    idx = content.find("isothermal fix")
    print(f"  idx={idx}, context={repr(content[idx:idx+80])}")

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.write(content)
print("Saved")

