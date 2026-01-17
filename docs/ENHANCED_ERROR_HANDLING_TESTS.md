# Enhanced Error Handling - Integration Test Plan

**Status**: Ready for Testing
**Date**: 2026-01-05
**Feature**: MVP-4 Enhanced Error Handling (OPTIONAL)
**Configuration**: `enhanced_error_handling: true` in plantOS.yaml

---

## Overview

This document provides comprehensive integration tests for the Enhanced Error Handling system. All 9 implementation phases are complete and compiled successfully. The system includes:

1. **Intelligent Retry Logic** - Different strategies per failure type
2. **Duration-Aware Adaptation** - Automatically adapt requested durations to SafetyGate limits
3. **Sensor Failure Retry** - Retry sensor readings 3-5 times with exponential backoff
4. **Enhanced Alert System** - RESOLVED state tracking with comprehensive 4-part error messages

---

## Test Environment Setup

### Prerequisites
- PlantOS firmware compiled with `enhanced_error_handling: true`
- Device flashed and running
- Serial logging enabled (`task run` or `task snoop`)
- Web UI accessible at device IP address

### Verification Steps
1. **Flash firmware**: `task run`
2. **Access web UI**: `http://<device-ip>`
3. **Monitor logs**: Serial output should show enhanced error messages

---

## Test Scenarios

### Test 1: Duration Adaptation (Acid Pump)

**Objective**: Verify SafetyGate automatically adapts duration violations instead of rejecting.

**Setup**:
- AcidPump max duration: 30s (configured in plantOS.yaml)
- pH correction will request 45s injection (exceeds limit)

**Steps**:
1. Start pH correction via web UI
2. Wait for PH_INJECTING state
3. Monitor serial logs for duration adaptation

**Expected Results**:
- ✅ Log shows: "Adapting AcidPump duration: 45s → 30s"
- ✅ Alert created: `DURATION_ADAPTED_AcidPump` with 4-part message:
  - Root cause: "SafetyGate rejected: Duration 45s exceeds max 30s"
  - User action: "System adapted duration. Consider increasing max duration or reducing dose."
  - Context: Current operation state
  - Recovery: "Retrying with adapted duration 30s"
- ✅ Pump activates for 30s (not 45s)
- ✅ pH correction completes successfully
- ✅ Alert marked RESOLVED in status report

**Pass Criteria**: Duration automatically adapted, pump runs 30s, alert shows RESOLVED

---

### Test 2: Sensor Retry with Exponential Backoff

**Objective**: Verify pH sensor failures trigger retry with increasing delays.

**Setup**:
- Disconnect pH sensor UART (GPIO20/21) or power off sensor
- Start pH correction

**Steps**:
1. Disconnect pH sensor
2. Start pH correction via web UI
3. Wait for PH_MEASURING state
4. Monitor serial logs for retry attempts
5. Reconnect sensor after 3 retries (optional)

**Expected Results**:
- ✅ Retry attempt 1: Immediate (0s delay)
- ✅ Retry attempt 2: After 5s backoff
- ✅ Retry attempt 3: After 10s backoff
- ✅ Retry attempt 4: After 15s backoff
- ✅ Retry attempt 5: After 15s backoff (max)
- ✅ After 5 failures: Alert `SENSOR_CRITICAL` with 4-part message:
  - Root cause: "Sensor has no value: hasPhValue() returned false for 5 consecutive readings"
  - User action: "Check sensor wiring, power, and UART connection (TX=GPIO20, RX=GPIO21)"
  - Context: "pH measurement phase, 0 readings collected"
  - Recovery: "Aborting to IDLE. System will retry on next pH correction cycle."
- ✅ FSM transitions to IDLE (not ERROR)
- ✅ If sensor reconnected: Next retry succeeds, alert marked RESOLVED

**Pass Criteria**: 5 retries with correct backoff delays, comprehensive error message, graceful abort

---

### Test 3: Operation Retry Framework (pH Correction)

**Objective**: Verify multi-step operation retry with backoff and context tracking.

**Setup**:
- Configure AcidPump with extremely low max duration (e.g., 1s) to force rejection
- pH correction should fail repeatedly

**Steps**:
1. Lower AcidPump max duration to 1s in plantOS.yaml
2. Recompile and flash
3. Start pH correction via web UI
4. Monitor retry attempts

**Expected Results**:
- ✅ Operation initialized: "Operation retry initialized: pH_CORRECTION (max retries: 3)"
- ✅ Retry attempt 1: Immediate (0s delay)
- ✅ Retry attempt 2: After 1s backoff
- ✅ Retry attempt 3: After 2s backoff
- ✅ Retry attempt 4: After 4s backoff (final attempt)
- ✅ After max retries: Alert `PUMP_REJECTION_ACID` with context:
  - Root cause: "SafetyGate rejected acid pump command even after duration adaptation"
  - User action: "Check acid pump wiring, verify pump not stuck, check SafetyGate max duration config"
  - Context: "pH correction attempt 1/5, injection phase"
  - Recovery: "Aborting pH correction. Will retry on next cycle."
