# Hydromat – pH-Regellogik (Architect Reference)

> Tankvolumen: **70 L** | Controller: ESP32 (ESPHome / Home Assistant) | Architektur: 3-Layer HAL (Controller → ActuatorSafetyGate → HAL)

---

## 1. Systemlogik: Der natürliche pH-Kreislauf

Das Hydromat-System hat einen vorhersehbaren, selbstverstärkenden pH-Kreislauf, der durch **zwei unabhängige Mechanismen** aufwärts treibt:

**Mechanismus A – Wasserverbrauch / Nachfüllung via Magnetventil:**
Pflanzen verbrauchen Wasser → Füllstand sinkt → Magnetventil öffnet → Leitungswasser (pH ~6,5–8,0 je nach Versorgungsgebiet) strömt nach → pH-Wert im Reservoir steigt durch die Verdünnung der angesierten Lösung.

**Mechanismus B – Nährstoffaufnahme (Sekundär):**
Pflanzen nehmen Anionen (vor allem Nitrat NO₃⁻) auf und scheiden OH⁻ aus → pH steigt langsam durch Pflanzenaktivität.

Beide Mechanismen treiben den pH **ausschließlich aufwärts**. Das Leitungswasser-pH liegt systembedingt über dem Zielfenster. Das bedeutet: **Eine pH-Up-Pumpe ist nicht nur nicht vorhanden – sie ist schlicht nicht nötig.** Das System ist korrekt als reines pH-Down-System ausgelegt.

Der operative Regelkreis des Hydromat sieht damit so aus:

```
Wasserverbrauch
      │
      ▼
EC sinkt unter Zielwert ──► FEEDING (Nährstoffpumpen A+B+C)
      │                              │
      ▼                              ▼
Magnetventil füllt nach        pH verändert sich
      │                        (durch Nährstoffe)
      ▼                              │
pH steigt (Leitungswasser)           │
      │                              │
      └──────────────────────────────┘
                    │
                    ▼
         PH_PROCESSING → Korrektur via pH-Down
                    │
                    ▼
              pH zurück auf 5,8–5,9
                    │
                    ▼
             Nächster Zyklus
```

---

## 2. Trigger-Hierarchie: Was wann wichtiger ist

Bei diesem Systemdesign sind **nicht alle Trigger gleichwertig**. Die Gewichtung nach Relevanz:

| Priorität | Trigger | Begründung |
|---|---|---|
| **1 (kritisch)** | Nach `WATER_FILLING` abgeschlossen | Frischwasser hebt pH direkt und sprunghaft; wichtigster Korrektur-Anlass |
| **2 (wichtig)** | Nach `FEEDING` abgeschlossen | Nährstoffkonzentrate können pH in beide Richtungen verschieben |
| **3 (Wächter)** | 2h-Periodiktimer | Erkennt unerwartete Drift zwischen den Ereignissen; in 70 L selten aktiv |

Beide ereignisbasierten Trigger (1+2) sind bereits im FSM implementiert (`auto_ph_correction_pending = true`). Das ist die richtige Priorität. Der 2h-Timer ist für 70 L korrekt bemessen – er findet in der Regel pH im Fenster vor und tut nichts.

---

## 3. Fenster- und Triggerparameter

| Parameter | Wert | Konfigurationsort |
|---|---|---|
| **Untere Alarmgrenze** | pH 5,5 | FSM Safety Check (PH_CALCULATING) |
| **Obere Alarmgrenze** | pH 6,5 | FSM Safety Check |
| **Korrektur-Trigger (Vegetation)** | pH > 6,3 | PH_PROCESSING → PH_MEASURING |
| **Korrektur-Trigger (Blüte)** | pH > 6,4 | PH_PROCESSING → PH_MEASURING |
| **Korrektur-Zielwert** | pH 5,8–5,9 | PH_CALCULATING (Zieldosis-Berechnung) |
| **Aktiver Driftbereich** | pH 5,8–6,3/6,4 | → kein Eingriff |
| **Minimaldosis (Skip-Threshold)** | < 0,5 ml | PH_CALCULATING |
| **Maximaldosis pro Einzel-Injektion** | 5 ml | ActuatorSafetyGate (MaxDuration AcidPump) |

