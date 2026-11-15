#pragma once
// PSM event IDs: the one list of names written with logEvent() and compared by the boot
// recovery, the IDLE/NIGHT hold check and clearPsmEventIf(). Use these constants on both
// sides; a literal on one side only is how WATER_FILL / WATER_FILLING drifted apart (fixed
// 2026-11-15). Recovery per event: FSMINFO.md "Persistent State Manager (PSM) Events".
// The IDs are stored in NVS (CriticalEventLog::eventID, max 31 chars): renaming one makes an
// event written by older firmware unknown to the recovery.

namespace esphome {
namespace persistent_state_manager {
namespace psm_event {

// Hold states: restored after INIT
constexpr const char *STATE_SHUTDOWN = "STATE_SHUTDOWN";
constexpr const char *STATE_PAUSE = "STATE_PAUSE";

// Water operations: recovery switches the water actuators OFF, clears the event, alerts
constexpr const char *WATER_FILL = "WATER_FILL";              // startFillTank
constexpr const char *WATER_EMPTY = "WATER_EMPTY";            // startEmptyTank
constexpr const char *FEED_OPERATION = "FEED_OPERATION";      // startFeed (fill -> nutrients -> pH)
constexpr const char *RESERVOIR_CHANGE = "RESERVOIR_CHANGE";  // startReservoirChange (drain -> fill -> ...)

// Dosing / measuring: recovery only alerts (pumps are GPIO outputs, off after a reset)
constexpr const char *PH_CORRECTION = "PH_CORRECTION";
constexpr const char *PH_CALIBRATION = "PH_CALIBRATION";
constexpr const char *PH_CRITICAL = "PH_CRITICAL";  // status 2, cleared by the next good measurement
constexpr const char *FEEDING = "FEEDING";
constexpr const char *EC_FEEDING = "EC_FEEDING";
constexpr const char *MOTHER_MIX = "MOTHER_MIX";

}  // namespace psm_event
}  // namespace persistent_state_manager
}  // namespace esphome
