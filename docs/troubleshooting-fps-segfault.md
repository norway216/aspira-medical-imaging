# Troubleshooting: FPS Statistics Anomaly and Segmentation Fault Crash

## Date

2026-06-24

## Problem Summary

Three issues were observed when running `aspira_main`:

1. **FPS always showing 0.0** — All three pipeline stages (Acquisition / Processing / Display) showed zero or near-zero frame rates
2. **Segfault crash** — Application crashed with `Segmentation fault (core dumped)` or `double free or corruption (fasttop)` after several seconds of operation
3. **Frame pool count growing without bound** — Pool display increased from 2 to 200+, far exceeding the pool capacity of 64

## Observed Symptoms

```
● HEALTHY | FPS: A=0.0 P=0.0 D=0.0 | Latency: 35209us | Pool: 518 | Uptime: 2021s
Segmentation fault (core dumped)
```

---

## Root Cause Analysis

### Issue 1: FPS Always Shows 0.0

**Original code:**

```cpp
// Global cumulative average: total frames / total runtime
stats.acquisition_fps = (double)acq / ((double)total_ms / 1000.0);
```

This is a **cumulative average** calculation. At startup, `total_ms` is large (uptime starts from program launch), while `acq` frame count is small, so FPS approaches zero. The value never accurately reflects the current throughput.

**Fix:** Implement a sliding-window FPS calculator using the most recent 30 instantaneous frame rates:

```cpp
struct FpsCalculator {
    static const int WINDOW = 30;
    double history[WINDOW] = {};
    int idx = 0, count = 0;
    uint64_t last_ts = 0;

    void tick(uint64_t now_ns) {
        if (last_ts != 0 && count < WINDOW) {
            double dt_ms = (double)(now_ns - last_ts) / 1.0e6;
            if (dt_ms > 0.0)
                history[idx++ % WINDOW] = 1000.0 / dt_ms;  // instantaneous FPS
            count++;
        }
        last_ts = now_ns;
    }

    double get() const {
        // Returns sliding window average
    }
};
```

---

### Issue 2: Segfault — SPSC Multiple Consumer Violation

**Original code:**

```cpp
// processing_task executed concurrently by multiple thread pool workers
auto processing_task = [&]() {
    aspira_frame* frame = nullptr;
    if (!aspira_spsc_pop(acq_queue.native(), &frame) || !frame) return;
    // ... process and push to render_queue ...
};

// Main loop: enqueue processing_task multiple times into the thread pool
while (pending > 0) {
    thread_pool.enqueue([&processing_task]() { processing_task(); },
                         ASPIRA_PRIORITY_PROCESSING);
    pending--;
}
```

**Root cause: SPSC (Single Producer Single Consumer) contract violation**

The `aspira_spsc_rb` is a **lock-free single-producer single-consumer** ring buffer. Its correctness depends on:

- Exactly one thread calling `push` (the producer)
- Exactly one thread calling `pop` (the consumer)

The code above submits `processing_task` to a thread pool, causing **multiple worker threads to pop from the same SPSC queue concurrently**, violating the SPSC contract. This results in:

1. **Data race:** Two consumers may read the same `tail` value and process the same frame twice
2. **Element skipping:** Consumer A reads tail=5, Consumer B also reads tail=5. A processes element 5 and updates tail to 6; B processes the same element 5 and updates tail to 7, skipping element 6 entirely
3. **Memory corruption:** The same frame gets pushed to the render_queue multiple times, or freed back to the frame pool multiple times, corrupting the pool's lock-free free list

**Fix:** Use a **dedicated processing thread** to preserve the SPSC single-consumer guarantee:

```cpp
// Dedicated processing thread — the sole consumer
static void processing_thread_fn(...) {
    while (running->load()) {
        aspira_frame* frame = nullptr;
        if (!aspira_spsc_pop(acq_queue->native(), &frame) || !frame) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        signal_pipeline->process_inplace(frame);
        // Push to render_queue (single producer)
        if (!aspira_spsc_push(render_queue->native(), &frame))
            aspira_frame_pool_free_frame(frame_pool->native(), frame);
    }
}

// Launch dedicated thread in main()
std::thread proc_thread(processing_thread_fn, ...);
```

This guarantees:
- **Acq queue:** Main thread (sole producer) → Processing thread (sole consumer) ✓
- **Render queue:** Processing thread (sole producer) → Main thread (sole consumer) ✓

---

### Issue 3: Segfault — Self-Referencing Lambda Undefined Behavior

**Original code:**

```cpp
auto processing_task = [&]() { /* ... */ };

// Lambda capturing a reference to itself
thread_pool.enqueue([&processing_task]() { processing_task(); }, ...);
```

Although `processing_task` is a local variable in `main()` (lifetime covers the entire program), this **self-referencing capture pattern** is fragile:

- `std::function` internally copies/moves the lambda, which may dangle the captured reference
- Thread pool workers may begin executing before `processing_task` is fully constructed

