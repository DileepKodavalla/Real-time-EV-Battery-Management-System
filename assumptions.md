# Assumptions & Operational Thresholds Document

### 1. Electrical & Voltage Parameters
* **Nominal Cell Voltage Range:** $2.20\text{V}$ to $3.20\text{V}$ per cell, mapped to 12-bit ADC input channels ($0\text{V}$ to $3.30\text{V}$).
* **Sensor Disconnect Threshold:** Any sensed voltage below $0.02\text{V}$ or above $3.28\text{V}$ is classified as an open-circuit sensor fault (`ERR_SENSOR_DISCONNECT`).
* **Imbalance Thresholds:**
  * **Minor Imbalance:** Cell voltage variance between $2.0\%$ and $4.9\%$ (`MODE_DEGRADED`).
  * **Critical Imbalance:** Cell voltage variance $\ge 5.0\%$ (`MODE_DEGRADED` with critical warnings and polarization risk alerts).

### 2. Timing & Execution Intervals
* **Sensor Sampling Rate:** Executed every $200\text{ms}$ to ensure responsive fault detection without flooding CPU cycles.
* **Cloud Telemetry Interval:** Asynchronous transmission prioritized during state changes or anomalies, with a baseline heartbeat interval of $1000\text{ms}$.
* **LCD Page Rotation:** Rotates display pages every $3000\text{ms}$ during normal operation, subject to immediate interrupt overrides upon fault detection.
* **ADC Freeze Timeout:** A static voltage reading persisting unchanged for longer than $15,000\text{ms}$ (15 seconds) triggers a frozen ADC failsafe condition (`ERR_ADC_FROZEN`).

### 3. Simulation Environment
* **Network:** Assumes a stable `Wokwi-GUEST` Wi-Fi access point connection within the simulation sandbox.
* **Hardware Interfacing:** Potentiometers (`POT1`–`POT4`) accurately simulate individual cell voltage variations across ESP32 ADC input pins.