**Kritisch:** Der Korrektur-Zielwert muss **5,8–5,9** sein, nicht 6,0. Höherer Zielwert schrumpft das Drift-Fenster und verhindert den Nährstoff-Sweep nach unten.

**Zur unteren Alarmgrenze:** Da das System kein pH-Up hat und der pH systembedingt nur steigt, ist pH < 5,5 ein seltenes, aber mögliches Ereignis nach sehr konzentrierter Nährstoffdosierung. Hier braucht es keinen automatischen Gegensteuer-Versuch – ein **Home Assistant Alert** mit manueller Intervention ist die korrekte Reaktion.

---

## 4. Dosierung: Dynamische Formel (tankvolumen-relativ)

Das Tankvolumen ist im Code bekannt und wird bereits für Nährstoffdosierungen verwendet – die pH-Dosierformel muss darauf aufbauen, nicht auf einem hardgecodeten 70-L-Wert.

### Formel

```
dose_ml = (pH_Ist – pH_Ziel) × tank_volume_L × K
```

`K` ist der **empirische Kalibrierkonstante** in `ml / (L × pH-Einheit)`. Sie fasst Säurestärke, Pufferkapazität der Nährlösung und Leitungswasser-Alkalinität in einem einzigen konfigurierbaren Wert zusammen.

### Warum K und nicht eine rein chemische Berechnung?

10% Salpetersäure (HNO₃) hat bekannte Eigenschaften:
- Dichte ≈ 1,054 g/ml → 1 ml enthält ~1,67 mmol H⁺ (starke Säure, vollständige Dissoziation)
- Rein chemisch wäre die pH-Änderung in reinem Wasser berechenbar

In der Praxis **dominiert die Pufferkapazität** der Lösung – nicht das Volumen allein:
- Carbonathärte (KH) des Leitungswassers (DE: typisch 5–20 °dKH) **neutralisiert Säure, bevor der pH fällt**
- Die Nährstofflösung selbst puffert zusätzlich
- Das Verhalten ist **nicht linear** mit dem pH-Delta

Deshalb ist K der einzig saubere Weg – und kann sich nach jedem Zyklus **selbst verbessern**.

### Startwert für K (10% HNO₃)

| Leitungswasser-Härte | KH | K-Startwert | Beispiel 70 L, Δ0,4 |
|---|---|---|---|
| Weiches Wasser | < 3 °dKH | 0,02 – 0,04 | 0,6 – 1,1 ml |
| Mittleres Wasser | 5–10 °dKH | 0,05 – 0,10 | 1,4 – 2,8 ml |
| Hartes Wasser (typisch DE) | 10–20 °dKH | 0,10 – 0,20 | 2,8 – 5,6 ml |

Empfohlener **Startwert: K = 0,07** (mittleres deutsches Leitungswasser). Nach dem ersten Zyklus übernimmt die adaptive Anpassung.

---

### Adaptives K: Selbstkalibrierung per EMA

Nach jedem abgeschlossenen Korrekturzyklus (alle Injektionen + finales PH_MEASURING) sind drei Werte bekannt:
- `ph_vor` – pH vor der ersten Injektion im Zyklus
- `ph_nach` – stabilisierter pH nach der letzten Messung
- `ml_gesamt` – Summe aller dosierten ml im Zyklus

Daraus lässt sich K rückberechnen:

```
K_beobachtet = ml_gesamt / ((ph_vor – ph_nach) × tank_volume_L)
```

Diesen Beobachtungswert nicht direkt übernehmen (einzelne Messung kann fehlerhaft sein), sondern als **Exponential Moving Average (EMA)** einpflegen:

```
K_neu = α × K_beobachtet + (1 – α) × K_alt
```

`α` (EMA-Gewicht) steuert, wie schnell K auf neue Beobachtungen reagiert:

