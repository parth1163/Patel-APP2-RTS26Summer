/*
 * Application 2 — Multi-task system with scheduling defense
 *
 * Scaffold level: ~70% complete.
 *
 * Scaffold Code - AI useage: 
 * Addition of the comment blocks for "real esp32 function" and the "compute standins"
 * Logic to allow for switching between webserve mode and pure logging mode
 * Commenting of code including human readable summaries
 *
 * What this scaffold gives you:
 * - Architecture from App 1 (dual-core, HTTP server) reused.
 * - Four FreeRTOS task skeletons, all pinned to Core 1, with priorities pre-assigned.
 * - Per-task heartbeat counters wired into the web page.
 * - A WCET measurement helper (MEASURE_WCET) you can wrap around any task body.
 *
 * What you do:
 * 1. Rename the task names and log strings for YOUR theme.
 * Tasks are currently named A, B, C, D — give them theme-appropriate names.
 * 2. Implement each task's body. Suggested workloads in the comments per task.
 * 3. Defend the priority assignment in your README. Use the high-level framework from the slide!
 * 4. Measure WCET for each task with MEASURE_WCET. Report mean / max.
 * 5. Demonstrate preemption: log a timestamp before/after, show in your README
 * that a higher-priority task interrupts a lower-priority one.
 *
 * What you DON'T need to change:
 * - The HTTP server, Wi-Fi setup, or web-page rendering structure.
 * - The WCET helper itself — just use it.
 * - The xTaskCreatePinnedToCore plumbing.
 *
 * ============================================================
 * OUTPUT MODE  (web monitor vs. terminal-only monitor)
 * ============================================================
 *
 * USE_WEBSERVER selects how the live monitor data is surfaced. Both modes
 * report the SAME fields (period, priority, heartbeats, WCET-max); only the
 * transport differs.
 *
 * USE_WEBSERVER = 1  -> Wi-Fi + HTTP server, auto-refreshing web page (App 1
 * carry-over). Open the printed IP in a browser.
 * USE_WEBSERVER = 0  -> No Wi-Fi, no HTTP. A monitor task prints the same
 * table to the serial console once per second. Use this
 * when you don't want to deal with Wi-Fi/Wokwi-GUEST,
 * or want a clean serial trace to paste into your README.
 * * ============================================================
 * Theme: Industrial (Ride-X Dispatch)
 * ============================================================
 */

#ifndef USE_WEBSERVER
#define USE_WEBSERVER 0
#endif

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#if USE_WEBSERVER
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#endif

#define WIFI_SSID  "Wokwi-GUEST"
#define WIFI_PASS  ""

#define CONFIG_LOG_DEFAULT_LEVEL_INFO 1
#define CONFIG_LOG_MAXIMUM_LEVEL  5

static const char *TAG = "RIDEX";

/* ---------- Per-task heartbeat counters (for the monitor) ---------- */
/* Each task increments its counter at the end of every iteration. The monitor
 * (web or terminal) reads them and displays values. Single 32-bit reads are
 * atomic on Xtensa, so we don't need a mutex around these (yet — App 6 changes
 * this). */
static volatile uint32_t hb_interlock, hb_motor, hb_input, hb_log;

/* ---------- WCET measurement helper ----------
 *
 * Usage:
 * uint64_t wcet_a_max_us = 0;
 * ...
 * MEASURE_WCET(wcet_a_max_us, {
 * // your task body code here
 * });
 *
 * The macro records the maximum observed time across all invocations.
 * Combine with periodic logging to get the WCET evidence for your README.
 */
#define MEASURE_WCET(_max_var, _body) do {                       \
    int64_t _t0 = esp_timer_get_time();                          \
    _body;                                                        \
    int64_t _dt = esp_timer_get_time() - _t0;                    \
    if ((uint64_t)_dt > (_max_var)) (_max_var) = (uint64_t)_dt;  \
} while (0)

/* Storage for WCET-max per task. Log these periodically. */
static uint64_t wcet_interlock_max_us, wcet_motor_max_us, wcet_input_max_us, wcet_log_max_us;
//        Workload Knobs 
// Loop bounds were calculated using the MEASURE_WCET macro
// until execution fell under the maximum 
// Variables inside these loops are marked 'volatile' to prevent dead-code
// elimination, forcing the CPU to execute every iteration instead of optimizing them away.
#define LOOPS_INTERLOCK   50 
#define LOOPS_MOTOR       300
#define LOOPS_INPUT       2000

