# HYDROMAT – Steuerungslogik (Control Logic Specification)

> Dieses Dokument definiert das generelle Steuerungskonzept für den Hydromat. Es beschreibt, welche Aktionen autonom, manuell oder hybrid ablaufen, wie Alerts strukturiert sind und wie das System 2+ Wochen autark laufen kann.
>
> **Zielgruppe:** Software-Architekt / Entwickler
> **Abhängigkeiten:** → Architecture.md, → pH_Regellogik.md, → EC_Feeding_Logik.md, → FSMINFO.md

---

## 1. Design-Philosophie

Das System folgt dem Prinzip **"autonom im Normalbetrieb, konservativ bei Unsicherheit"**. Konkret bedeutet das:

- Im Normalfall läuft alles ohne menschlichen Eingriff: pH-Korrektur, EC-Feeding, Wassernachfüllung, Lichtsteuerung.
- Bei Sensorausfällen, unplausiblen Werten oder Grenzwertüberschreitungen stoppt das System alle betroffenen Automatisierungen und alarmiert den Betreiber.
- Manuelle Eingriffe sind jederzeit über das ESPHome WebUI möglich und haben Vorrang vor Automatisierungen.
- Phasenwechsel im Grow-Zyklus sind **immer manuell** – das System schlägt vor, aber der Grower entscheidet.

---

## 2. Betriebsmodi

### 2.1 AUTO (Normalbetrieb)

Standardmodus. Alle Automatisierungen aktiv gemäß CalendarManager und Sensor-Triggern.

| Subsystem | Verhalten |
|-----------|-----------|
| pH-Regelung | Autonom nach → pH_Regellogik.md |
| EC-Feeding | Autonom nach → EC_Feeding_Logik.md |
| Wassernachfüllung | Autonom wenn Auto-Fill = ON |
| Licht | Nach CalendarManager (wenn ESP32-gesteuert) oder extern |
| Luftpumpe | Automatischer Zyklus (Tag/Nacht-abhängig) |
| Temperatur | Monitoring + Alerts |

### 2.2 NIGHT (Nachtmodus)

Aktiviert sich automatisch basierend auf `light_off_time` aus dem CalendarManager. **Alle Dosierungen werden pausiert.**

**Aktiv im Nachtmodus:**
- Sensor-Monitoring (pH, EC, Temperatur, Wasserstand) – nur lesend
- Alerts bei kritischen Grenzwerten (EMPTY-Sensor, Temperatur-Alarm)
- Luftpumpe im Nacht-Zyklus (reduzierte Frequenz)
- Logging

**Pausiert im Nachtmodus:**
- pH-Korrektur
- EC-Feeding / Nährstoff-Dosierung
- Wassernachfüllung (außer bei EMPTY → Notfall)
- Wasserentleerung

**Übergang:** Bei `light_on_time` wechselt das System zurück in AUTO. Sensor-Check wird durchgeführt bevor Dosierungen wieder freigegeben werden.

### 2.3 PAUSE (Manueller Eingriff)

Wird aktiviert durch:
- Manuellen Button im WebUI
- Automatisch wenn ein manueller Aktor-Override aktiv ist

Alle Automatisierungen gestoppt. Sensoren lesen weiter. Alerts bleiben aktiv. Rückkehr zu AUTO per Button.

### 2.4 ERROR

Wird aktiviert durch:
- Sensor-Ausfall nach Retry-Zyklus
- EMPTY-Wasserstand bei aktivem Auto-Fill (→ Sensorproblem vermutet)
- SafetyGate Violation (MaxDuration, unerwarteter Aktor-Zustand)
- Temperatur außerhalb kritischer Grenzen

**Verhalten:** Alle Aktoren stoppen. Alert wird gesendet. Manueller Reset erforderlich (per Button oder nach Fehlerbehebung automatisch, je nach Error-Typ – siehe Abschnitt 6).

### 2.5 VACATION (Urlaubsmodus)

Per Toggle aktivierbar. Modifiziert den AUTO-Modus wie folgt:

