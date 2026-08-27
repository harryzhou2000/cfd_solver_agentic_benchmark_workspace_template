
### TASK 1 - Roe linear-wave dissipation floor (forces / convergence)

| variant | Cd | pressure Cd | viscous Cd | Cl | orders | final step | wall [s] | status |
|---|---|---|---|---|---|---|---|---|
| production baseline (floor 0.05, np=8) | 2.024290 | 1.223930 | 0.800361 | 7.188e-04 | 5.00 | 2148.000000 | 219.7 | converged |
| floor 0.05 (rebuilt copy) | 2.018119 | 1.220943 | 0.797176 | 5.762e-04 | 6.73 | 2783.000000 | 124.0 | converged |
| floor 0.01 | 2.017701 | 1.220480 | 0.797221 | 5.906e-04 | 6.73 | 2785.000000 | 136.2 | converged |
| floor 0.00 | 2.017754 | 1.220447 | 0.797307 | 5.853e-04 | 6.73 | 2785.000000 | 140.1 | converged |

### TASK 1 - Roe linear-wave dissipation floor (surface smoothness)

| variant | cp range min | cp range max | max |d1| | max |d2| | d2/d1 | sign alt. | max mirror asym. |
|---|---|---|---|---|---|---|---|
| production baseline (floor 0.05, np=8) | -1.00236 | 1.26524 | 1.4018e-01 | 1.7998e-02 | 0.128 | 4.000000 | 8.232e-04 |
| floor 0.05 (rebuilt copy) | -0.99410 | 1.26406 | 1.3957e-01 | 1.7932e-02 | 0.128 | 4.000000 | 6.096e-04 |
| floor 0.01 | -0.99361 | 1.26383 | 1.3954e-01 | 1.7918e-02 | 0.128 | 4.000000 | 6.266e-04 |
| floor 0.00 | -0.99357 | 1.26380 | 1.3954e-01 | 1.7917e-02 | 0.128 | 4.000000 | 6.213e-04 |

### TASK 2 - spatial order (forces / convergence)

| variant | Cd | pressure Cd | viscous Cd | Cl | orders | final step | wall [s] | status |
|---|---|---|---|---|---|---|---|---|
| spatial order 1 | 3.239634 | 2.323919 | 0.915715 | 1.504e-02 | 5.38 | 2597.000000 | 29.4 | converged |
| spatial order 2 | 2.018119 | 1.220943 | 0.797176 | 5.762e-04 | 6.73 | 2783.000000 | 57.7 | converged |

### TASK 2 - spatial order (surface smoothness)

| variant | cp range min | cp range max | max |d1| | max |d2| | d2/d1 | sign alt. | max mirror asym. |
|---|---|---|---|---|---|---|---|
| spatial order 1 | -1.41535 | 2.13160 | 1.7538e-01 | 3.4335e-02 | 0.196 | 6.000000 | 4.022e-02 |
| spatial order 2 | -0.99410 | 1.26406 | 1.3957e-01 | 1.7932e-02 | 0.128 | 4.000000 | 6.096e-04 |

### TASK 3 - Venkatakrishnan k (forces / convergence)

| variant | Cd | pressure Cd | viscous Cd | Cl | orders | final step | wall [s] | status |
|---|---|---|---|---|---|---|---|---|
| venkat_k = 1.0 | 2.032244 | 1.233259 | 0.798984 | 4.715e-04 | 6.78 | 2783.000000 | 25.2 | converged |
| venkat_k = 5.0 (default, = order 2 run) | 2.018119 | 1.220943 | 0.797176 | 5.762e-04 | 6.73 | 2783.000000 | 57.7 | converged |
| venkat_k = 10.0 | 2.015887 | 1.218112 | 0.797775 | 6.515e-04 | 6.73 | 2783.000000 | 18.3 | converged |

### TASK 3 - Venkatakrishnan k (surface smoothness)

| variant | cp range min | cp range max | max |d1| | max |d2| | d2/d1 | sign alt. | max mirror asym. |
|---|---|---|---|---|---|---|---|
| venkat_k = 1.0 | -0.99730 | 1.26587 | 1.3887e-01 | 1.9924e-02 | 0.143 | 4.000000 | 5.368e-04 |
| venkat_k = 5.0 (default, = order 2 run) | -0.99410 | 1.26406 | 1.3957e-01 | 1.7932e-02 | 0.128 | 4.000000 | 6.096e-04 |
| venkat_k = 10.0 | -0.99227 | 1.26381 | 1.3892e-01 | 1.8567e-02 | 0.134 | 4.000000 | 6.583e-04 |