- ✅ FSM transitions to ERROR
- ✅ After ERROR timeout: FSM transitions to INIT

**Pass Criteria**: 4 retry attempts with exponential backoff (1s, 2s, 4s), comprehensive error, ERROR state

---

### Test 4: Resolved Alert History

**Objective**: Verify alerts transition to RESOLVED (not cleared) and appear in status reports.

**Setup**:
- Trigger 3 different errors (sensor, duration, pump rejection)
- Fix issues and complete operations successfully

**Steps**:
1. Trigger `SENSOR_CRITICAL` (disconnect pH sensor)
2. Trigger `DURATION_ADAPTED_AcidPump` (request >30s duration)
3. Trigger `PUMP_REJECTION_ACID` (extreme duration violation)
4. Fix all issues (reconnect sensor, restore config)
5. Complete pH correction successfully
6. Wait for status report (30s interval)

**Expected Results**:
- ✅ Status report shows "Active Alerts: 3"
- ✅ Each alert shows full 4-part error message
- ✅ After successful completion: Alerts marked RESOLVED
- ✅ Status report shows:
  - "Active Alerts: 0 (ALL CLEAR)"
  - "Resolved Alerts: 3"
- ✅ Resolved alerts show:
  - Original error details
  - RESOLVED timestamp
  - Duration active (seconds)
- ✅ After 1 hour: Resolved alerts pruned automatically

**Pass Criteria**: Alerts show RESOLVED (not deleted), history preserved for 1 hour

---

### Test 5: Hardware Detection Alerts (Water Operations)

**Objective**: Verify water level sensor failures trigger comprehensive alerts.

**Setup**:
- Water level sensors on GPIO10 (HIGH) and GPIO11 (LOW)
- Test sensor failure detection

**Test 5a: HIGH Water Sensor Failure (Fill Operation)**

**Steps**:
1. Disconnect HIGH water sensor (GPIO10) or simulate failure
2. Start water fill operation via web UI
3. Wait for 30s timeout

**Expected Results**:
- ✅ Water fill operation completes after 30s timeout
- ✅ Alert `HARDWARE_WATER_SENSOR_HIGH` created:
  - Root cause: "Sensor did not trigger after 30s timeout - possible sensor failure or disconnection"
  - User action: "Check HIGH water sensor wiring (GPIO10), verify sensor power, test sensor with multimeter"
  - Context: "Water filling completed via timeout (30s) instead of sensor trigger"
  - Recovery: "Tank may be under-filled or sensor may be faulty. Manual verification recommended."
- ✅ FSM transitions to IDLE
- ✅ Alert visible in status report

**Test 5b: LOW Water Sensor Failure (Drain Operation)**

**Steps**:
1. Disconnect LOW water sensor (GPIO11) or simulate failure
2. Start water empty operation via web UI
3. Wait for 30s timeout

**Expected Results**:
- ✅ Water empty operation completes after 30s timeout
- ✅ Alert `HARDWARE_WATER_SENSOR_LOW` created:
  - Root cause: "Sensor did not clear after 30s timeout - possible sensor failure, disconnection, or clog"
  - User action: "Check LOW water sensor wiring (GPIO11), verify sensor power, test sensor, check for clogs"
  - Context: "Water emptying completed via timeout (30s) instead of sensor trigger"
  - Recovery: "Tank may not be fully drained or sensor may be faulty. Manual verification recommended."
- ✅ FSM transitions to IDLE
- ✅ Alert visible in status report

**Pass Criteria**: Sensor failures detected, comprehensive error messages, graceful timeout handling

---

### Test 6: Pump Rejection Alerts (Feeding)

**Objective**: Verify nutrient pump rejections trigger comprehensive alerts.

**Setup**:
- Configure NutrientPumpA with very low max duration (1s)
- Start feeding sequence

**Steps**:
1. Lower NutrientPumpA max duration to 1s
2. Recompile and flash
3. Start feeding via web UI
4. Monitor pump activation attempts

**Expected Results**:
- ✅ Alert `PUMP_REJECTION_NUTRIENT_A` created:
  - Root cause: "SafetyGate rejected nutrient pump A command"
  - User action: "Check nutrient pump A wiring, verify pump not stuck, check SafetyGate max duration config"
  - Context: "Feeding operation aborted during Nutrient A dosing"
  - Recovery: "Manual intervention required. Check pump hardware."
- ✅ FSM transitions to ERROR
- ✅ Pumps B and C not activated (aborted early)

**Pass Criteria**: Pump rejection detected, comprehensive error, graceful abort

---

### Test 7: Temperature Sensor Detection

**Objective**: Verify temperature sensor availability is checked.

**Setup**:
- Temperature sensor configured on GPIO23 (DS18B20)

**Steps**:
1. Boot device normally (sensor connected)
2. Check logs for temperature sensor status
3. Disconnect sensor (optional)
4. Check logs again