| Parameter | Normal | Urlaub |
|-----------|--------|--------|
| Alert-Schwellen | Standard-Fenster | Engere Toleranzen (±50% des normalen Bands) |
| Status-Report | Keiner | Täglicher Telegram-Report (Zusammenfassung) |
| Dosierung | Normal | Konservativer (z.B. 70% statt 80% Max-Dosis) |
| Retry-Versuche bei Sensorfehler | 3 | 5 (mehr Geduld bevor Error) |
| Auto-Fill | Normaler Trigger | Wie normal, aber zusätzlicher Alert bei jedem Fill-Event |

**Täglicher Status-Report enthält:**
- Aktuelle pH/EC/Temperatur-Werte
- Wasserverbrauch der letzten 24h (geschätzt aus Fill-Events)
- Anzahl pH-Korrekturen und Feedings
- Grow-Tag und aktuelle Phase
- Warnungen (falls vorhanden)

---

## 3. Aktor-Steuerungsmatrix

### 3.1 Übersicht: Autonom vs. Manuell

| Aktor | Automatisch | Manueller Trigger | Anmerkung |
|-------|-------------|-------------------|-----------|
| pH-Down Pumpe | ✅ Sensor-getriggert | ✅ "pH-Korrektur jetzt" Button | → pH_Regellogik.md |
| Dünger-Pumpen A/B/C | ✅ EC-getriggert | ✅ "Feeding jetzt" Button | → EC_Feeding_Logik.md |
| Magnetventil (Frischwasser) | ✅ LOW-Sensor (wenn Auto-Fill ON) | ✅ "Wasser nachfüllen" Button | Toggle: Auto-Fill ON/OFF |
| Tauchpumpe (Entleerung) | ❌ Nur manuell | ✅ "Tank entleeren" Button | Über Shelly HTTP-API |
| Licht | ✅ CalendarManager | ✅ "Licht an/aus" Button | Fallback: extern gesteuert |
| Luftpumpe | ✅ Automatischer Zyklus | ✅ "Luftpumpe an/aus" Button | Tag/Nacht-Zyklen |

### 3.2 Tauchpumpe (Shelly-Integration)

Die Tauchpumpe läuft über eine Shelly WLAN-Steckdose und ist **kein direkter ESP32-Aktor**. Integration:

```
ESP32 ──HTTP-API──▶ Shelly Plug ──220V──▶ Tauchpumpe
```

**Besonderheiten:**
- Kein SafetyGate-Schutz (liegt außerhalb der HAL). Stattdessen: Software-Timeout im Controller (MaxDuration z.B. 300s).
- WLAN-Abhängigkeit: Wenn Shelly nicht erreichbar → Alert, kein Retry-Loop.
- Entleerung ist **immer manuell getriggert** – kein automatischer Wasserwechsel.
- Während Entleerung: Alle anderen Aktoren gestoppt (WATER_EMPTYING State).
- EMPTY-Sensor beendet Entleerung (Shelly OFF, Magnetventil kann danach zum Neufüllen öffnen).

### 3.3 Lichtsteuerung

Das System unterstützt zwei Modi:

**Modus A – ESP32-gesteuert:** Licht hängt an einem Relais/MOSFET am ESP32. Zeiten kommen aus CalendarManager (`light_on_time`, `light_off_time`). ESP32 schaltet direkt.

**Modus B – Extern gesteuert:** Licht läuft über separate Zeitschaltuhr / Shelly / HA. ESP32 liest die Zeiten aus CalendarManager nur zur Berechnung des Nachtmodus-Fensters, steuert aber nichts.

**Konfiguration:** Boolean `light_controlled_by_esp` (default: true). Wenn false, wird der Licht-Aktor im HAL ignoriert, Nachtmodus-Berechnung läuft trotzdem.

---

## 4. Sensor-Logik & Fehlerbehandlung

### 4.1 Sensor-Übersicht

| Sensor | Typ | Leseintervall | Aktion bei Ausfall |
|--------|-----|---------------|-------------------|
| pH | Analog (ADC) | 30s | Stopp pH-Dosierung |
| EC/TDS | Analog (ADC) | 30s | Stopp EC-Feeding |
| Wasserstand HIGH | Digital (GPIO17) | 1s | Stopp Füllung |
| Wasserstand LOW | Digital (GPIO23) | 1s | Trigger Füllung |
| Wasserstand EMPTY | Digital (GPIO22) | 1s | NOTFALL → Error |
| Temperatur (Wasser) | Digital (DS18B20 o.ä.) | 60s | Nur Alert, kein Stopp |

