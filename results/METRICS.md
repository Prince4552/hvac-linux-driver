# Metrics

Generated 2026-11-16T05:24:21Z from commit `20526fa` by `make metrics`. Timing numbers come from an emulated ARM64 board (QEMU without hardware acceleration) and are only meaningful relative to each other.

| Metric | Value | Target | Pass | What it means |
| --- | --- | --- | --- | --- |
| `guest_tests_passed` | 106 count | report | yes | In-guest tests that passed on the emulated board |
| `guest_tests_failed` | 0 count | == 0 | yes | In-guest tests that failed, errored or were skipped |
| `host_tests_passed` | 107 count | report | yes | Unit, integration and simulation checks on the host |
| `host_tests_failed` | 0 count | == 0 | yes | Host checks that failed |
| `kunit_cases_passed` | 5 count | == 5 | yes | Kernel-level checks of the conversion math (all KUnit cases) |
| `driver_load_unload_cycles` | 500 count | >= 500 | yes | Times the driver was loaded and removed without a problem |
| `kmemleak_reports` | 0 count | == 0 | yes | Memory blocks our driver allocated and never freed |
| `kasan_reports` | 0 count | == 0 | yes | Invalid memory accesses (use-after-free, out of bounds) |
| `lockdep_reports` | 0 count | == 0 | yes | Locking mistakes that could deadlock |
| `concurrent_read_errors` | 0 count | == 0 | yes | Wrong or failed readings with 8 threads reading at once |
| `driver_read_latency_median_us` | 1144 us | report | yes | Time for one temperature read on the emulated board (median; relative only) |
| `driver_read_latency_p99_us` | 4110 us | report | yes | Time for one temperature read on the emulated board (p99; relative only) |
| `checkpatch_errors` | 0 count | == 0 | yes | Kernel coding-style errors |
| `checkpatch_warnings` | 0 count | == 0 | yes | Kernel coding-style warnings (allowed types excluded) |
| `sparse_warnings` | 0 count | == 0 | yes | Kernel type-checking problems |
| `mutation_score` | 3/3 killed/total | == 3/3 | yes | Deliberately inserted bugs that the tests caught |
| `cppcheck_findings` | 0 count | == 0 | yes | Likely bugs in user-space C found without running it |
| `core_line_coverage_pct` | 100.0 % | >= 95 | yes | Share of control-core lines exercised by unit tests |
| `core_branch_coverage_pct` | 98.9 % | >= 90 | yes | Share of control-core branches exercised by unit tests |
| `sanitizer_errors` | 0 count | == 0 | yes | Memory and undefined-behavior errors found by ASan/UBSan |
| `valgrind_errors` | 0 count | == 0 | yes | Memory errors found by Valgrind |
| `valgrind_leaked_bytes` | 0 bytes | == 0 | yes | Memory leaked (definitely or indirectly lost) in user-space code |
| `sil_comfort_pct_hot_day` | 100.0 % | >= 98 | yes | Share of time the simulated room stayed within 18.5..27.5 C |
| `sil_starts_per_hour_hot_day_product` | 2.83 per hour | < 592.2 | yes | How often the compressor starts (fewer is gentler) |
| `sil_starts_per_hour_hot_day_baseline` | 592.2 per hour | report | yes | Compressor starts per hour of the naive baseline controller |
| `sil_short_cycle_violations_hot_day` | 0 count | == 0 | yes | Compressor restarts that came too soon |
| `sil_comfort_pct_cold_day` | 100.0 % | >= 98 | yes | Share of time the simulated room stayed within 18.5..27.5 C |
| `sil_starts_per_hour_cold_day_product` | 0.0 per hour | == 0 | yes | How often the compressor starts (fewer is gentler) |
| `sil_starts_per_hour_cold_day_baseline` | 0.0 per hour | report | yes | Compressor starts per hour of the naive baseline controller |
| `sil_short_cycle_violations_cold_day` | 0 count | == 0 | yes | Compressor restarts that came too soon |
| `sil_comfort_pct_mild_day` | 100.0 % | >= 98 | yes | Share of time the simulated room stayed within 18.5..27.5 C |
| `sil_starts_per_hour_mild_day_product` | 0.5 per hour | < 113.5 | yes | How often the compressor starts (fewer is gentler) |
| `sil_starts_per_hour_mild_day_baseline` | 113.5 per hour | report | yes | Compressor starts per hour of the naive baseline controller |
| `sil_short_cycle_violations_mild_day` | 0 count | == 0 | yes | Compressor restarts that came too soon |
| `modbus_roundtrip_median_ms` | 2.0 ms | report | yes | Time for a Modbus request and reply on the emulated board |
| `fault_detection_latency_s` | 0.1 s | <= 1.6 | yes | Time from sensor loss to alarm visible over Modbus (target 3 x 0.2 s + 1 s) |
| `hvacd_rss_kb` | 1636 kB | report | yes | Daemon memory use after 30 s |
| `hvacd_binary_bytes` | 34672 bytes | report | yes | Daemon program size (stripped) |
| `guest_boot_to_tests_s` | 8.4 s | report | yes | Seconds from kernel start to the start of the guest tests |
| `build_duration_min` | 5.9 min | report | yes | Duration of the last full (from empty output folder) image build |
| `rootfs_size_mb` | 55 MB | report | yes | Space used by the root filesystem image |
| `control_loop_jitter_broker_down_ms` | 3 ms | <= 100 | yes | Largest change of the 200 ms control-loop period while the broker is down |
| `fdd_detection_rate` | 100.0 % | >= 100 | yes | Injected faults whose expected diagnostic bit was set (5 cases) |
| `fdd_false_alarms` | 0 count | == 0 | yes | Healthy simulated days (3 scenarios x 5 seeds) with any diagnostic bit set |
| `fdd_mean_time_to_detect_min` | 26.4 min | report | yes | Average time from fault to diagnostic bit |
| `mqtt_messages_received_ratio` | 1.0 ratio | == 1.0 | yes | Telemetry messages received / sent over 10 healthy periods |
| `mqtt_reconnect_s` | 4.74 s | <= 15 | yes | Time from broker restart until hvacd is connected again |
| `rtos_min_free_heap_bytes` | 65536 bytes | report | yes | Lowest free FreeRTOS kernel heap (of 65536; 0 allocations) |
| `rtos_queue_send_failures` | 0 count | == 0 | yes | Queue sends that timed out in the FreeRTOS port |
| `rtos_stack_high_water_words_control` | 4091 words | report | yes | Unused words of the control task's FreeRTOS stack (POSIX simulator: covers only the FreeRTOS stack area, not the Linux thread's stack; D-030) |
| `rtos_stack_high_water_words_logger` | 4091 words | report | yes | Unused words of the logger task's FreeRTOS stack (POSIX simulator: covers only the FreeRTOS stack area, not the Linux thread's stack; D-030) |
| `rtos_stack_high_water_words_plant` | 4091 words | report | yes | Unused words of the plant task's FreeRTOS stack (POSIX simulator: covers only the FreeRTOS stack area, not the Linux thread's stack; D-030) |
| `rtos_trace_match_pct` | 100.0 % | == 100 | yes | FreeRTOS trace identical to the host simulator's (hot_day, 2 h, seed 1) |
| `watchdog_trigger_latency_s` | 1.99 s | <= 5 | yes | Time from the daemon freezing to the watchdog expiring (timeout 2 s) |