| α | Verhalten |
|---|---|
| 0,10 | Träge – stabil, ignoriert Ausreißer gut |
| 0,20 | Empfohlen – balanciert Anpassungsgeschwindigkeit und Stabilität |
| 0,35 | Aggressiv – reagiert schnell auf Veränderungen der Wasserqualität |

Empfehlung: **α = 0,20**, via HA konfigurierbar. Damit wird ein neuer Messwert mit 20 % gewichtet, die Langzeiterfahrung mit 80 % – K konvergiert nach ~5 Zyklen auf den echten Wert.

### Vorteile gegenüber fixem K

- **Saisonale Anpassung:** Deutsches Leitungswasser variiert in KH je nach Jahreszeit und Versorgungsgebiet – K lernt mit
- **Nährstoffkonzentrationsabhängig:** Höhere EC = stärkere Pufferung = höheres K – wird automatisch abgebildet
- **Kein Re-Flash für Rekalibrierung** nach Reservoirwechsel oder Nährstoffwechsel

### Implementierungshinweis

```cpp
// --- Dosisberechnung (PH_CALCULATING) ---
float delta_ph = ph_vor - ph_ziel;
float dose_ml = delta_ph * tank_volume_L * K;
dose_ml = constrain(dose_ml, MIN_DOSE_ML, MAX_DOSE_ML);

// Zyklus-Akkumulator zurücksetzen
ph_zyklus_start = ph_vor;
ml_zyklus_gesamt = 0.0f;

// --- Akkumulation pro Injektion (PH_INJECTING) ---
ml_zyklus_gesamt += ml_diese_injektion;

// --- K-Update nach finalem PH_MEASURING (Zyklus abgeschlossen) ---
float ph_delta_beobachtet = ph_zyklus_start - ph_nach_stabil;

if (ph_delta_beobachtet > 0.05f && ml_zyklus_gesamt > 0.1f) {
    float k_beobachtet = ml_zyklus_gesamt / (ph_delta_beobachtet * tank_volume_L);
    k_beobachtet = constrain(k_beobachtet, 0.01f, 0.50f);  // Sanity-Clamp
    K = EMA_ALPHA * k_beobachtet + (1.0f - EMA_ALPHA) * K;
    saveToNVS("ph_k_factor", K);  // persistieren – überlebt Power-Cycles
}
```

### Guard-Bedingungen für den K-Update (wichtig)

Nicht jeder Zyklus liefert valide Daten für die Kalibrierung. K **nur aktualisieren wenn:**

| Bedingung | Begründung |
|---|---|
| `ph_delta_beobachtet > 0,05` | Kleinere Deltas sind Messrauschen, kein Signal |
| `ml_zyklus_gesamt > 0,1` | Minimale Dosismenge für sinnvolle Rückberechnung |
| Zyklus nicht durch Timeout oder ERROR abgebrochen | Unvollständige Zyklen verfälschen K |
| Kein Wasserauffüll-Event während des Zyklus | Frischwasser während der Korrektur verändert Pufferkapazität mid-cycle |
| `K_beobachtet` liegt in `[0,01 ; 0,50]` | Clamp vor dem EMA-Update, nicht danach |

Nach einer Leitungswasser-Nachfüllung ist Δ0,3–0,6 pH normal und der häufigste Korrekturanlass. Iterative Annäherung über die max-5-Attempts-Schleife ist **Standard, kein Ausnahmefall**.

---

## 5. Mischzeiten: Für 70 L anpassen

Der aktuelle FSM-Standard (2 Minuten PH_MIXING) ist für Sub-5-L-Systeme ausgelegt. Bei 70 L ist das **zu kurz** – Phosphorsäure ist dichter als Wasser und sinkt ohne ausreichende Mischzeit ab, was zu Falschmessungen an der Sonde führt.

| Tank | Empfohlene Mischzeit (PH_MIXING) |
|---|---|
| < 5 L | 2 min |
| 5–20 L | 3–4 min |
| 20–50 L | 5–7 min |
| **70 L (Hydromat)** | **8–10 min** |

