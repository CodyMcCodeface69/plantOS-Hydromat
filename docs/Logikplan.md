# Implementierungsplan: Hydromat Steuerungslogik

## Kontext

Drei Spezifikationsdokumente definieren ein umfassendes Upgrade der PlantOS-Steuerung:
- **pH_Regellogik.md**: Adaptive K-Faktor-Dosierung, EMA-Selbstkalibrierung, 70L-optimierte Misch-/Stabilisierungszeiten
- **EC_Feeding_Logik.md**: EC-getriggerte Düngung (statt zeitbasiert), adaptives EC_K_feed, CalendarManager-Erweiterung
- **Steuerungslogik.md**: Betriebsmodi (AUTO/NIGHT/PAUSE/ERROR/VACATION), 3-Stufen-Sensorfehlerbeh., Alert-System, Sequenzierung

**Aktueller Stand**: 16 FSM-States, pH-Korrektur mit Platzhalterformel, zeitbasiertes Feeding, EC wird gelesen aber nicht für Logik genutzt. PSM speichert nur `bool` (kein `float`/`int32_t`).

**Ziel**: Biologisch fundierte, selbstkalibrierende pH/EC-Regelung mit autonomem 2-Wochen-Betrieb.

**WICHTIG — Sprachregel**:
- **Plan-Dokument**: Deutsch
- **Code** (Variablennamen, Kommentare, Log-Nachrichten, Docstrings, Commits): **ENGLISCH**
- Bestehende Codebase ist durchgängig englisch — das bleibt so

---

## Phase 1: PSM Float/Int Persistenz (Fundament)

**Warum zuerst**: Jede spätere Phase braucht NVS-Persistenz für K-Faktoren, Timestamps, Zähler.

**Dateien**:
- `components/persistent_state_manager/persistent_state_manager.h` — `saveFloat()`, `loadFloat()`, `saveInt32()`, `loadInt32()` hinzufügen
- `components/persistent_state_manager/persistent_state_manager.cpp` — Implementierung analog zu `saveState(bool)` mit `make_preference<float>` / `make_preference<int32_t>`

**Details**:
```cpp
bool saveFloat(const char* key, float value);
float loadFloat(const char* key, float default_value = 0.0f);
bool saveInt32(const char* key, int32_t value);
int32_t loadInt32(const char* key, int32_t default_value = 0);
```

Gleiches Pattern wie `saveState`/`loadState` (Zeile 226-265): `fnv1_hash(key)` → `make_preference<T>` → `save()` / `load()`.

**Test**: Compile, Float schreiben + nach Reboot lesen, Wert muss überleben.

---

## Phase 2: Adaptiver pH K-Faktor

**Warum**: Ersetzt die Platzhalterformel `0.5mL per 0.1pH per 10L`. Kann sofort mit existierender pH-Korrektur getestet werden.

**Dateien**:
- `components/plantos_controller/controller.h` — Neue Member-Variablen + Helper
- `components/plantos_controller/controller.cpp` — Formel ersetzen, K-Update implementieren

**Neue Member in `controller.h`** (nach Zeile 466):
```cpp
// Adaptive pH K-factor (pH_Regellogik.md Section 4)
float ph_K_{0.07f};                         // Current K-factor (EMA-smoothed)
static constexpr float PH_K_EMA_ALPHA = 0.20f;
static constexpr float PH_K_MIN_CLAMP = 0.01f;
static constexpr float PH_K_MAX_CLAMP = 0.50f;
static constexpr float PH_CORRECTION_TARGET = 5.85f;  // Target pH for injection
static constexpr float PH_MIN_DOSE_ML = 0.5f;
static constexpr float PH_MAX_DOSE_ML = 5.0f;
float ph_cycle_start_ph_{0.0f};             // pH before first injection in cycle
float ph_cycle_total_ml_{0.0f};             // Total mL dosed in correction cycle
bool ph_cycle_water_filled_{false};         // Guard: water fill during cycle
bool ph_cycle_aborted_{false};              // Guard: cycle was aborted
static constexpr const char* NVS_KEY_PH_K = "PhK";
```

**Neue Helper-Methode**:
```cpp
void updatePhKFactor(float ph_before, float ph_after, float ml_total);
```

**Änderungen in `controller.cpp`**:

1. **`calculateAcidDoseML()`** ersetzen:
   - Neue Formel: `dose_ml = (current_ph - PH_CORRECTION_TARGET) * tank_volume_L * ph_K_`
   - `tank_volume_L` via `hal_->getTankVolumeDelta()` (oder `getTotalTankVolume()` bei Reservoir-Change)
   - Clamp: `constrain(dose_ml, PH_MIN_DOSE_ML, PH_MAX_DOSE_ML)`

2. **`setup()`**: `ph_K_ = psm_->loadFloat(NVS_KEY_PH_K, 0.07f);`

3. **`handlePhCalculating()`**: `ph_cycle_start_ph_` setzen bei erstem Versuch (`ph_attempt_count_ == 0`), `ph_cycle_total_ml_ = 0`

4. **`handlePhInjecting()`**: `ph_cycle_total_ml_ += ph_dose_ml_` akkumulieren

5. **Nach finalem PH_MEASURING** (wenn pH im Fenster oder max Versuche): `updatePhKFactor()` aufrufen

6. **`updatePhKFactor()`** implementieren mit Guard-Bedingungen:
   - `ph_delta > 0.05f`, `ml_total > 0.1f`, `!ph_cycle_water_filled_`, `!ph_cycle_aborted_`
   - `K_observed = ml_total / (ph_delta * tank_volume_L)` → Clamp → EMA → NVS save

7. **`calculatePhMixingDuration()`** anpassen: Lineare Skalierung auf `70L → 600000ms (10 min)` statt `70L → 120000ms (2 min)`

8. **pH-Trigger-Schwelle**: CalendarManager `target_ph_max` als Trigger verwenden (6.3 Veg / 6.4 Blüte, aus Schedule), `PH_CORRECTION_TARGET = 5.85f` als Zielwert

**Test**: pH-Korrektur manuell starten, Logs prüfen (Formel, K-Update), K nach Reboot wiederherstellen.

---

## Phase 3: CalendarManager EC-Erweiterung + Schedule-Update

**Warum**: EC-Feeding braucht `ec_target` und `ec_tolerance` pro Grow-Tag. Die Werte kommen aus der `Hydroponik_Zielwerttabelle.csv`.

**Dateien**:
- `components/calendar_manager/CalendarManager.h` — `DailySchedule` erweitern
- `components/calendar_manager/CalendarManager.cpp` — JSON-Parsing erweitern (+ `JSON_OBJECT_SIZE` auf 10 erhöhen)
- `plantOS.yaml` — Schedule-JSON komplett mit EC-Zielwerten aktualisieren

### 3a: DailySchedule erweitern

**Neue Felder in `DailySchedule`** (nach Zeile 27):
```cpp
float ec_target;          // Target EC in mS/cm (e.g., 1.20)
float ec_tolerance;       // Tolerance band in mS/cm (e.g., 0.20)
// ec_min_trigger = ec_target - ec_tolerance (feeding trigger)
// ec_max_alarm = ec_target + ec_tolerance * 1.5 (over-concentration alert)
```

**Einheit: mS/cm** — konsistent mit Zielwerttabelle und Spezifikationsdokumenten. Falls HAL `readEC()` in uS/cm liefert, wird im Controller konvertiert (`ec_mS = ec_uS / 1000.0f`).

Default-Werte im Konstruktor: `ec_target(0.0f), ec_tolerance(0.20f)` — backward-kompatibel, `ec_target == 0` = EC-Feeding deaktiviert.

**JSON-Parsing** (`CalendarManager.cpp` Zeile 98-127): Neue Felder mit Defaults parsen:
```cpp
float ec_target = obj["ec_target"] | 0.0f;
float ec_tolerance = obj["ec_tolerance"] | 0.20f;
```
`JSON_OBJECT_SIZE(8)` → `JSON_OBJECT_SIZE(10)` (Zeile 79).

### 3b: Schedule-JSON mit EC-Zielwerten aktualisieren

Die Zielwerte stammen aus `Hydroponik_Zielwerttabelle.csv`, angepasst an den 3-Wochen-Veg-Zyklus des CalendarManagers.

**Mapping CSV (18 Wochen, 8 Wochen Veg) → CalMan (120 Tage, 3 Wochen Veg)**:

| CalMan Tage | Phase | CSV-Phase (komprimiert) | ec_target | ec_tolerance | ph_min | ph_max |
|---|---|---|---|---|---|---|
| 1–7 | Keimling | Woche 1–2 (Keimling) | 0.50 | 0.10 | 5.8 | 6.2 |
| 8–10 | Jungpflanze | Woche 3–4 (Bewurzelung) | 0.80 | 0.20 | 5.8 | 6.2 |
| 11–14 | Fruehe Veg | Woche 5–6 (Fruehe Veg) | 1.20 | 0.20 | 5.8 | 6.2 |
| 15–21 | Volle Veg | Woche 7–8 (Volle Veg) | 1.60 | 0.20 | 5.8 | 6.2 |
| 22–28 | Stretch | Woche 9 (Stretch/Vorbluete) | 1.80 | 0.20 | 5.8 | 6.2 |
| 29–42 | Fruehe Bluete | Woche 10–11 | 2.00 | 0.20 | 6.0 | 6.4 |
| 43–77 | Mittlere Bluete | Woche 12–14 (Peak) | 2.20 | 0.20 | 6.0 | 6.4 |
| 78–98 | Spaete Bluete | Woche 15–16 | 1.80 | 0.20 | 6.0 | 6.3 |
| 99–112 | Reifung/Pre-Flush | Woche 17 | 1.20 | 0.20 | 5.8 | 6.2 |
| 113–116 | Pre-Flush | Uebergang | 0.60 | 0.20 | 5.8 | 6.2 |
| 117–120 | Flush | Woche 18 (nur Wasser) | 0.15 | 0.15 | 5.5 | 6.5 |

**Begruendung der Komprimierung**: Die CSV hat 8 Wochen Veg (56 Tage), der CalMan nur 3 Wochen (21 Tage). Keimling bleibt 1 Woche, dann werden die verbleibenden 2 Wochen auf Jungpflanze (3 Tage), Fruehe Veg (4 Tage) und Volle Veg (7 Tage) aufgeteilt. Die Bloom-Phasen bleiben zeitlich unveraendert.

**pH-Werte ebenfalls aktualisieren**: Die bestehende Schedule hat durchgaengig `ph_min: 5.8, ph_max: 6.0`. Laut Zielwerttabelle aendert sich das pH-Fenster in der Bluete auf `6.0–6.4` (Fruehe/Mittlere Bluete) bzw. `6.0–6.3` (Spaete Bluete).

**JSON-Format pro Eintrag** (Beispiel Day 29):
```json
{"day": 29, "ph_min": 6.0, "ph_max": 6.4, "dose_A_ml_per_L": 0.64, "dose_B_ml_per_L": 1.28, "dose_C_ml_per_L": 1.92, "ec_target": 2.00, "ec_tolerance": 0.20, "light_on_time": 960, "light_off_time": 240}
```

**Alle 120 Tage** werden in `plantOS.yaml` (Zeile 1665–1785) aktualisiert. Die bestehenden `dose_X_ml_per_L`-Werte bleiben unveraendert (nur `ec_target`, `ec_tolerance`, `ph_min`, `ph_max` werden angepasst).

**Test**: Compile, Schedule laden, `get_today_schedule().ec_target` im Log pruefen.

---

## Phase 4: EC-Feeding FSM States + Adaptives EC_K

**Abhängigkeiten**: Phase 1 (Float NVS) + Phase 3 (CalendarManager EC-Felder)

**Dateien**:
- `components/plantos_controller/controller.h` — 5 neue States + EC-Variablen
- `components/plantos_controller/controller.cpp` — Handler + Switch-Dispatch + state_names[]
- `components/plantos_controller/led_behavior.h/cpp` — EC-States auf LED-Behaviors mappen

**Neue States** (am Ende des Enums anhängen, wegen positionalem `state_names[]`):
```cpp
EC_PROCESSING,   // Check EC, decide if feeding needed
EC_CALCULATING,  // Calculate nutrient doses from EC delta
EC_FEEDING,      // Sequential pump A → B → C (reuse feeding pattern)
EC_MIXING,       // Air pump mixing (5 min)
EC_MEASURING     // Re-measure EC, K-factor update
```

