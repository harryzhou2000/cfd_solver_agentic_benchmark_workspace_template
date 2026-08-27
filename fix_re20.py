
with open('/workspace/solver/src/SteadySolver.cpp', 'r') as f:
    content = f.read()

# Add back modest CFL cap for Re<100 (0.5 instead of 0.1)
# The current line is: double cfl_effective_init = cfl0;  // Fix T: removed Re<100 0.1 CFL cap
old = 'double cfl_effective_init = cfl0;  // Fix T: removed Re<100 0.1 CFL cap (caused divergence)'
new = (
    'double cfl_effective_init = cfl0;\n'
    '    // Fix T: Re<100 needs modest initial CFL cap (0.3) to prevent BC violation blowup\n'
    '    // Use 0.3 (not 0.1 which was too low) to allow faster early convergence\n'
    '    if (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0) {\n'
    '        cfl_effective_init = std::min(cfl0, 0.3);\n'
    '    }'
)
if old in content:
    content = content.replace(old, new, 1)
    print("Re<100 CFL cap 0.3: OK")
else:
    print("NOT FOUND")
    idx = content.find("removed Re<100")
    print(f"  idx={idx}")

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.write(content)
print("Saved")

