
with open('/workspace/solver/src/SteadySolver.cpp', 'r') as f:
    content = f.read()

# Change Fix E to allow slow CFL recovery even when inner uses many iterations
# Previously: inner_count < max_inner * 3/4 (would miss 68/80 case)
# New: inner_count < max_inner (any convergence), with tiered boost rate
old_fixe = '''        } else if (inner_count < max_inner * 3 / 4 &&
                   (outer_res_decreased ||
                    (mu > 0.0 && outer_res_norm <= 2.5 * min_outer_res_ever))) {
            // Fix E: only boost CFL if Fix D is NOT currently active.
            // Fix D (0.97x) and Fix E (1.2x) cancel each other, keeping CFL stuck at max
            // when outer residuals oscillate above 2x min — typical for supersonic oscillation.
            bool fix_d_active = (step > 30 && step > fix_d_grace_until
                                 && min_outer_res_ever > 0
                                 && outer_res_norm > 2.0 * min_outer_res_ever);
            if (!fix_d_active) {
                double cfl_boost = (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0) ? 1.02
                                 : (mu <= 0.0 && cfg.freestream.mach > 1.0) ? 1.2
                                 : 1.1;
                cfl_effective = std::min(cfl_effective * cfl_boost, cfl_max);
            }
        }'''

new_fixe = '''        } else if (inner_count < max_inner &&
                   (outer_res_decreased ||
                    (mu > 0.0 && outer_res_norm <= 2.5 * min_outer_res_ever))) {
            // Fix E: boost CFL when inner loop converged (not hit max).
            // Tiered: fast boost when inner iterations are few; slow recovery when many.
            bool fix_d_active = (step > 30 && step > fix_d_grace_until
                                 && min_outer_res_ever > 0
                                 && outer_res_norm > 2.0 * min_outer_res_ever);
            if (!fix_d_active) {
                double cfl_boost;
                if (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0)
                    cfl_boost = 1.02;  // slow ramp for Re<100
                else if (mu <= 0.0 && cfg.freestream.mach > 1.0)
                    cfl_boost = 1.2;   // fast ramp for supersonic inviscid
                else if (inner_count < max_inner / 2)
                    cfl_boost = 1.1;   // fast boost when inner converges quickly
                else
                    cfl_boost = 1.02;  // slow recovery when inner used many iterations
                cfl_effective = std::min(cfl_effective * cfl_boost, cfl_max);
            }
        }'''

if old_fixe in content:
    content = content.replace(old_fixe, new_fixe, 1)
    print("Fix E tiered: OK")
else:
    print("Fix E: NOT FOUND")
    idx = content.find("inner_count < max_inner * 3 / 4")
    print(f"idx={idx}, context={repr(content[idx:idx+80]) if idx>=0 else 'N/A'}")

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.write(content)
print("Saved")