### 4.2 Fehlerbehandlung: 3-Stufen-Modell

```
Sensor-Wert unplausibel oder fehlt
        │
        ▼
┌──────────────────────┐
│  STUFE 1: STOPP       │  Alle vom Sensor abhängigen
│  Sofort               │  Automatisierungen pausieren.
└──────────┬───────────┘  Andere Subsysteme laufen weiter.
           │
           ▼
┌──────────────────────┐
│  STUFE 2: RETRY       │  3× Retry (im Urlaub: 5×)
│  Intervall: 60s       │  mit je 60s Pause.
└──────────┬───────────┘
           │
     ┌─────┴─────┐
     │ Erfolg?   │
     ▼           ▼
   ✅ AUTO    ┌──────────────────────┐
   zurück     │  STUFE 3: ERROR       │  Alert senden.
              │  + ALERT              │  Manueller Reset nötig.
              └──────────────────────┘
```

**Plausibilitätsprüfungen:**
- pH: Wert muss zwischen 3.0 und 10.0 liegen. Änderung >1.0 pH/Minute = Sensorfehler.
- EC: Wert muss zwischen 0.0 und 5.0 mS/cm liegen. Sprung >1.0 mS/cm zwischen Messungen = verdächtig.
- Temperatur: Wert muss zwischen 5°C und 45°C liegen.
- Wasserstand: Logische Konsistenz (HIGH=true und EMPTY=true gleichzeitig = Fehler).

### 4.3 Temperatur-Alerts

| Bereich | Aktion |
|---------|--------|
| < 15°C | ⚠️ WARN: Stoffwechsel stark verlangsamt |
| 15–28°C | ✅ Normalbetrieb |
| > 28°C | ⚠️ WARN: Wurzelfäule-Risiko, Sauerstoff sinkt |
| > 32°C | 🔴 CRITICAL: Pflanzenschaden wahrscheinlich |

Temperatur löst **keine automatischen Gegenmaßnahmen** aus (kein Kühler vorhanden), nur Alerts.

---

## 5. Alert-System

### 5.1 Architektur

Alerts werden über eine abstrakte `AlertService`-Schicht gesendet, die verschiedene Backends unterstützt:

```
Controller/FSM
      │
      ▼
┌─────────────────┐
│  AlertService    │  ← Abstraktion
├─────────────────┤
│  TelegramBackend │  ← Phase 1 (aktiv)
│  HABackend       │  ← Phase 2 (geplant)
└─────────────────┘
```

**Phase 1:** Telegram Bot API direkt vom ESP32 (WiFiClientSecure → api.telegram.org).
**Phase 2:** Home Assistant Notification Service (über ESPHome native API oder HTTP).

### 5.2 Alert-Stufen

| Stufe | Name | Beispiele | Verhalten |
|-------|------|-----------|-----------|
| 🔴 | CRITICAL | EMPTY-Sensor bei aktivem Auto-Fill, Sensor-Ausfall nach Retry, Temperatur >32°C, SafetyGate Violation | Sofort senden. Im Urlaubsmodus: 3× wiederholen mit 15 Min Abstand bis Acknowledge. |
| ⚠️ | WARNING | pH/EC außerhalb Fenster >2h, Temperatur >28°C, Shelly nicht erreichbar, Kalibrierung überfällig | Sofort senden. Keine Wiederholung. |
| ℹ️ | INFO | Phasenwechsel empfohlen, Fill-Event abgeschlossen (nur Urlaub), Reboot stattgefunden | Im Normalbetrieb: nur loggen. Im Urlaubsmodus: in täglichen Report aufnehmen. |

### 5.3 Alert-Rate-Limiting

Damit das System nicht spammt:
- Gleicher Alert maximal 1× pro 30 Minuten (außer CRITICAL im Urlaubsmodus).
- Täglicher Report im Urlaubsmodus: 1× um 08:00 Uhr (konfigurierbar).
- Acknowledge-Mechanismus: CRITICAL-Alerts können per Telegram-Reply bestätigt werden (stoppt Wiederholung).

---

## 6. Fehler-Recovery & Reboot

