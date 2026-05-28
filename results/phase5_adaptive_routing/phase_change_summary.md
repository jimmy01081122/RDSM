# Phase 5 Scripted Phase-Change Approximation Summary

Scope: consecutive controlled processes only. This approximates phase changes across runs and does not prove continuous in-process adaptive reaction.


This is a multi-process scripted approximation. It is not evidence of continuous in-process adaptive state transitions. It is useful only as a low-risk approximation of phase changes.

| Scenario | Phase | Rep | Workload | tx/sec | Samples | Invariants | Duplicates | Route OCC | Route Arb | Insufficient | Oscillation |
|---|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| phase_change_uniform_to_hot_to_uniform | 1 | 1 | low_uniform_read95 | 772446 | 10000 | 0 | 0 | 3862231 | 0 | 0 | 0 |
| phase_change_uniform_to_hot_to_uniform | 1 | 2 | low_uniform_read95 | 827895 | 10000 | 0 | 0 | 4139477 | 0 | 0 | 0 |
| phase_change_uniform_to_hot_to_uniform | 2 | 1 | mixed_hot4_write50 | 314229 | 10000 | 0 | 0 | 1570774 | 372 | 372 | 4 |
| phase_change_uniform_to_hot_to_uniform | 2 | 2 | mixed_hot4_write50 | 330710 | 10000 | 0 | 0 | 1653185 | 366 | 366 | 4 |
| phase_change_uniform_to_hot_to_uniform | 3 | 1 | low_uniform_read95 | 801521 | 10000 | 0 | 0 | 4007603 | 0 | 0 | 0 |
| phase_change_uniform_to_hot_to_uniform | 3 | 2 | low_uniform_read95 | 802037 | 10000 | 0 | 0 | 4010185 | 0 | 0 | 0 |
| phase_change_hot1_to_hot16 | 1 | 1 | high_hot1_write100 | 214101 | 10000 | 0 | 0 | 1070400 | 107 | 107 | 2 |
| phase_change_hot1_to_hot16 | 1 | 2 | high_hot1_write100 | 215045 | 10000 | 0 | 0 | 1075126 | 100 | 100 | 1 |
| phase_change_hot1_to_hot16 | 2 | 1 | high_hot16_write100 | 217242 | 10000 | 0 | 0 | 1085408 | 802 | 802 | 8 |
| phase_change_hot1_to_hot16 | 2 | 2 | high_hot16_write100 | 213805 | 10000 | 0 | 0 | 1068223 | 801 | 801 | 8 |
| phase_change_read_heavy_to_write_heavy | 1 | 1 | low_uniform_read95 | 808859 | 10000 | 0 | 0 | 4044294 | 0 | 0 | 0 |
| phase_change_read_heavy_to_write_heavy | 1 | 2 | low_uniform_read95 | 812851 | 10000 | 0 | 0 | 4064256 | 0 | 0 | 0 |
| phase_change_read_heavy_to_write_heavy | 2 | 1 | high_hot16_write100 | 215280 | 10000 | 0 | 0 | 1075600 | 801 | 801 | 8 |
| phase_change_read_heavy_to_write_heavy | 2 | 2 | high_hot16_write100 | 207422 | 10000 | 0 | 0 | 1036307 | 801 | 801 | 8 |
| phase_change_zipf_low_to_zipf_high | 1 | 1 | zipf90_write50 | 151213 | 10000 | 0 | 0 | 756004 | 61 | 5 | 12 |
| phase_change_zipf_low_to_zipf_high | 1 | 2 | zipf90_write50 | 152391 | 10000 | 0 | 0 | 761886 | 70 | 70 | 2 |
| phase_change_zipf_low_to_zipf_high | 2 | 1 | zipf99_write100 | 115405 | 10000 | 0 | 0 | 576905 | 122 | 119 | 6 |
| phase_change_zipf_low_to_zipf_high | 2 | 2 | zipf99_write100 | 115936 | 10000 | 0 | 0 | 579624 | 55 | 55 | 1 |
