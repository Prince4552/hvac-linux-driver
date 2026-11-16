# Ext 4: The same control core on FreeRTOS

## 1. What this phase adds

The thermostat's control core (`thermostat/src/hvac_core.c`) now also runs on FreeRTOS, the most
widely used RTOS (real-time operating system) for microcontrollers, **without a single change**.
Three FreeRTOS tasks (plant, control, logger) pass messages through queues. A test proves that the
FreeRTOS run and the Linux simulator run give byte-identical results.

## 2. Why it matters

HVAC (heating, ventilation and air conditioning) products often have two kinds of computer: an
embedded Linux board (the gateway or controller with networking) and small microcontrollers that run
an RTOS (fan boards, compressor drives, room sensors). Code that can move between them saves a
product team from writing and testing the same logic twice. It only works if the logic is
written to be portable: no operating-system calls, no clock reads, no memory allocation. `hvac_core.c`
was written that way from Phase 5, and this phase shows it pays off.

## 3. Files added or changed

| File | Purpose |
| --- | --- |
| `scripts/fetch_freertos.sh` | Clones FreeRTOS-Kernel V11.3.3 into `.cache/` and checks the pinned commit |
| `rtos/FreeRTOSConfig.h` | Kernel settings: 1 ms tick, 4 priorities, static allocation, heap_4, checks on |
| `rtos/rtos_app.h` | Queue message types and the context shared by the tasks |
| `rtos/main.c` | Creates the static queues and tasks, starts the scheduler, writes metrics |
| `rtos/plant_task.c` | Room model and sensor (reuses `thermostat/sim/room_model.c`) |
| `rtos/control_task.c` | Calls the unchanged `hvac_core_step()` |
| `rtos/logger_task.c` | Lowest priority; writes the trace and ends the scheduler |
| `rtos/CMakeLists.txt` | Builds the kernel, the POSIX port and our tasks; sources come from `thermostat/` |
| `thermostat/sim/sil_trace.c`, `.h` | One shared trace writer for the host simulator and FreeRTOS |
| `scripts/check_rtos.py` | Compares the traces and records the FreeRTOS metrics |

## 4. How it works

```
 priority 3  control_task   waits on "readings" -> hvac_core_step() -> sends on "outputs"
                  ^  reading {t_s, temp}        |  outputs {state, compressor, heater}
                  |                             v
 priority 2  plant_task     every tick: read sensor -> send -> WAIT for outputs -> log -> step room
                                                    |  vTaskDelayUntil(1 tick)
                                                    v  "log" queue (32 slots)
 priority 1  logger_task    writes one trace row per step; at the end: stats, vTaskEndScheduler()
 priority 0  idle task
```

1. **Tasks and priorities.** A task is an endless function with its own stack. The scheduler always
   runs the highest-priority task that is ready. Control has the highest priority, so it answers as
   soon as a reading arrives. The logger has the lowest, so slow file output can never delay control.
2. **Queues.** Tasks never share variables directly. They copy messages into queues. A task that
   waits on an empty queue is *blocked* and uses no processor time.
3. **Lockstep for determinism.** The plant task sends a reading and then waits for the answer before
   it moves the room on. Simulated time is the plant task's own step counter, carried in the
   message, so the result never depends on how fast the computer is.
4. **`vTaskDelayUntil()`.** It wakes the plant task at fixed tick times (every 1 ms here). Unlike
   `vTaskDelay()`, the time spent working does not add up as drift.
5. **Static allocation.** All tasks and queues live in fixed arrays created with
   `xTaskCreateStatic()` and `xQueueCreateStatic()`. Nothing can run out of memory at run time, and
   the memory use is known when the program is built. The kernel heap (`heap_4`) was never used.
6. **The POSIX port.** FreeRTOS normally runs on a microcontroller. Its official POSIX port runs each
   task as a Linux thread and lets only one run at a time, so FreeRTOS becomes an ordinary program.

## 5. Run it yourself

```
scripts/fetch_freertos.sh
cmake -S rtos -B build/rtos && cmake --build build/rtos
build/rtos/hvac_rtos /tmp/rtos.csv /tmp/rtos.json   # about 8 s: 7200 ticks of 1 ms
build/thermostat/sil trace /tmp/host.csv
cmp /tmp/host.csv /tmp/rtos.csv && echo identical
```

## 6. Tests and results

| Check | Result |
| --- | --- |
| Gate `make test-all` | host 107/107 + FreeRTOS equivalence check, guest 106/106, 0 skipped |
| `rtos_trace_match_pct` | 100 % (7,201 lines, byte-identical to `sil trace`) |
| `rtos_queue_send_failures` | 0 |
| `rtos_min_free_heap_bytes` | 65536 of 65536 (0 kernel-heap allocations: everything is static) |
| `rtos_stack_high_water_words_*` | 4091 of 4096 for plant, control and logger (see section 7: not real stack use on this port) |
| Run time | about 8 s for 7,200 ticks |
| cppcheck on `rtos/` | 0 findings (one inline suppression for the FreeRTOS-defined hook signature) |
| Metrics | 54/54 on target |

## 7. Key concepts

- **RTOS.** An operating system whose job is predictable timing: the most important ready task
  always runs, and waiting tasks cost nothing.
- **Stack high-water mark.** FreeRTOS fills each new stack with a known pattern; the "high-water mark"
  is how much of the pattern is still untouched, which is the smallest free stack ever seen. On a
  microcontroller this tells you whether the stack is big enough. **On the POSIX simulator it does not:**
  the port runs each task on its own Linux thread stack, and the FreeRTOS stack array only holds the
  port's thread record. That is why all three tasks report 4091 of 4096 words free (D-030).
- **Why the core ported unchanged.** `hvac_core.c` takes its inputs (reading, time) as arguments and
  returns its outputs. It never reads a clock, never prints, never allocates. Any system that can call
  a C function can run it.

## 8. What could go wrong

- **Time from the tick count instead of the message.** If the host is busy, ticks can be late, and
  using the tick count as the clock would make the trace differ between runs. Carrying simulated time in
  the message keeps it deterministic.
- **A slow logger.** If the log queue filled up, sends would time out (`rtos_queue_send_failures`).
  The run had 0. The queue has 32 slots and the logger has a whole tick to empty it.
- **No sanitizers or Valgrind.** The POSIX port suspends threads with signals, which those tools do not
  model, so they are not run on `hvac_rtos`. The same `hvac_core.c` is checked by them in the Linux
  build.

## 9. Interview questions

1. *How did you show your control code is portable?* The same `hvac_core.c` runs in the Linux daemon,
   the host simulator and three FreeRTOS tasks. A test runs 2 simulated hours on both and requires
   byte-identical traces (7,201 lines, 100 % match).
2. *Why static allocation?* No heap fragmentation and no out-of-memory failure at run time; memory use
   is fixed when the program is built. Many safety standards prefer it.
3. *How do your tasks communicate?* Through queues of copied messages, never shared variables, so no
   locks are needed. Each counter is written by exactly one task.
4. *What does `vTaskDelayUntil` give you over `vTaskDelay`?* A fixed period measured from the last
   wake time, so the time spent working does not add up as drift.
5. *What would change on a real microcontroller?* The port (for example ARM Cortex-M instead of POSIX),
   a real sensor driver in the plant task's place, and the stack sizes, which would then be tuned from
   the high-water marks.