### 6.1 Stromausfall / ESP32-Reboot

Das System verwendet NVS (Non-Volatile Storage) für State-Persistenz:

**Gespeichert in NVS:**
- Letzter FSM-State
- Grow-Tag (CalendarManager)
- Aktuelle Phase
- Adaptive K-Faktoren (pH und EC)
- Auto-Fill Toggle-Status
- Urlaubsmodus Toggle-Status
- Letzte Kalibrierungszeitpunkte
- Feeding- und pH-Korrektur-Counter

**Reboot-Sequenz:**
```
POWER ON
    │
    ▼
NVS laden → State wiederherstellen
    │
    ▼
Sensor-Check (alle Sensoren einmal lesen)
    │
    ├── Alle OK → Direkt in letzten Modus (AUTO/NIGHT/VACATION)
    │
    └── Sensor-Fehler → ERROR State, Alert senden
```

**Kein manueller Eingriff nötig** bei erfolgreichem Sensor-Check. Das System nimmt den Betrieb sofort wieder auf. Ein INFO-Alert "Reboot stattgefunden" wird gesendet.

**Sicherheit nach Reboot:** Dosier-Cooldowns werden auf Maximalwert gesetzt (als ob gerade dosiert wurde), um Double-Dosing nach Reboot zu verhindern. Die Cooldowns laufen dann normal ab.

### 6.2 Error-Recovery-Matrix

| Error-Typ | Auto-Recovery möglich? | Bedingung |
|-----------|----------------------|-----------|
| Sensor temporär unplausibel | ✅ Ja | Retry erfolgreich (3× / 5× im Urlaub) |
| Sensor dauerhaft ausgefallen | ❌ Nein | Manueller Reset nach Hardware-Fix |
| EMPTY bei aktivem Auto-Fill | ❌ Nein | Manueller Reset (Sensorproblem untersuchen) |
| Shelly nicht erreichbar | ⚠️ Teilweise | Betrifft nur Entleerung, Rest läuft weiter |
| SafetyGate Violation | ❌ Nein | Manueller Reset (Ursache untersuchen) |
| Temperatur-Alarm | ✅ Ja | Automatisch wenn Temperatur in Normalbereich zurückkehrt |
| WiFi-Ausfall | ✅ Ja | Alle lokalen Automatisierungen laufen weiter, nur Alerts/Shelly betroffen |

---

## 7. Grow-Phasen-Management

### 7.1 Prinzip: Immer manuell bestätigen

Der CalendarManager zählt die Tage, aber **Phasenwechsel erfordern immer manuelle Bestätigung**. Das gibt dem Grower volle Kontrolle darüber, wann z.B. die Blüte eingeleitet wird.

### 7.2 Ablauf

```
CalendarManager: Tag X erreicht → nächste Phase fällig
        │
        ▼
System: WARNING-Alert "Phasenwechsel empfohlen: Volle Veg → Stretch"
        │                + neue Zielwerte anzeigen
        ▼
Grower: Bestätigt per WebUI-Button "Phase wechseln"
        │
        ▼
System: Übernimmt neue Zielwerte (pH-Fenster, EC-Target, Dosis-Caps)
        │
        ▼
System: INFO-Alert "Phase gewechselt: Stretch (Tag X)"
```

**Wenn nicht bestätigt:** System bleibt in aktueller Phase mit aktuellen Parametern. Täglicher Reminder bis bestätigt oder übersprungen.

### 7.3 WebUI-Elemente

- **Aktuelle Phase:** Anzeige mit Grow-Tag
- **"Phase wechseln" Button:** Wechselt zur nächsten geplanten Phase
- **"Phase überspringen" Button:** Überspringt eine Phase (z.B. wenn Stretch übersprungen werden soll)
- **"Grow-Tag korrigieren" Input:** Manuelles Setzen des Grow-Tags (falls Zählung falsch)

---

## 8. Kalibrierung

### 8.1 Erinnerungen

Das System speichert den Zeitpunkt der letzten Kalibrierung in NVS und erinnert:

| Sensor | Intervall | Alert-Stufe |
|--------|-----------|-------------|
| pH-Sensor | Alle 14 Tage | ⚠️ WARNING |
| EC-Sensor | Alle 30 Tage | ⚠️ WARNING |