**Expected Results**:
- ✅ With sensor connected: "Temperature sensor configured and responding"
- ✅ Without sensor: "Temperature sensor not configured" (DEBUG level log)
- ✅ No error alert (informational only - not critical)

**Pass Criteria**: Temperature sensor status logged, no critical alerts

---

### Test 8: ERROR State Resolution Behavior

**Objective**: Verify ERROR state resolves all active alerts instead of clearing them.

**Setup**:
- Trigger multiple alerts (pH critical, sensor critical, pump rejection)
- Enter ERROR state
- Wait for ERROR timeout (5s)

**Steps**:
1. Trigger 3 active alerts
2. FSM enters ERROR state
3. Wait 5 seconds
4. FSM transitions to INIT
5. Check status report

**Expected Results**:
- ✅ During ERROR: Status shows 3 active alerts
- ✅ After ERROR timeout: Log shows "Resolving alerts and restarting to INIT"
- ✅ All 3 alerts marked RESOLVED (not cleared)
- ✅ Status report shows:
  - Active alerts: 0
  - Resolved alerts: 3 (with timestamps)
- ✅ FSM in INIT state, then IDLE

**Pass Criteria**: Alerts marked RESOLVED (not deleted), history preserved

---

### Test 9: Backward Compatibility (Flag Disabled)

**Objective**: Verify legacy behavior when enhanced error handling is disabled.

**Setup**:
- Modify plantOS.yaml: `enhanced_error_handling: false`
- Recompile and flash

**Steps**:
1. Set `enhanced_error_handling: false`
2. Recompile: `task build`
3. Flash: `task flash`
4. Trigger sensor failure (disconnect pH sensor)
5. Start pH correction

**Expected Results**:
- ✅ NO retry attempts (legacy: abort immediately)
- ✅ Alerts still created (backward compatible)
- ✅ NO RESOLVED state (legacy: alerts cleared)
- ✅ ERROR state clears alerts (not resolves)
- ✅ No duration adaptation (legacy: strict rejection)
- ✅ No operation retry framework

**Pass Criteria**: Legacy behavior preserved, no retry logic executes

---

## Success Criteria Summary

All tests must pass for MVP-4 to be considered complete:

- [x] **Phase 1-9**: Implementation complete, compiled successfully
- [ ] **Test 1**: Duration adaptation works automatically
- [ ] **Test 2**: Sensor retry with exponential backoff (5s, 10s, 15s)
- [ ] **Test 3**: Operation retry framework (1s, 2s, 4s backoff)
- [ ] **Test 4**: Alerts marked RESOLVED, history preserved
- [ ] **Test 5**: Water sensor failures detected with comprehensive errors
- [ ] **Test 6**: Pump rejections trigger comprehensive alerts
- [ ] **Test 7**: Temperature sensor status logged
- [ ] **Test 8**: ERROR state resolves (not clears) alerts
- [ ] **Test 9**: Backward compatibility verified

---

## 4-Part Error Message Verification

Every alert must include all 4 parts:

1. **Root Cause** (Technical): What actually failed
   - Example: "SafetyGate rejected: Duration 45s exceeds max 30s"

2. **User Action** (Non-technical): What user should do
   - Example: "Check sensor wiring, verify sensor power, test with multimeter"

3. **Operation Context**: What the system was doing
   - Example: "pH correction attempt 3/5, injection phase"

4. **Recovery Plan**: What system will do next
   - Example: "System will retry with adapted duration 30s"

**Verification**: Check serial logs for each alert - all 4 fields must be populated.

---

## Debugging Tips

### View Alert History
- Status reports print every 30 seconds
- Check "Active Alerts" section (count + details)
- Check "Resolved Alerts" section (historical errors)

### Monitor Retry Attempts
- Enable verbose mode in web UI
- Watch for "Retrying operation" logs
- Verify backoff delays match expected values

### Check Compile Flag
- Search logs for: `PLANTOS_ENHANCED_ERROR_HANDLING`
- Verify `ENHANCED_ERROR_HANDLING_ENABLED = true`

### Force ERROR State
- Use PSM checker component
- Trigger calibration failure
- Disconnect critical sensors

---

## Known Limitations

1. **Hardware Required**: Tests require actual hardware (pumps, sensors, actuators)
2. **Manual Verification**: No automated test framework (yet)
3. **Timing Sensitive**: Backoff delays must be verified with stopwatch/logs
4. **One Hour Pruning**: Resolved alert cleanup can't be tested without waiting 1 hour

---

## Next Steps After Testing

Once all tests pass:

1. **Document Results**: Record test outcomes in this file
2. **Update FSMINFO.md**: If any FSM behavior changed
3. **Merge to Main**: Enhanced error handling ready for production
4. **Monitor 24-Hour Run**: Verify stability under real conditions
5. **Consider Additional Features**:
   - Network failure retry (WiFi reconnection)
   - I2C bus recovery (after bus hang)
   - Automatic calibration retry (on sensor drift)

---

**Last Updated**: 2026-01-05
**Test Status**: Ready for hardware verification
**Estimated Test Time**: 2-3 hours (all 9 tests)