**Neue Member-Variablen**:
```cpp
// EC Adaptive K-factor (EC_Feeding_Logik.md Section 6)
float ec_K_feed_{0.15f};
static constexpr float EC_K_EMA_ALPHA = 0.20f;
static constexpr float EC_K_MIN_CLAMP = 0.02f;
static constexpr float EC_K_MAX_CLAMP = 0.50f;
float ec_pre_feeding_{0.0f};       // EC before feeding
float ec_total_ml_per_L_{0.0f};    // Total mL/L dosed in cycle
uint8_t ec_attempt_count_{0};
static constexpr uint8_t MAX_EC_ATTEMPTS = 3;
int64_t last_ec_feeding_timestamp_{0};
static constexpr int64_t EC_MIN_INTERVAL_S = 14400;  // 4 hours
float ec_dose_A_ml_{0.0f};
float ec_dose_B_ml_{0.0f};
float ec_dose_C_ml_{0.0f};
bool ec_cycle_water_filled_{false};
bool auto_ec_check_pending_{false};  // Flag after WATER_FILLING
static constexpr const char* NVS_KEY_EC_K = "EcK";
```

**Handler-Logik**:

1. **`handleEcProcessing()`**:
   - EC lesen via `hal_->readEC()`
   - Guard-Checks: `hasECValue()`, nicht EMPTY, nicht NIGHT, 4h-Intervall, kein pH-Korrektur-Lauf
   - Wenn `ec_target == 0` (nicht konfiguriert) → IDLE
   - Wenn EC > ec_max → IDLE (Verdunstungskonzentration, warten)
   - Wenn EC im Fenster [ec_min, ec_max] → IDLE
   - Wenn EC < ec_min → EC_CALCULATING

2. **`handleEcCalculating()`**:
   - `delta_ec = ec_target - ec_current`
   - `total_ml_per_L = delta_ec / ec_K_feed_`
   - A:B:C Verhältnis aus CalendarManager beibehalten:
     ```
     r_total = r_A + r_B + r_C
     dose_A = (r_A / r_total) * total_ml_per_L * tank_volume_L
     ```
   - Hard-Cap: `min(dose_X, calendar.dose_X_ml_per_L * tank_volume_L)`
   - → EC_FEEDING

3. **`handleEcFeeding()`**: Gleiches sequentielles A→B→C Pattern wie `handleFeeding()`, aber mit EC-berechneten Dosen. **Shared Helper** extrahieren: `runPumpSequence(dose_A, dose_B, dose_C)`.

4. **`handleEcMixing()`**: 5 min Luftpumpe via Shelly Pattern, dann → EC_MEASURING

5. **`handleEcMeasuring()`**: EC messen, `updateEcKFactor()`, Loop (max 3×) oder → PH_PROCESSING (immer pH nach Düngung!)

**LED-Mapping**: EC-States auf bestehende Farben mappen (z.B. Orange für EC_FEEDING, wie FEEDING).

**Periodic EC-Check**: In `handleIdle()` (analog pH-Monitoring Block Zeile 531-600): 2h-Intervall, `hal_->hasECValue()` prüfen.

**Test**: EC-Feeding manuell triggern, Dosisberechnung in Logs, K-Update, Sequential Pumps.

---

## Phase 5: Post-Fill Sequenzierung

**Abhängigkeit**: Phase 4 (EC-States existieren)

**Kernprinzip aus Steuerungslogik.md Abschnitt 10.2**: Nach Wassernachfüllung immer EC vor pH!

**Dateien**:
- `components/plantos_controller/controller.cpp` — Completion-Handler anpassen
- `components/plantos_controller/controller.h` — Stabilisierungs-Timer

**Neue Member**:
```cpp
static constexpr uint32_t POST_FILL_STABILIZE_MS = 600000;   // 10 min
static constexpr uint32_t POST_FEED_STABILIZE_MS = 300000;   // 5 min
```

**Sequenz nach WATER_FILLING complete**:
```
WATER_FILLING done
  → PH_PROCESSING (mit 10 min Stabilisierung in PH_MEASURING)
  → Aber ERST EC_PROCESSING prüfen!
```

Konkreter Ablauf:
1. `handleWaterFilling()` completion: `auto_ec_check_pending_ = true`, `auto_ph_correction_pending_ = true`
2. Transition zu EC_PROCESSING (nicht direkt PH_PROCESSING)
3. EC_PROCESSING/EC_MEASURING completion: `auto_ph_correction_pending_` → PH_PROCESSING
4. PH_PROCESSING completion → IDLE