Erinnerung wird täglich wiederholt bis Kalibrierung durchgeführt.

### 8.2 Kalibrierungsmodus

Per WebUI-Button "Kalibrierung starten" erreichbar. Wechselt in State `PH_CALIBRATING` (bzw. EC_CALIBRATING):

- Alle Dosierungen gestoppt
- Sensor liefert Live-Werte im WebUI
- Grower führt 2-Punkt-Kalibrierung durch (pH 4.0 + pH 7.0 bzw. EC-Referenzlösungen)
- "Kalibrierung abschließen" Button → Zeitstempel in NVS speichern → zurück zu AUTO/IDLE

---

## 9. Manuelles Eingriffs-Interface (WebUI)

### 9.1 Buttons / Aktionen

| Button | Aktion | FSM-Transition |
|--------|--------|----------------|
| Auto-Fill ON/OFF | Toggle für automatische Wassernachfüllung | Kein State-Wechsel |
| Urlaubsmodus ON/OFF | Toggle für Urlaubsmodus | Kein State-Wechsel |
| Feeding jetzt | Sofort ein EC-Feeding auslösen | → FEEDING |
| pH-Korrektur jetzt | Sofort pH-Korrektur auslösen | → PH_CALCULATING |
| Wasser nachfüllen | Magnetventil manuell öffnen | → WATER_FILLING |
| Tank entleeren | Shelly-Tauchpumpe aktivieren | → WATER_EMPTYING |
| Flush starten | Entleeren → Neufüllen → optional: kein Feeding für X Stunden | → WATER_EMPTYING → WATER_FILLING |
| Phase wechseln | Nächste Grow-Phase aktivieren | Kein State-Wechsel |
| Kalibrierung starten | Kalibrierungsmodus für pH oder EC | → PH_CALIBRATING |
| System pausieren | Alle Automatisierungen stoppen | → PAUSE |
| System fortsetzen | PAUSE beenden | → AUTO / NIGHT |
| Error Reset | Error-State zurücksetzen nach Problembehebung | → IDLE → AUTO |
| Licht an/aus | Manueller Licht-Toggle (wenn ESP32-gesteuert) | Kein State-Wechsel |

### 9.2 Parameter-Overrides (Runtime)

Folgende Werte sollen über das WebUI **ohne Code-Änderung** anpassbar sein:

| Parameter | Typ | Beschreibung |
|-----------|-----|-------------|
| `ph_target_min` / `ph_target_max` | number | pH-Zielfenster temporär überschreiben |
| `ec_target` / `ec_tolerance` | number | EC-Zielwert temporär überschreiben |
| `dose_multiplier` | number (0.0–1.0) | Globaler Dosierungs-Multiplikator (z.B. 0.5 = halbe Dosis) |
| `auto_fill_enabled` | boolean | Auto-Fill Toggle |
| `vacation_mode` | boolean | Urlaubsmodus Toggle |
| `light_controlled_by_esp` | boolean | Lichtsteuerung durch ESP32 |
| `night_dosing_allowed` | boolean | Dosierung auch nachts erlauben (default: false) |

Diese Werte werden in NVS persistiert und überleben Reboots.

---

## 10. Sequenzierung & Prioritäten

### 10.1 Aktions-Prioritäten

Wenn mehrere Aktionen gleichzeitig fällig sind, gilt diese Reihenfolge:

```
1. NOTFALL (EMPTY, SafetyGate Violation)     ← höchste Priorität
2. WATER_FILLING (bei LOW-Sensor)
3. EC-Feeding (bei EC unter Fenster)
4. pH-Korrektur (bei pH über Trigger)
5. Kalibrierungs-Erinnerung
6. Phasenwechsel-Erinnerung                  ← niedrigste Priorität
```

### 10.2 Sequenzierung nach Wassernachfüllung

Wassernachfüllung verändert pH und EC signifikant. Deshalb:

```
WATER_FILLING abgeschlossen
        │
        ▼
Stabilisierungsphase (10 Min warten, → pH_Regellogik.md)
        │
        ▼
Sensor-Neuerfassung (pH + EC messen)
        │
        ▼
EC-Feeding falls nötig (EC durch Verdünnung gesunken?)
        │
        ▼
Stabilisierungsphase (5 Min)
        │
        ▼
pH-Korrektur falls nötig (Feeding verändert pH)
```

