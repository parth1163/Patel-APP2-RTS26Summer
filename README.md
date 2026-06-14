# App 2 scaffold — multi-task scheduling

Scaffold level: **~70% complete**.

## What's given

- 4 task skeletons pinned to Core 1, priorities pre-assigned (RMS-style)
- WCET measurement macro: `MEASURE_WCET(max_var, { body })`
- Web monitor with per-task heartbeat counters and live WCET-max display
- Wi-Fi + HTTP server boilerplate (reused from App 1)

## What you implement

1. **Theme rename** — replace `YOURTHEME` everywhere
2. **Four task bodies** — see the comments in each `task_X()` for suggested workloads
3. **WCET measurement** — fill in `task_d` to log all four WCETs to serial periodically
4. **README defense** — see below

## README defense (graded)

Your README must include:

### Task table (mandatory)

| Task | Function | Period (ms) | WCET measured (µs) | WCET + 30% margin (µs) | Deadline | Priority | Core |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **A** | `dispatch_interlock` | 10 | 57 | 74.1 | 10 ms | 15 | 1 |
| **B** | `motor_control` | 25 | 325 | 422.5 | 25 ms | 10 | 1 |
| **C** | `refresh_operator_display` | 50 | 2025 | 2632.5 | 50 ms | 5 | 1 |
| **D** | `task_log` | 200 | 85017 | 110522.1 | 200 ms | 2 | 1 |    

### Schedulability defense

- Total utilization U = ∑ Cᵢ/Tᵢ
- Liu-Layland bound for n=4: U ≤ 4(2^(1/4) − 1) = 0.7568
- If U > Liu-Layland: run response-time analysis on task D (lowest priority)
- Conclusion: feasible / infeasible / borderline. State which.

### Preemption evidence

Add this to one of your task bodies:
{DONE}
```c
int64_t t = esp_timer_get_time();
ESP_LOGI(TAG, "task_a tick t=%lld", t);
```

Repeat in task B. Compare the serial log: when A wakes during a B iteration,
A's log line should appear BEFORE B finishes. That's preemption. Quote two
adjacent log lines that prove it.

From Output after running for a few seconds:
[Interlock tick] t=1066731  
[Interlock tick] t=1085745
[Interlock tick] t=1088174
[Motor tick] t=1066789  (This line got blocked and pushed down)
### Engineering analysis

1. **Priority defense** — explain each priority. RMS says shortest period &rarr; highest priority. Did you follow it?

  **Yes i followed the RMS. 
  **Highest Priority 15 : dispatch_interlock (10ms period)
Because this task would be respoonsible for passengers' safety , this would be a high priority task.
In addition because missing a dead line could result in an life threatening accident and system shutdown, 
would classify as a strict hard deadline, further proving its need for its high priority.

  **Priority 10 : motor_control (25ms period)
Since this controls the motor, this would be a higher priority task. Howoever since it would probably rely on the 
dispatch interlock and other safety nets, it would be lower than those tasks but still have a high priority since missing a deadline
can cause major accidents and shutdowns.

 **Priority 5 : refresh_operator_display (50ms period)
This task would be responsible for updated the rider operators display and show different statuses and metric of the ride. Since this 
task is can miss a deadline without causing and safety incidents, this tasks priority would rank lower than those above.

**Lowest Priority 2 : task_log (200ms period)
This task would be running in the backgroud of the main processor. Because a delay in updating the log does not affect
the overall ride operation and safety, it would rank lower than the previously listed tasks.

2. **3× WCET stress** — if your highest-priority task's WCET tripled, what's the new U? Is the set still feasible?
Linked

3. **Preemption proof** — quote the two timestamps showing preemption.
Linked 
## How to fail

- Skipping the WCET measurement and writing "the task takes about 1 ms." That's vibes, not engineering.
- Pinning task D to Core 0. That puts it next to Wi-Fi; Wi-Fi will starve it.
- Using `vTaskDelay` instead of `vTaskDelayUntil`. App 3 will teach you why; for App 2, use the latter so periods don't drift.
- Assigning equal priorities to two tasks "to be fair." That's round-robin, not real-time.

## Setup in Wokwi

Same shape as App 1. In a fresh Wokwi ESP-IDF project:

1. Replace `diagram.json`, `wokwi.toml`, `sdkconfig.defaults`, and `main/CMakeLists.txt` with this folder's versions.
2. Place this folder's `main.c` at `main/main.c` (delete Wokwi's `main/src/` folder), or leave `main/src/main.c` and edit `main/CMakeLists.txt` to use `SRCS "src/main.c"` + `INCLUDE_DIRS "src"`.
3. Confirm `wokwi.toml`'s `firmware` / `elf` paths reference `app2_tasks_scheduling` &mdash; that must match the `project(...)` name in the top-level `CMakeLists.txt`.
4. Click &#9654; to build.

**Critical for App 2:** the `REQUIRES esp_wifi esp_event esp_http_server esp_netif nvs_flash` line in this folder's `main/CMakeLists.txt` is what links the HTTP/Wi-Fi APIs. If you keep Wokwi's default `main/CMakeLists.txt`, you will get unresolved-symbol errors on `httpd_start`, `esp_wifi_init`, etc. **Use this folder's `main/CMakeLists.txt`.**

### Build locally with ESP-IDF instead

```bash
. $HOME/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Viewing the 4-task monitor

After Wi-Fi connects, the serial log prints the IP (`Got IP: 10.13.37.x`). In Wokwi, click the network indicator that appears in the simulator panel once port 80 is up &mdash; the table opens in a new tab and refreshes every second.

What to look for:

- All four heartbeat columns increment monotonically. If one stops growing, that task is starved or hung.
- WCET columns climb during the first few seconds, then stabilize once the worst-case path has been exercised. Use those stable numbers in your README.
- Refresh rate is 1 Hz; if the page itself stalls, your HTTP handler is being preempted &mdash; a teachable moment about Core 0 / Core 1 isolation.

## Honor code

## AI Disclosure
**AI Usage Disclosure
** Tool Use: Gemini was used strictly to clean up and reformat the markdown syntax for the Task Table after the text columns became misaligned and broken during data transfer.