**Fix:** Use a plain function with an explicit context struct, avoiding lambda self-reference entirely:

```cpp
static void processing_task_fn(PipelineCtx* ctx) { /* ... */ }
```

---

### Issue 4: Segfault — WatchdogManager Calling free() on Stack Objects

**Call stack (Valgrind output):**

```
Invalid free() / delete
  at aspira_watchdog_manager_free_ptr
  by aspira::WatchdogManager::~WatchdogManager()
  by main
```

**Root cause:** `WatchdogManager::register_watchdog()` stores raw pointers to watchdogs, and its destructor (`aspira_watchdog_manager_destroy()`) iterates all registered watchdogs, calling `aspira_watchdog_destroy()` followed by `free()` on each.

However, the watchdog objects in `main.cpp` are **stack-allocated**:

```cpp
WatchdogManager wd_manager(200);
Watchdog acq_wd("acquisition", ...);     // stack-allocated!
Watchdog proc_wd("processing", ...);     // stack-allocated!
Watchdog render_wd("rendering", ...);    // stack-allocated!
wd_manager.register_watchdog(&acq_wd);   // registers raw pointer
```

When `wd_manager` is destroyed, it calls `free()` on stack-allocated objects, causing undefined behavior (and a crash on glibc's heap integrity check).

**Fix:** Remove `WatchdogManager` entirely and check each watchdog's health directly:

```cpp
bool pipeline_healthy = acq_wd.is_healthy()
                     && proc_wd.is_healthy()
                     && render_wd.is_healthy();
```

---

### Issue 5: Double Free — Residual Frames in Queues at Shutdown

At shutdown, the `acq_queue` and `render_queue` may still contain unprocessed frame pointers. These frames were allocated from the frame pool but were never returned. When the `FramePool` destructor runs and frees the underlying slab memory, the remaining frame pointers become dangling references. If any code path later attempts to access or free them, a double-free or use-after-free occurs.

**Fix:** Drain both queues at shutdown and return all residual frames to the pool:

```cpp
aspira_frame* leftover = nullptr;
while (aspira_spsc_pop(acq_queue.native(), &leftover) && leftover)
    aspira_frame_pool_free_frame(frame_pool.native(), leftover);
while (aspira_spsc_pop(render_queue.native(), &leftover) && leftover)
    aspira_frame_pool_free_frame(frame_pool.native(), leftover);
```

---

### Issue 6: Misleading Frame Pool Count Display

**Original code:**

```cpp
stats.pool_allocations = disp + acq;  // cumulative frame count approximation
stats.pool_free = disp;
// Display: Pool = pool_allocations - pool_free = acq (cumulative acquired count)
```

The display showed **cumulative acquired frame count**, not the actual **frame pool usage**. Since frames are allocated and freed cyclically, the cumulative count grew without bound.

**Fix:** Use the frame pool's actual free count:

```cpp
stats.pool_allocations = pipe_cfg.frame_pool_size;
stats.pool_free = frame_pool.free_count();
```

---

## Stable State After Fixes

```
FPS: A=28.9 P=29.3 D=29.3 | Latency: ~21ms | Dropped: 0 | Pool: 3 | HEALTHY

═══ Final Statistics ═══
  Frames Acquired:  231
  Frames Processed: 230
  Frames Displayed: 230
  Frames Dropped:   0
Aspira Medical Imaging Framework shutdown complete.
Goodbye.
```

| Metric | Before Fix | After Fix |
|--------|-----------|-----------|
| Acquisition FPS | 0.0 | 28.9 |
| Processing FPS | 0.0 | 29.3 |
| Display FPS | 0.0 | 29.3 |
| Dropped Frames | 0 | 0 |
| Crashes | Segfault within seconds | None |
| Pool Display | Grows to 200+ | Stable at 2–3 |
| Valgrind | double free errors | Clean (library-level leaks only) |

---

## Lessons Learned

1. **Respect SPSC queue contracts strictly.** Lock-free SPSC correctness depends entirely on the single-producer/single-consumer guarantee. For multi-consumer scenarios, use MPMC queues or a dedicated consumer thread.

2. **Lambda self-reference is an anti-pattern.** Prefer plain functions with explicit context structs to avoid lifetime and construction-order pitfalls.

3. **Allocation/deallocation symmetry matters.** Objects created with `_create`/`_free_ptr` factories must be heap-allocated; stack-allocated objects must not be passed to freeing managers.

4. **Valgrind is the tool of choice for memory issues.** Its precise call-stack output quickly pinpoints the source of double-free and invalid-free errors.

## Files Modified

| File | Changes |
|------|---------|
| `src/main/main.cpp` | Comprehensive rewrite: `FpsCalculator`, dedicated processing thread, direct watchdog health checks, queue draining at shutdown, frame pool count fix |
| `src/core/src/sizes.c` | Linter-added `_create`/`_free_ptr` factory functions (bridging C/C++ opaque type boundary) |
