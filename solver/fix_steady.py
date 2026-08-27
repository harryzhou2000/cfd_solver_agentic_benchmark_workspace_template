with open('/workspace/solver/src/SteadySolver.cpp', 'r') as f:
    content = f.read()

fixes_applied = 0

# Fix 1: Add in_2nd_order_ramp flag
marker1 = '       StateVec last_res = outer_res_l2;'
insert1 = '

       // Fix L: During 2nd-order limiter ramp, suppress CFL reductions (residual jump expected).
       bool in_2nd_order_ramp = (step > first_order_steps && step <= first_order_steps + second_order_ramp_steps);'
if marker1 in content and 'in_2nd_order_ramp' not in content:
    content = content.replace(marker1, marker1 + insert1, 1)
    fixes_applied += 1
    print('Fix1 (in_2nd_order_ramp variable): Applied')
else:
    if 'in_2nd_order_ramp' in content:
        print('Fix1: Already applied')
    else:
        print('Fix1: Marker not found!')

# Fix 2: Build 5 - add !in_2nd_order_ramp guard
old2 = '        if (step > 5 && prev_outer_res > 0 && outer_res_norm > 10.0 * prev_outer_res) {'
new2 = '        if (!in_2nd_order_ramp && step > 5 && prev_outer_res > 0 && outer_res_norm > 10.0 * prev_outer_res) {'
if old2 in content:
    content = content.replace(old2, new2, 1)
    fixes_applied += 1
    print('Fix2 (Build5 guard): Applied')
elif new2 in content:
    print('Fix2: Already applied')
else:
    print('Fix2: Pattern not found!')

# Fix 3: Build 6 - add !in_2nd_order_ramp guard
old3 = '        if (step > first_order_steps && outer_res_before > 0 && outer_res_norm > 1.1 * outer_res_before) {'
new3 = '        if (!in_2nd_order_ramp && step > first_order_steps && outer_res_before > 0 && outer_res_norm > 1.1 * outer_res_before) {'
if old3 in content:
    content = content.replace(old3, new3, 1)
    fixes_applied += 1
    print('Fix3 (Build6 guard): Applied')
elif new3 in content:
    print('Fix3: Already applied')
else:
    print('Fix3: Pattern not found!')

# Fix 4: Fix K - gentler during ramp
old4_line = '           double cfl_inner_penalty = (mu > 0.0) ? 0.5 : 0.85;'
new4_replace = '''           double cfl_inner_penalty;
           if (in_2nd_order_ramp) {
               cfl_inner_penalty = 0.95;  // Very gentle during 2nd-order ramp
           } else if (mu > 0.0) {
               cfl_inner_penalty = 0.5;
           } else {
               cfl_inner_penalty = 0.85;
           }'''
if old4_line in content:
    content = content.replace(old4_line, new4_replace, 1)
    fixes_applied += 1
    print('Fix4 (FixK ramp guard): Applied')
elif 'in_2nd_order_ramp' in content and '0.95' in content:
    print('Fix4: Already applied')
else:
    print('Fix4: Pattern not found!')

with open('/workspace/solver/src/SteadySolver.cpp', 'w') as f:
    f.write(content)

print(f'Total fixes applied: {fixes_applied}')