**Immer: EC vor pH.** Nährstoffe verändern den pH, daher wäre eine vorherige pH-Korrektur verschwendet.

---

## 11. 2-Wochen-Autonomie: Engpassanalyse

### 11.1 Limitierende Faktoren

| Faktor | Kapazität | Reicht für ~2 Wochen? |
|--------|-----------|----------------------|
| Frischwasser | Unbegrenzt (Leitung) | ✅ Ja |
| pH-Down (10% HNO₃) | Flaschengröße (typisch 500ml–1L) | ⚠️ Abhängig von Verbrauch. ~2-5ml/Tag bei 70L Tank → 500ml reicht ~100–250 Tage |
| Dünger A/B/C | Flaschengröße (typisch 1L je) | ⚠️ Abhängig von Phase. Blüte verbraucht am meisten. Grob: 5-15ml/Tag je Komponente → 1L reicht ~65–200 Tage |
| Strom | Netzstrom | ✅ Ja (außer Stromausfall) |
| WiFi | Router | ✅ Ja (für Alerts/Shelly; lokale Automatisierung läuft auch ohne) |

### 11.2 Kritische Szenarien

| Szenario | Verhalten |
|----------|-----------|
| Stromausfall | NVS-Recovery → sofort weiter. Alert per Telegram sobald WiFi wieder da. |
| WiFi-Ausfall | Alle lokalen Automatisierungen (pH, EC, Wasser, Licht) laufen weiter. Nur Telegram-Alerts und Shelly-Steuerung betroffen. |
| pH-Sensor driftet | Adaptive K fängt langsame Drift ab. Kalibrierungs-Erinnerung beachten! |
| EC-Sensor driftet | Adaptive EC_K_feed kompensiert. Pflanzen zeigen bei starkem Drift visuell Symptome. |
| Vorratsflasche leer | Pumpe läuft, aber nichts kommt. EC/pH reagiert nicht wie erwartet → "erwartete Wirkung ausgeblieben" nach 2 Zyklen → WARNING Alert. |

### 11.3 Empfehlung vor Urlaub

- Frische Kalibrierung aller Sensoren
- Alle Vorratsflaschen auffüllen
- Einen kompletten Wasserwechsel durchführen
- Urlaubsmodus aktivieren
- Testlauf: 2–3 Tage beobachten ob System stabil läuft

---

## 12. Zusammenfassung: Entscheidungsmatrix

| Funktion | Autonomie | Manuell möglich | Alert |
|----------|-----------|----------------|-------|
| pH-Korrektur | ✅ Sensor-getriggert | ✅ Button | ⚠️ bei Fenster-Verletzung >2h |
| EC-Feeding | ✅ Sensor-getriggert | ✅ Button | ⚠️ bei Fenster-Verletzung >2h |
| Wassernachfüllung | ✅ Toggle (Auto-Fill) | ✅ Button | ℹ️ bei Fill-Event (Urlaub) |
| Wasserentleerung | ❌ Nur manuell | ✅ Button | – |
| Flush | ❌ Nur manuell | ✅ Button | – |
| Licht | ✅ CalendarManager | ✅ Button | – |
| Luftpumpe | ✅ Auto-Zyklus | ✅ Button | – |
| Phasenwechsel | ❌ Nur manuell | ✅ Button | ⚠️ Empfehlung wenn fällig |
| Kalibrierung | ❌ Nur manuell | ✅ Button | ⚠️ Erinnerung nach Intervall |
| Urlaubsmodus | ❌ Nur manuell | ✅ Toggle | – |
| Sensor-Fehler | Auto-Stopp + Retry | ✅ Reset-Button | 🔴 nach Retry-Fail |
| Temperatur | Nur Monitoring | – | ⚠️/🔴 bei Grenzwert |
| Reboot-Recovery | ✅ Automatisch | – | ℹ️ "Reboot stattgefunden" |

---

*Erstellt: 7. März 2026*
*Abhängigkeiten: Architecture.md, pH_Regellogik.md, EC_Feeding_Logik.md, FSMINFO.md*