**Empfehlung:** PH_MIXING-Timeout für Hydromat auf **600 Sekunden (10 min)** setzen.

---

## 6. Messsequenz: Stabilisierungszeit für 70 L

Der FSM wartet in PH_MEASURING 0,5 Minuten vor dem Sampling. Nach einer Injektion in 70 L:

- Empfohlene Stabilisierungswartezeit: **3–5 Minuten** (nach Ende PH_MIXING)
- Dann 5-minütiger Sampling-Zyklus (alle 5 Sekunden) wie im Standard

Gesamtdauer pro Korrektur-Runde damit ca. **15–20 Minuten**. Da Korrekturen in 70 L selten sind, kein Problem.

---

## 7. ActuatorSafetyGate-Konfiguration (AcidPump)

Die SafetyGate arbeitet mit **Laufzeit in Sekunden**, nicht mit ml. Die Umrechnung erfolgt über die kalibrierte Pumprate:

```cpp
// Pumpenrate ebenfalls als konfigurierbarer Parameter (ml/s)
float pump_rate_ml_per_s = 0.15;  // empirisch kalibrieren (Peristaltikpumpe variiert)

float duration_s = dose_ml / pump_rate_ml_per_s;
duration_s = min(duration_s, MAX_DURATION_S);  // SafetyGate-Cap greift zusätzlich
```

```cpp
// plantOS.yaml oder Initialisierung
id(actuator_safety)->setMaxDuration("AcidPump", 40);   // 40s bei 0,15 ml/s ≈ 6 ml Hard-Cap
id(actuator_safety)->setMinInterval("AcidPump", 900);  // min. 15 min zwischen Impulsen
```

| Parameter | Wert | Begründung |
|---|---|---|
| `MaxDuration` AcidPump | 40 s | Hard-Cap unabhängig von Berechnung; bei 0,15 ml/s ≈ 6 ml |
| `MinInterval` AcidPump | 900 s | Hardware-seitige Hysterese; überlebt Power-Cycles und State-Transitions |
| `pump_rate_ml_per_s` | empirisch | Peristaltik-Pumprate variiert je nach Schlauch-Innendurchmesser und Spannung |
| Soft-start | aktivieren | Schutz der Silikonschläuche vor Druckstößen |

**Beide Caps müssen konsistent sein:** `MAX_DOSE_PER_INJECTION` im Controller und `MaxDuration` im SafetyGate müssen aus derselben Pumprate berechnet werden, sonst schützt einer der beiden nicht sinnvoll.

`MinInterval` über das SafetyGate zu enforzen ist **bewusst Layer-2-Logik** – es wirkt unabhängig vom FSM-State und kann durch keine State-Transition umgangen werden.

---

## 8. Ausblick: EC-getriggerte Düngerlogik

Der aktuelle FSM nutzt `CalendarMgr` für Feeding-Events (zeitbasiert). Das Systemdesign des Hydromat sieht jedoch **EC-getriggerte Düngung** vor: Feeding erfolgt, wenn der EC-Wert das untere Ende des Zielfensters unterschreitet.

Das ist eine signifikante FSM-Änderung, die eine eigene Spezifikation braucht. Folgende Aspekte sind zu klären:

- Neuer Automatik-Trigger: `EC < EC_Ziel_Min` → `FEEDING` (analog zum bestehenden Wasserstand-Trigger für Auto-Feeding)
- Verhältnis zum bestehenden CalendarMgr: ablösen oder parallel betreiben?
- Hysterese: EC muss für X Minuten unter Schwellwert liegen, bevor Feeding getriggert wird (Vermeidung von Messschwankungen als Auslöser)
- Tageslock analog zum Auto-Feeding (NVS-backed, max. 1× täglich) oder mehrfach täglich erlauben?
- Sicherheitsprüfung: kein Feeding wenn Wasserstand EMPTY (Pumpen würden konzentriertes Nährstoffgemisch in leeren Tank dosieren)

