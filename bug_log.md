# Engineering Bug Log & Troubleshooting History

| Bug ID | Symptom / Issue | Root Cause | Resolution / Fix | Status |
| :--- | :--- | :--- | :--- | :--- |
| **BUG-01** | Periodic micro-delays causing sensor sampling jitter and UI stutter. | Initial prototype utilized blocking `delay()` calls within the display and telemetry loops. | Converted the entire execution architecture to a non-blocking finite state machine (FSM) driven entirely by `millis()` timing intervals. | **Resolved** |
| **BUG-02** | Rapid state fluctuation causing contactor relay chatter during boundary voltage conditions. | Relay logic directly toggled based on instantaneous ADC readings without hysteresis or debounce protection. | Implemented a 1000ms debounce delay timer (`DEBOUNCE_DELAY`) and strict operational mode transition rules. | **Resolved** |
| **BUG-03** | Blynk dashboard text display module failing to render string directives. | Blynk container module expected structured numeric data sub-modules rather than direct text strings. | Streamlined UI architecture by routing operator action directives directly into the unified `V5` diagnostic terminal log alongside fault codes. | **Resolved** |
| **BUG-04** | Loss of event logs during temporary Wi-Fi disconnects. | Telemetry transmission failed silently when `Blynk.connected()` returned false. | Added an offline event queuing mechanism (`cloudSyncPending`) with automatic batch synchronization upon Wi-Fi reconnection. | **Resolved** |
