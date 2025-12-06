# Layer Interaction Diagram

This diagram shows how the three architectural layers interact with each other.

```mermaid
graph TB
    subgraph Layer1["Layer 1: Unified Controller"]
        Controller[PlantOSController]
        LED[LED Behavior System]
        StatusLogger[CentralStatusLogger]
    end

    subgraph Layer2["Layer 2: Safety Gate"]
        SafetyGate[ActuatorSafetyGate]
    end

    subgraph Layer3["Layer 3: Hardware Abstraction"]
        HAL[HAL Interface]
        ESPHomeHAL[ESPHomeHAL Implementation]
    end

    subgraph Services["Supporting Services"]
        PSM[PersistentStateManager]
        Calendar[CalendarManager]
    end

    subgraph Hardware["Physical Hardware"]
        LED_HW[WS2812 RGB LED]
        PH_SENSOR[EZO pH Sensor]
        PUMPS[Pumps & Valves]
    end

    Controller -->|requestPump/requestValve| SafetyGate
    Controller -->|readPH| HAL
    Controller -->|getSchedule| Calendar
    Controller -->|logEvent| PSM
    Controller -->|owns| StatusLogger
    LED -->|setSystemLED| HAL

    SafetyGate -->|executeCommand approval| HAL
    SafetyGate -->|setPump/setValve| HAL

    HAL -->|interface| ESPHomeHAL
    ESPHomeHAL -->|control| LED_HW
    ESPHomeHAL -->|read| PH_SENSOR
    ESPHomeHAL -->|actuate| PUMPS

    style Layer1 fill:#e1f5e1
    style Layer2 fill:#fff4e1
    style Layer3 fill:#e1f0ff
    style Services fill:#f0e1ff
    style Hardware fill:#ffe1e1
```

## Key Interactions

### Controller → SafetyGate
- **Purpose**: Request actuator operations with safety validation
- **Methods**: `requestPump()`, `requestValve()`, `turnOffAllPumps()`
- **Flow**: Controller requests → SafetyGate validates → HAL executes

### Controller → HAL
- **Purpose**: Direct sensor reading and LED control
- **Methods**: `readPH()`, `hasPhValue()`
- **Flow**: Controller reads → HAL wraps ESPHome component → returns value

### SafetyGate → HAL
- **Purpose**: Execute approved actuator commands
- **Methods**: `setPump()`, `setValve()`, `getPumpState()`
- **Flow**: SafetyGate approves → calls HAL → HAL wraps GPIO/PWM

### Controller → Services
- **Purpose**: Access scheduling and persistence
- **Methods**:
  - `Calendar::getTodaySchedule()` - Get pH targets and nutrient doses
  - `PSM::logEvent()` / `clearEvent()` - Crash recovery logging
- **Flow**: Controller queries/logs → Service provides data/persistence

## Benefits of This Architecture

1. **Hardware Independence**: Controller doesn't know about ESP32, works with any HAL
2. **Safety Enforcement**: All hardware access flows through validation
3. **Testability**: Mock HAL for unit tests without hardware
4. **Clear Responsibility**: Each layer has single, well-defined purpose
5. **Maintainability**: Hardware changes don't require controller changes