→ **Vollständige Spezifikation: `EC_Feeding_Logik.md`**

---

## 9. Phasenspezifische Konfiguration (via Home Assistant)

| Parameter | Vegetation | Blüte |
|---|---|---|
| pH Korrektur-Trigger (oben) | 6,3 | 6,4 |
| pH Korrektur-Zielwert | 5,8 | 5,9 |
| pH Alert-Grenze (oben) | 6,5 | 6,5 |

Als Number-Slider in HA analog zur Nachtmodus-Konfiguration realisierbar (`02_xx_pH_Upper_Threshold`).

---

## 10. Sequenz-Übersicht (70-L-optimiert)

```
Primäre Trigger:
  WATER_FILLING abgeschlossen ──► auto_ph_correction_pending = true
  FEEDING abgeschlossen        ──► auto_ph_correction_pending = true
  IDLE (2h-Timer)              ──► PH_PROCESSING (Wächter)

PH_PROCESSING: pH lesen
  │
  ├─[5,6 ≤ pH ≤ 6,3/6,4] ──► IDLE  (Drift im Fenster, kein Eingriff)
  │
  ├─[pH < 5,5] ──► HA-Alert senden, kein Gegensteuer möglich ──► IDLE
  │
  └─[pH > 6,3/6,4]
          │
          ▼
    PH_MEASURING
    (3–5 min Stabilisierung, dann 5 min Sampling)
          │
          ▼
    PH_CALCULATING
    dose_ml = (pH_Ist – 5,85) × tank_vol_L × K
    duration_s = dose_ml / pump_rate_ml_per_s
    Cap: min 0,5 ml / max per SafetyGate MaxDuration
          │
    ┌─────┴────────────────┐
    │ Dosis < 0,5 ml       │ Dosis ≥ 0,5 ml
    ▼                      ▼
  IDLE              PH_INJECTING
                    (SafetyGate: max 40s, MinInterval 15 min)
                          │
                          ▼
                    PH_MIXING (10 min, Luftpumpe an)
                          │
                          ▼
                    PH_MEASURING (Loop, max 5×)
                          │
                          ▼
                    K-UPDATE (wenn Guard-Bedingungen erfüllt):
                    K_obs = ml_gesamt / (Δph × tank_vol_L)
                    K = α × K_obs + (1–α) × K  → saveToNVS
                          │
                          ▼
                        IDLE
```

---

## 11. Delta zur Standard-FSM

| Parameter | Standard-FSM | Hydromat (70 L) |
|---|---|---|
| PH_MIXING Timeout | 2 min | **10 min** |
| PH_MEASURING Stabilisierung | 0,5 min | **3–5 min** |
| Korrektur-Zielwert | nicht spezifiziert | **5,8–5,9 (explizit)** |
| Korrektur-Trigger oben | nicht spezifiziert | **6,3 (Veg) / 6,4 (Blüte)** |
| Dosierformel | fix / CalendarMgr | **dose_ml = ΔpH × tank_vol_L × K (dynamisch)** |
| Kalibrierkonstante K | fix / manuell | **Selbstkalibrierend via EMA (α=0,20), Startwert 0,07, NVS-persistent** |
| EMA-Gewicht α | — | **0,20 (konfigurierbar), steuert Anpassungsgeschwindigkeit** |
| Pumprate | — | **pump_rate_ml_per_s, empirisch kalibrieren** |
| Maximaldosis pro Impuls | kein Cap | **via SafetyGate MaxDuration (40 s ≈ 6 ml)** |
| SafetyGate MinInterval AcidPump | nicht spezifiziert | **900 s** |
| pH-Up-Pumpe | WARNING (kein Pump) | **Nicht nötig – Systemdesign pH-Down-only** |
| Feeding-Trigger | CalendarMgr (zeitbasiert) | **EC-getriggert (→ ausstehend: EC_Feeding_Logik.md)** |

---

*Letzte Aktualisierung: 2026-03-07 | Basis: FSMINFO.md v1.1 + Architecture.md + Systemlogik-Klärung*