/* ============================================================
 * PURE-COMPUTE WORKLOAD NOTES  (read before filling in the TODOs)
 * ============================================================
 *
 * The suggested workloads below are deliberately PERIPHERAL-FREE. No GPIO,
 * esp_random(), flash/NVS reads, etc. The only thing that changes your runtime
 * is a tunable constant, which is what you want for your WCET.
 *
 * Each task's comment now leads with a REAL / WOKWI hardware path (the realworld
 * version of the workload) and then lists pure-compute stand-ins. Do the
 * hardware path if you can; reach for a stand-in when you want a guaranteed
 * deterministic WCET or don't want to wire a part. Again we're early on so
 * feel free to just use a drop-in!
 *
 * Why not just do "random cycles?"
 *
 * (1) DEAD-CODE ELIMINATION. With optimization on (-O2/-Os), the
 * compiler deletes any computation whose result is never observed —
 * your whole loop can vanish and report ~0 us. Each kernel ends by
 * writing to a `volatile` sink, and seeds itself from that sink, so the
 * work is observable and cannot be elided.
 *
 * (2) INITIALIZE BUFFERS ONCE, NOT IN THE LOOP. malloc()/memset() of large
 * buffers inside the period destroys determinism. Declare buffers
 * static (file scope or `static` inside the task) and fill them one time
 * — e.g. in app_main, or guarded by a `static bool inited` flag.
 *
 * (3) USE float, NOT double, FOR PREDICTABLE TIMING. The ESP32 FPU is
 * single-precision only; `double` is software-emulated and runs ~10-50x
 * slower with data-dependent timing. (You CAN use double as a "make it
 * slower" knob, but call it out — it's emulated, not free.)
 *
 * (4) WARM UP. The first invocation pays one-time costs (instruction-cache
 * fill from flash, branch predictor cold). Either discard the first
 * sample or run a few warm-up iterations before trusting MEASURE_WCET.
 *
 * (5) WOKWI != SILICON. Constants below are 240 MHz hardware ballpark. Wokwi's
 * timing model differs, so MEASURE and tune the *_ITERS / *_N / *_REPS
 * knobs until you land in the target band. That tuning IS the assignment.
 *
 * Utilization sanity check for the README: WCET/period for each task, summed,
 * must sit under the rate-monotonic bound (~0.757 for n=4). With the targets
 * below you're around 15-20% — comfortably schedulable, which is part of why
 * the rate-monotonic priority ordering (higher rate = higher priority) holds.
 */

/* ============================================================
 * TASK A   priority 15   period 10 ms   highest priority
 * ============================================================
 *
 * Suggested workloads per theme:
 * Industrial: Dispatch Interlock Check
 */

// Instead of using the complex PRNG or IIR filter options, I used a delay loop
// By using a volatile as the counter you can eliminate dead-code and fufull under 10ms
static void dispatch_interlock(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    // Convert 10ms 
    const TickType_t period = pdMS_TO_TICKS(10);

    for (;;) {
        // Log System time for Preemption Evidence
        int64_t t = esp_timer_get_time();
        printf("[Interlock tick] t=%lld\n", t);

        MEASURE_WCET(wcet_interlock_max_us, {
            // "Compute Stand-Ins" 
            // It bypasses physical hardware requirements while accurately modeling 
            // the fixed timing latency of an actual industrial interlock sensor scan.
            volatile uint32_t delay_counter = 0;
              //Bound at 50 iterations to meet requirement (~59 us) 
              // Knobs are defined at the top
            for (int i = 0; i < LOOPS_INTERLOCK; i++) {
                delay_counter++;
            }
        });

        hb_interlock++;
        vTaskDelayUntil(&last, period);
    }
}

/* ============================================================
 * TASK B   priority 10   period 25 ms
 * ============================================================
 *
 * Suggested workloads:
 * Industrial: motor speed control
 */
static void motor_control(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(25);

    for (;;) {
        // Log System time for Preemption Evidence
        int64_t t = esp_timer_get_time();
        printf("[Motor tick] t=%lld\n", t);

        MEASURE_WCET(wcet_motor_max_us, {
            // Simulates a fixed motor control loop. 
            //Used a volatile loop to get predictable execution time
            // Bound at 300 iterations to meet requirement (~307 us)
            volatile uint32_t delay_counter = 0;
            // Knobs are defined at the top
            for (int i = 0; i < LOOPS_MOTOR; i++) {
                delay_counter++;
            }
        });

        hb_motor++;
        vTaskDelayUntil(&last, period);
    }
}

/* ============================================================
 * TASK C   priority 5    period 50 ms
 * ============================================================
 *
 * Suggested workloads:
 * Industrial: refresh operator-display 
 */
 // Instead of implementing CRC-32 over a buffer, or float matrix multiplications from the options, 