**Guard-Flags** in pH-/EC-Cycle-Tracking setzen (`ph_cycle_water_filled_`, `ec_cycle_water_filled_`), wenn während eines Zyklus ein WATER_FILLING Event passiert.

**Test**: Water fill auslösen, prüfen ob EC-Check → EC-Feeding → pH-Korrektur in korrekter Reihenfolge.

---

## Phase 6: Betriebsmodi (Vacation, Night aus Calendar, Auto-Fill)

**Abhängigkeit**: Phase 1 (NVS)

**Dateien**:
- `components/plantos_controller/controller.h` — Vacation-Mode Variablen
- `components/plantos_controller/controller.cpp` — Modifikatoren in Dosier-/Retry-Logik
- `components/plantos_controller/__init__.py` — YAML-Config-Optionen
- `plantOS.yaml` — WebUI Buttons/Toggles

**Vacation Mode** (Modifier, kein eigener FSM-State):
```cpp
bool vacation_mode_{false};
static constexpr float VACATION_DOSE_MULTIPLIER = 0.70f;
static constexpr uint8_t VACATION_MAX_RETRIES = 5;  // vs. 3 normal
static constexpr const char* NVS_KEY_VACATION = "VacMode";
```
- Dosierungen: `dose_ml *= VACATION_DOSE_MULTIPLIER` wenn aktiv
- Retry-Limits: 5 statt 3
- NVS-persistent

**Night Mode aus CalendarManager**: `isNightModeHours()` erweitern um `calendar_manager_->get_today_schedule().light_off_time` / `light_on_time` zu nutzen (Fallback: bestehende YAML-Konfiguration).

**Auto-Fill Toggle**: Existiert konzeptuell bereits (`auto_feeding_enabled_`). Separaten `auto_fill_enabled_` Toggle für Magnetventil bei LOW-Sensor hinzufügen, NVS-persistent.

**Runtime Parameter Overrides** (WebUI Number-Inputs):
```cpp
float override_dose_multiplier_{1.0f};  // Global dose scaling
```
Weitere Overrides (ph_target, ec_target) als optionale Übersteuerung der CalendarManager-Werte.

**Test**: Vacation-Mode toggeln, konservative Dosierung in Logs prüfen. Night-Mode aus Calendar testen.

---

## Phase 7: 3-Stufen Sensor-Fehlerbehandlung + Temperatur-Alerts

**Abhängigkeit**: Phase 1 (NVS)

**Dateien**:
- `components/plantos_controller/controller.h` — SensorHealth-Struct formalisieren
- `components/plantos_controller/controller.cpp` — Fehlerbehandlung in Sensor-Reads
- `components/plantos_controller/CentralStatusLogger.h/cpp` — AlertLevel enum

**Vorhandene Infrastruktur nutzen**: `SensorRetryState` (Zeile 770-800) existiert bereits! Erweitern um 3-Stufen-Modell:

```cpp
enum class SensorTier { OK, STOPPED, RETRYING, FAILED };

struct SensorHealth {
    SensorTier tier{SensorTier::OK};
    uint8_t retry_count{0};
    uint8_t max_retries{3};       // 5 in vacation mode
    uint32_t last_retry_time{0};
    static constexpr uint32_t RETRY_INTERVAL_MS = 60000;  // 60s

    void recordFailure();
    void recordSuccess();
    bool canRetry(uint32_t now) const;
};

SensorHealth ph_health_;
SensorHealth ec_health_;
SensorHealth temp_health_;
```

**Temperatur-Alerts** in `handleIdle()` oder Status-Report-Loop:
- `< 15°C` → WARNING
- `> 28°C` → WARNING
- `> 32°C` → CRITICAL

**Plausibilitätsprüfungen**:
- pH: 3.0–10.0, Änderung > 1.0 pH/min = Sensorfehler
- EC: 0–5.0 mS/cm, Sprung > 1.0 mS/cm zwischen Messungen = verdächtig
- Wasserstand: HIGH=true + EMPTY=true = logischer Fehler

**Kalibrierungs-Erinnerungen**:
```cpp
int64_t last_ph_calibration_ts_{0};
int64_t last_ec_calibration_ts_{0};
static constexpr int64_t PH_CALIB_INTERVAL_S = 1209600;   // 14 Tage
static constexpr int64_t EC_CALIB_INTERVAL_S = 2592000;   // 30 Tage
```
NVS-persistent, täglicher Check in `handleIdle()`, WARNING bei Überfälligkeit.

