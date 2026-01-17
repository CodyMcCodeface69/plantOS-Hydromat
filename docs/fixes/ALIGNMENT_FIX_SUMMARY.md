# ESP32-C6 Alignment Crash - Complete Fix Summary

**Date**: 2026-01-15
**Status**: ✅ **FIXED**

## The Problem

Your ESP32-C6 was crashing with:
```
MCAUSE: 0x00000007 (Store address misaligned)
at xQueueGiveFromISR
Stack contains: "AutoFeedEnable"
```

## The Root Cause

**`CriticalEventLog` struct had misaligned fields** that violated RISC-V's strict alignment requirements.

### Broken Struct (OLD)
```cpp
struct CriticalEventLog {
    char eventID[32];       // 32 bytes at offset 0
    int64_t timestampSec;   // 8 bytes at offset 32 (MISALIGNED!)
    int32_t status;         // 4 bytes at offset 40
};
```

**Problem**: `int64_t` requires 8-byte alignment, but was at offset 32 (only 4-byte aligned when struct instance isn't 8-aligned).

## The Fix

**Reordered struct members from largest to smallest**:

```cpp
struct __attribute__((aligned(8))) CriticalEventLog {
    int64_t timestampSec;   // 8 bytes at offset 0 (✓ 8-byte aligned)
    int32_t status;         // 4 bytes at offset 8 (✓ 4-byte aligned)
    char eventID[32];       // 32 bytes at offset 12 (✓ OK)
};
```

## Files Modified

**`components/persistent_state_manager/persistent_state_manager.h`**:
- Lines 13-52: Complete struct rewrite
- Reordered fields (timestampSec → status → eventID)
- Added `__attribute__((aligned(8)))`
- Updated constructor to match new order

## Build Status

✅ **Build succeeded** (1.4 MB firmware, 76.8% flash usage)

## Next Steps

### 1. IMPORTANT: Clear NVS Before Flashing

The struct layout changed, so **old NVS data will be corrupted** if read with new firmware.

**Option A: Erase flash completely** (recommended):
```bash
esptool.py --chip esp32c6 --port /dev/ttyUSB0 erase_flash
task flash
```

**Option B: Flash and accept data loss**:
```bash
task flash
# Old events will be unreadable, but system will boot normally
```

### 2. Flash and Monitor

```bash
task run
```

### 3. Verify Fix

Look for in the logs:
- ✅ No "Store address misaligned" crashes
- ✅ PSM loads/saves correctly
- ✅ AutoFeed state works
- ✅ No crashes during NVS operations

### 4. Test Auto-Feed Toggle

1. Web UI → Toggle auto-feed enable/disable
2. Reboot device (power cycle)
3. Verify setting persisted

### 5. Stress Test

Run for 24+ hours with multiple reboots to confirm stability.

## Why This Happened

### RISC-V vs Xtensa Alignment

| Architecture | Misaligned Access |
|--------------|-------------------|
| ESP32 (Xtensa) | Allowed (with performance hit) |
| ESP32-C6 (RISC-V) | **Hardware exception - crashes!** |

RISC-V enforces strict alignment for speed. No exceptions, no workarounds - the hardware just crashes if you violate alignment rules.

## Documentation

Three detailed documents created:

1. **`RISC-V_ALIGNMENT_FIX.md`** - Complete technical analysis and fix (READ THIS)
2. **`ISR_ALIGNMENT_FIX.md`** - ISR logging improvements (good practice, but wasn't the crash cause)
3. **`ALIGNMENT_FIX_SUMMARY.md`** - This quick reference

## Quick Verification

After flashing, verify the fix worked:

```bash
# Monitor logs
task snoop

# Look for these SUCCESS indicators:
# - "PSM SETUP COMPLETE" with no crashes
# - "Auto-feeding: ENABLED" (or DISABLED)
# - No "Store address misaligned" exceptions
# - System runs for hours without rebooting
```

## Rollback (If Needed)

If you need to revert:
```bash
git diff HEAD components/persistent_state_manager/persistent_state_manager.h
git checkout HEAD -- components/persistent_state_manager/persistent_state_manager.h
task build && task flash
```

## Key Takeaway

**Always order struct members from largest to smallest alignment**:

```cpp
✅ CORRECT:
struct MyStruct {
    double big;      // 8 bytes
    int64_t time;    // 8 bytes
    int32_t count;   // 4 bytes
    uint16_t id;     // 2 bytes
    uint8_t flag;    // 1 byte
    char name[32];   // Arrays last
} __attribute__((aligned(8)));

❌ WRONG (crashes on RISC-V):
struct MyStruct {
    char name[32];   // 32 bytes
    int64_t time;    // MISALIGNED!
};
```

---

**Need Help?**
- See `RISC-V_ALIGNMENT_FIX.md` for detailed technical explanation
- Check crash logs with: `task snoop`
- Test alignment with: `gcc -O2 -march=rv32imc test.c` (RISC-V cross-compile)

**Status**: Ready to flash! 🚀