// I used delay loop to simulate updating the operator screen statuses
static void refresh_operator_display(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(50);

    for (;;) {
        MEASURE_WCET(wcet_input_max_us, {
            // Bound at 2000 iterations to meet requirement (~2007 us)
            volatile uint32_t delay_counter = 0;
            // Knobs are defined at the top
            for (int i = 0; i < LOOPS_INPUT; i++) {
                delay_counter++;
            }
        });

        hb_input++;
        vTaskDelayUntil(&last, period);
    }
}

/* ============================================================
 * TASK D   priority 2    period 200 ms   lowest priority
 * ============================================================
 *
 * Suggested workloads:
 * All themes: housekeeping / logging.
 *
 * This task is intentionally interruptible. If A/B/C take longer than expected,
 * D's deadline can slip. That's by design — you'll defend this trade-off
 * in your README. Its length also makes it the obvious target for your
 * preemption demo: log a timestamp before/after and you'll see A/B/C cut in.
 */

// Housekeeping and logging task
// Option 3: Leibniz formula for Pi to create a heavy load
static void task_log(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(200);

    for (;;) {
        MEASURE_WCET(wcet_log_max_us, {
            volatile float pi = 0.0f;
            float sign = 1.0f;

            // Knob is calculated here to ensure enough room for higher priority tasks to start 
            for (int k = 0; k < 8000; k++) {
                pi += sign / (float)(2 * k + 1);
                sign = -sign;

                // Prevents the hardware watchdog from tripping
                if (k % 1000 == 0) {
                    taskYIELD();   // Prevents watchdog trigger
                }
            }
            /*// BONUS: log the WCET-max for all four tasks every iteration:
             ESP_LOGI(TAG, "WCET us  A=%llu B=%llu C=%llu D=%llu",
             wcet_interlock_max_us, wcet_motor_max_us, wcet_input_max_us, wcet_log_max_us);
            */
        });

        hb_log++;
        vTaskDelayUntil(&last, period);
    }
}

#if USE_WEBSERVER
/* ============================================================
 * WEB MONITOR  (USE_WEBSERVER = 1)
 * ============================================================ */
static esp_err_t handle_root(httpd_req_t *req)
{
    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
        "<!DOCTYPE html>"
        "<html lang=\"en\"><head>"
        "<meta charset=\"utf-8\"><meta http-equiv=\"refresh\" content=\"1\">"
        "<title>Ride-X Dispatch · 4-task monitor</title>"
        "</head><body>"
        "<h1>Ride-X Dispatch Monitor</h1>"
        "</body></html>");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void) { return NULL; }
static void wifi_init_sta(void) {}
#else  /* USE_WEBSERVER == 0 */
/* ============================================================
 * TERMINAL MONITOR  (USE_WEBSERVER = 0)
 * ============================================================
 */
static void task_monitor(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000);

    for (;;) {
        printf("\n=== Ride-X Dispatch · 4-task monitor ===\n");
        printf("%-20s %-8s %-9s %-12s %-10s\n",
               "Task", "Period", "Priority", "Heartbeats", "WCET(us)");
        printf("%-20s %-8s %-9d %-12lu %-10llu\n",
               "dispatch_interlock",     "10 ms",  15, (unsigned long)hb_interlock, (unsigned long long)wcet_interlock_max_us);
        printf("%-20s %-8s %-9d %-12lu %-10llu\n",
               "motor_control",          "25 ms",  10, (unsigned long)hb_motor,      (unsigned long long)wcet_motor_max_us);
        printf("%-20s %-8s %-9d %-12lu %-10llu\n",
               "refresh_operator_display", "50 ms",  5,  (unsigned long)hb_input,      (unsigned long long)wcet_input_max_us);
        printf("%-20s %-8s %-9d %-12lu %-10llu\n",
               "task_log",               "200 ms",  2, (unsigned long)hb_log,        (unsigned long long)wcet_log_max_us);
        printf("(heartbeats should grow monotonically; a stalled counter = starved or hung task)\n");

        vTaskDelayUntil(&last, period);
    }
}
#endif /* USE_WEBSERVER */

/* ---------- app_main ---------- */
void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "==== App 2 [Ride-X Dispatch] starting — 4-task scheduler demo ====");

#if USE_WEBSERVER
    wifi_init_sta();
#else
    ESP_LOGI(TAG, "Output mode: TERMINAL MONITOR (USE_WEBSERVER=0) — no Wi-Fi, serial only");
    xTaskCreatePinnedToCore(task_monitor, "task_monitor", 4096, NULL, 1, NULL, PRO_CPU_NUM);
#endif

    xTaskCreatePinnedToCore(dispatch_interlock,        "dispatch_interlock",        3072, NULL, 15, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(motor_control,             "motor_control",             3072, NULL, 10, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(refresh_operator_display,  "refresh_operator_display",  3072, NULL,  5, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(task_log,                  "task_log",                  4096, NULL,  2, NULL, APP_CPU_NUM);
}