**Test**: Sensor-Wert außerhalb Plausibilitätsbereich simulieren, 3-Stufen-Progression in Logs prüfen.

---

## Phase 8: Alert-Service + Enhanced Reboot Recovery

**Abhängigkeit**: Phase 7

**Dateien**:
- Neues Component: `components/alert_service/` — `alert_service.h`, `alert_service.cpp`, `__init__.py`
- `components/plantos_controller/controller.h` — AlertService DI
- `components/plantos_controller/controller.cpp` — Reboot-Recovery erweitern
- `components/plantos_controller/__init__.py` — AlertService optional DI

**AlertService** (abstrakte Schicht für zukünftige Telegram-Integration):
```cpp
class AlertBackend {
public:
    virtual void sendAlert(AlertLevel level, const std::string& title, const std::string& msg) = 0;
};

class LogAlertBackend : public AlertBackend { /* ESP_LOG output */ };
// Future: TelegramAlertBackend, HAAlertBackend

class AlertService : public Component {
    void alert(AlertLevel level, const std::string& title, const std::string& msg);
    // Rate limiting: same alert max 1x per 30 min
};
```

**Enhanced Reboot Recovery** in `handleInit()`:
- Letzten FSM-State aus NVS laden
- Sensor-Check auf Boot (alle Sensoren einmal lesen)
- Cooldowns auf Maximum setzen (Anti-Double-Dosing nach Reboot)
- INFO-Alert "System rebooted"

**Test**: AlertService erstellen, Log-Backend prüfen. Reboot während Dosierung, Recovery in Logs verifizieren.

---

## Phasen-Abhängigkeiten

```
Phase 1: PSM Float/Int
    |
    +-- Phase 2: Adaptiver pH K-Faktor
    |
    +-- Phase 3: CalendarManager EC-Felder
    |       |
    |       +-- Phase 4: EC-Feeding FSM
    |               |
    |               +-- Phase 5: Post-Fill Sequenzierung
    |
    +-- Phase 6: Betriebsmodi (Vacation, Night, Auto-Fill)
    |
    +-- Phase 7: 3-Stufen Sensorfehlerbeh. + Temp-Alerts
            |
            +-- Phase 8: Alert-Service + Reboot Recovery
```

Phasen 2, 3, 6, 7 können nach Phase 1 **parallel** bearbeitet werden.

---

## Kritische Dateien (Zusammenfassung)

| Datei | Phasen | Änderungstyp |
|-------|--------|-------------|
| `persistent_state_manager.h/.cpp` | 1 | Float/Int-Methoden hinzufügen |
| `controller.h` | 2, 4, 5, 6, 7 | States, Variablen, Handler-Deklarationen |
| `controller.cpp` | 2, 4, 5, 6, 7 | Handler-Implementierung, Formel, Switch |
| `CalendarManager.h/.cpp` | 3 | DailySchedule + Parsing erweitern |
| `led_behavior.h/.cpp` | 4 | EC-State LED-Mappings |
| `alert_service/` (neu) | 8 | Neues Component |
| `plantOS.yaml` | 3, 6 | Schedule-JSON, WebUI |
| `FSMINFO.md` | 4, 5 | Authoritative FSM-Doku aktualisieren! |

---

## Verifikation (End-to-End)

1. **pH-Korrektur**: Manuell triggern → adaptive Dosis in Log → K-Update → K überlebt Reboot
2. **EC-Feeding**: Manuell triggern → EC-basierte Dosis → A:B:C Ratio korrekt → Hard-Cap greift → K_feed-Update
3. **Post-Fill-Sequenz**: Water Fill → 10min Stabilisierung → EC-Check → (Feeding falls nötig) → pH-Check → IDLE
4. **Vacation Mode**: Toggle → 70% Dosierung → 5 statt 3 Retries
5. **Sensor-Fehler**: pH-Sensor abklemmen → STOPP → RETRY (3×) → ERROR
6. **Reboot Recovery**: Reboot während Dosierung → Cooldowns max → kein Double-Dosing
7. **Full Build**: `task build` kompiliert ohne Fehler nach jeder Phase
