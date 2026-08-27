import sys

with open("/workspace/solver/src/SteadySolver.cpp", "r") as f:
    src = f.read()

orig_len = len(src)
errors = []

# Change 1: add consec_div_steps after result.converged = false;
old1 = '    result.converged = false;'
new1 = '    result.converged = false;
    int consec_div_steps = 0;'
if old1 in src:
    src = src.replace(old1, new1, 1)
    print("Change 1 applied")
else:
    errors.append("Change 1 anchor not found")

# Change 2: add mach_ref_lm after sr_frozen
old2 = '        std::vector<double> sr_frozen = spectral_radii;'
new2 = '        std::vector<double> sr_frozen = spectral_radii;
        double mach_ref_lm = (cfg.freestream.mach < 0.3 && mu > 0.0) ? cfg.freestream.mach : 0.0;'
if old2 in src:
    src = src.replace(old2, new2, 1)
    print("Change 2 applied")
else:
    errors.append("Change 2 anchor not found")

# Change 3: replace divergence detection block
old3 = '       // Build 5: Outer residual divergence detection
        if (step > 5 && prev_outer_res > 0 && outer_res_norm > 10.0 * prev_outer_res) {
            cfl_effective = std::max(cfl_effective * 0.7, cfl_effective_init);
        }
        prev_outer_res = outer_res_norm;'
new3 = '       // Build 5: divergence detection; supersonic inviscid needs 2 consecutive spikes
        if (step > 5 && prev_outer_res > 0 && outer_res_norm > 10.0 * prev_outer_res) {
            consec_div_steps++;
            if (mu <= 0.0 && cfg.freestream.mach > 1.0) {
                if (consec_div_steps >= 2) {
                    cfl_effective = std::max(cfl_effective * 0.8, cfl_effective_init);
                    consec_div_steps = 0;
                }
            } else {
                cfl_effective = std::max(cfl_effective * 0.7, cfl_effective_init);
                consec_div_steps = 0;
            }
        } else {
            consec_div_steps = 0;
        }
        prev_outer_res = outer_res_norm;'
if old3 in src:
    src = src.replace(old3, new3, 1)
    print("Change 3 applied")
else:
    errors.append("Change 3 anchor not found")

# Change 4: add mach_ref_lm to lusgsSolve call
old4 = '            lusgsSolve(lm, cfg, residuals_cur, sr_frozen, states, dt_local, gamma, mu, dU);'
new4 = '            lusgsSolve(lm, cfg, residuals_cur, sr_frozen, states, dt_local, gamma, mu, mach_ref_lm, dU);'
if old4 in src:
    src = src.replace(old4, new4, 1)
    print("Change 4 applied")
else:
    errors.append("Change 4 anchor not found")

# Change 5: update cfl_boost for supersonic inviscid
old5 = '            double cfl_boost = (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0) ? 1.02 : 1.1;'
new5 = '            double cfl_boost = (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0) ? 1.02
                             : (mu <= 0.0 && cfg.freestream.mach > 1.0) ? 1.2
                             : 1.1;'
if old5 in src:
    src = src.replace(old5, new5, 1)
    print("Change 5 applied")
else:
    errors.append("Change 5 anchor not found")

if errors:
    for e in errors:
        print("ERROR:", e)
    sys.exit(1)

with open("/workspace/solver/src/SteadySolver.cpp", "w") as f:
    f.write(src)

print(f"Done. Original: {orig_len}, New: {len(src)}")
