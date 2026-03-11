# Hydromat – EC-Feeding-Logik (Architect Reference)

> Tankvolumen: **70 L** | Nährstoffe: A / B / C via Peristaltikpumpen | Referenz: `plantOS.yaml` CalendarManager

---

## 1. Was der CalendarManager leistet – und was nicht

Der CalendarManager definiert pro Grow-Tag:
- `dose_A_ml_per_L`, `dose_B_ml_per_L`, `dose_C_ml_per_L` → **Mischverhältnis** und **Maximalmenge pro Feeding-Event** in mL pro Liter Tankvolumen. Das sind keine Tagesmengen – ein Feeding-Event findet nicht täglich, sondern EC-getriggert statt (selten, sporadisch, abhängig vom tatsächlichen Verbrauch).
- `ph_min`, `ph_max` → pH-Fenster (wird bereits verwendet)
- Aktuell **keine EC-Zielwerte** im Schema → muss erweitert werden (→ Abschnitt 9)

Die CalendarManager-Werte definieren das phasenkorrekte **A:B:C-Verhältnis** für den aktuellen Grow-Tag sowie die **absolute Obergrenze** für eine einzelne Dosierung. Sie sagen nichts darüber aus, wann oder wie oft gedüngt wird – das übernimmt die EC-Logik. Die Werte entsprechen bereits ~80 % der Hersteller-Empfehlung; selbst das führte in früheren Runs zu Nährstoffbrand. Sie sind bewusst konservativ gesetzt und dürfen vom System nie überschritten werden.

---

## 2. EC-Drift-Mechanismen im 70-L-System

Im Hydromat wirken drei gegenläufige Kräfte auf den EC:

| Mechanismus | EC-Effekt | Auslöser |
|---|---|---|
| **Pflanzlicher Wasserverbrauch** | ↑ steigt (Nährstoffe konzentrieren sich) | Kontinuierlich |
| **Verdunstung** | ↑ steigt (nur Wasser geht, Salze bleiben) | Kontinuierlich, temperaturabhängig |
| **Nährstoffaufnahme durch Pflanze** | ↓ sinkt leicht (Nährstoffe werden konsumiert) | Kontinuierlich, wachstumsabhängig |
| **Frischwasser-Nachfüllung (Magnetventil)** | ↓ sinkt stark und sprunghaft | Wasserstandssensor LOW |

In der Praxis dominieren Verbrauch + Verdunstung → **EC steigt zwischen Nachfüllungen kontinuierlich**. Bei einer Nachfüllung von 20–30 L in einem 70-L-Tank fällt der EC entsprechend stark:

```
EC_nach_fill = EC_vorher × (Restvolumen / Gesamtvolumen)

Beispiel: EC=1,8 vor Füllstand 40L, Nachfüllung auf 70L:
EC_nach = 1,8 × (40 / 70) = 1,03 mS/cm  → Verlust ~0,77 mS/cm
```

Das ist der Hauptauslöser für Düngerbedarf: **nicht die Zeit, sondern die Nachfüllung**.

---

## 3. EC-Fenster-Logik

### Drei Schwellwerte

```
EC_max_alarm  (z.B. +0,4 über Ziel) → Alert, manuell eingreifen oder Wasser verdünnen
EC_ziel                              → CalendarManager-Ziel für aktuellen Grow-Tag
EC_min_trigger (z.B. -0,2 unter Ziel) → Düngung auslösen
```

Der Abstand zwischen `EC_min_trigger` und `EC_ziel` definiert die **Hysterese** – er verhindert dauerhaftes Nachdüngen bei kleinen Schwankungen.

### Woher kommt EC_ziel?

Zwei Optionen – Option B ist empfohlen:

**Option A (CalendarManager-Erweiterung):** `ec_target` direkt im Schedule-JSON pro Tag definieren. Explizit, transparent, aber aufwändig für 120 Tage.

**Option B (Ableitung aus Dosiermenge + EC_K_feed):** Der CalendarManager definiert wie viel Nährstoff an einem Tag vorgesehen ist. Kombiniert mit dem adaptiven `EC_K_feed` (EC-Anstieg pro mL/L Gesamtdosis) ergibt sich der Ziel-EC implizit. Einfacher zu pflegen, aber `EC_K_feed` muss erst kalibriert sein.

**Empfehlung:** Option A für Produktion – `ec_target_min` und `ec_target_max` ins CalendarManager-Schema aufnehmen (→ Abschnitt 9). Das macht den Sollwert pro Grow-Phase explizit und ist unabhängig von `EC_K_feed`-Kalibrierfehlern.

---

## 4. Trigger-Hierarchie: Wann wird gedüngt

| Priorität | Trigger | Bedingung | Reaktion |
|---|---|---|---|
| **1** | Nach `WATER_FILLING` abgeschlossen | EC < `ec_target_min` | EC-Check → Dosierung |
| **2** | Nach `FEED_FILLING` abgeschlossen | EC < `ec_target_min` | EC-Check → Dosierung mit CalendarMgr-Verhältnis |
| **3** | Periodischer EC-Check (2h, analog pH) | EC < `ec_target_min` | Dosierung |
| **–** | Jeder Trigger | EC > `ec_target_max` | **Kein Düngen** – Verdunstungskonzentration, warten auf nächste Nachfüllung |

**Wichtig:** Bei EC > `ec_target_max` wird **nicht** gedüngt, auch wenn der Trigger feuert. Das System wartet darauf, dass eine Nachfüllung den EC verdünnt. Nur Wasser löst das Problem – nicht mehr Nährstoffe.

---

## 5. Dosiermenge: Verhältnis halten, EC-Ziel anpeilen

### Grundprinzip

Die CalendarManager-Werte definieren das **A:B:C-Verhältnis** für den aktuellen Grow-Tag. Dieses Verhältnis ist biologically korrekt und muss eingehalten werden – unabhängig davon, wie viel wir davon brauchen.

```
Verhältnis-Faktoren (aus CalendarManager):
r_A = dose_A_ml_per_L
r_B = dose_B_ml_per_L
r_C = dose_C_ml_per_L
r_total = r_A + r_B + r_C

EC-Delta, das geschlossen werden soll:
delta_EC = ec_target - ec_aktuell

Gesamtdosis (mL/L), um delta_EC zu erreichen:
total_ml_per_L = delta_EC / EC_K_feed

Tatsächliche Dosen:
dose_A_ml = (r_A / r_total) × total_ml_per_L × tank_volume_L
dose_B_ml = (r_B / r_total) × total_ml_per_L × tank_volume_L
dose_C_ml = (r_C / r_total) × total_ml_per_L × tank_volume_L
```

### Maximaldosis-Cap pro Feeding-Event: CalendarManager-Werte als absolute Obergrenze

Die `dose_X_ml_per_L`-Werte im CalendarManager sind **Maximalwerte pro Feeding-Event**, keine Zieldosen und keine Tagesmengen. Ein Feeding-Event findet nicht täglich statt – es wird EC-getriggert, kann also mehrere Tage ausbleiben oder mehrfach pro Woche auftreten, je nach Wasserverbrauch und Pflanzenaktivität. Die Werte wurden bereits auf ~80 % der Hersteller-Empfehlung gesetzt – und selbst das hat in früheren Runs zu Nährstoffbrand (knusprige Blätter, Wachstumsstopp) geführt. Das EC-basierte Dosieren arbeitet immer darunter; der CalendarManager-Wert ist der Hard-Cap, den das System unter keinen Umständen überschreitet.

```
dose_A_ml = min(dose_A_ml_ec_berechnet, dose_A_ml_per_L × tank_volume_L)
dose_B_ml = min(dose_B_ml_ec_berechnet, dose_B_ml_per_L × tank_volume_L)
dose_C_ml = min(dose_C_ml_ec_berechnet, dose_C_ml_per_L × tank_volume_L)
```

Ist der EC-Delta so groß, dass selbst der volle Cap nicht reicht → Cap dosieren, EC messen, dann ggf. weiteres Füttern am nächsten Trigger-Event (nach Mindestintervall). **Niemals mehr als den Cap auf einmal, egal wie weit der EC vom Ziel entfernt ist.**

### Implementierungshinweis

```cpp
// --- EC-Dosisberechnung ---
float delta_ec = ec_target - ec_aktuell;
float total_ml_per_L = delta_ec / EC_K_feed;

float dose_A = (r_A / r_total) * total_ml_per_L * tank_volume_L;
float dose_B = (r_B / r_total) * total_ml_per_L * tank_volume_L;
float dose_C = (r_C / r_total) * total_ml_per_L * tank_volume_L;

// Maximaldosis-Cap pro Feeding-Event anwenden (CalendarMgr-Werte = absolute Obergrenze)
dose_A = min(dose_A, calendar->getDoseA_mLperL() * tank_volume_L);
dose_B = min(dose_B, calendar->getDoseB_mLperL() * tank_volume_L);
dose_C = min(dose_C, calendar->getDoseC_mLperL() * tank_volume_L);
```

---

## 6. Adaptives EC_K_feed: Selbstkalibrierung

Analog zu `K` in der pH-Logik: `EC_K_feed` ist die empirische Konstante für EC-Anstieg pro mL/L Gesamtdosis, und sie lernt sich nach jedem Dünger-Zyklus selbst.

```
EC_K_feed_beobachtet = ml_gesamt_per_L / (EC_nach – EC_vor)
EC_K_feed_neu = α × EC_K_feed_beobachtet + (1 – α) × EC_K_feed_alt
```

Mit `α = 0,20` (konfigurierbar, gleiche EMA-Logik wie pH-K).

| Nährstoffkonzentrat | EC_K_feed Startwert (mS/cm pro mL/L) |
|---|---|
| Schwächeres 2-Part Konzentrat | 0,08 – 0,12 |
| Mittleres 3-Part Konzentrat (typisch) | 0,12 – 0,18 |
| Starkes Konzentrat / Einzelsalze | 0,18 – 0,25 |

**Empfohlener Startwert: EC_K_feed = 0,15** – nach 3–5 Zyklen übernimmt die adaptive Anpassung.

Guard-Bedingungen für K_feed-Update (analog pH-K):
- `EC_delta_beobachtet > 0,05 mS/cm`
- Kein Wasser-Nachfüll-Event während des Dünger-Zyklus
- Zyklus nicht durch Timeout oder ERROR abgebrochen
- `EC_K_feed_beobachtet` liegt in `[0,02 ; 0,50]`

---

## 7. Hysterese & Mindestintervall

Ohne Mindestintervall würde das System bei EC-Messrauschen (±0,05–0,1 mS/cm typisch für Hydroponik-Sonden) ständig kurze Dosierungen auslösen.

| Parameter | Empfehlung | Begründung |
|---|---|---|
| **Mindestintervall zwischen Feeding-Events** | 4 h | Verhindert Micro-Dosing aus Messrauschen |
| **EC_min_trigger-Hysterese** | EC_ziel – 0,2 mS/cm | Fenster, in dem nichts passiert |
| **Tageslock (NVS)** | Optional – eher nicht für EC-basiert | EC-basiertes Triggern macht Tageslock überflüssig; der physikalische Zustand ist der Lock |

**Zum Tageslock:** Der bestehende NVS-Tageslock (`AUTOFEED_<timestamp>`) ist für zeitbasiertes Feeding sinnvoll, aber für EC-basiertes kontraproduktiv. Wenn abends 25 L Wasser nachgefüllt werden und morgens EC niedrig ist, muss das System düngen können – unabhängig davon, ob heute schon gedüngt wurde. **Das Mindestintervall von 4 h ersetzt den Tageslock als Sicherheitsmechanismus.**

---

## 8. Sequenzierung mit pH-Korrektur

Reihenfolge ist kritisch: Nährstoffe verändern den pH. Deshalb:

```
WATER_FILLING abgeschlossen
      │
      ▼
EC messen
      │
      ├─ EC in Fenster → weiter zu pH-Check → IDLE
      │
      └─ EC < ec_target_min → FEEDING (A → B → C sequenziell)
              │
              ▼
         EC-Messung nach Mischen (5 min Wartezeit)
              │
              ├─ EC erreicht? → K_feed-Update → weiter zu pH-Check
              │
              └─ EC noch niedrig + Mindestintervall ok → nächste Runde (max 3×)
              │
              ▼
         PH_PROCESSING (pH korrigieren nach Düngung)
              │
              ▼
            IDLE
```

**Niemals** pH vor EC korrigieren, wenn Düngung geplant ist – Nährstoffe verschieben den pH, was eine sofortige zweite pH-Korrektur erzwingen würde.

---

## 9. Erforderliche CalendarManager-Erweiterung

Das aktuelle YAML-Schema muss um EC-Zielwerte erweitert werden:

```yaml
# Erweitertes Schema (Ergänzung der bestehenden Felder)
{"day": 1,
 "ph_min": 5.8, "ph_max": 6.0,
 "ec_target": 0.8, "ec_tolerance": 0.2,
 "dose_A_ml_per_L": 0.8, "dose_B_ml_per_L": 0.8, "dose_C_ml_per_L": 0.8,
 "light_on_time": 960, "light_off_time": 480}
```

| Neues Feld | Bedeutung |
|---|---|
| `ec_target` | Ziel-EC in mS/cm für diesen Grow-Tag |
| `ec_tolerance` | Hysterese-Breite; `ec_min = ec_target - ec_tolerance`, `ec_max = ec_target + (ec_tolerance × 1,5)` |

Alternativ explizit:

```yaml
{"ec_min": 0.6, "ec_max": 1.1, "ec_target": 0.8}
```

Die Werte folgen der Hydroponik-Zielwerttabelle (→ `Hydroponik_Zielwerttabelle.xlsx`, Sheet "Hydroponik Zielwerte").

---

## 10. Guard-Bedingungen für FEEDING

| Bedingung | Begründung |
|---|---|
| EC < `ec_target_min` | Primäres Kriterium |
| EC ≤ `ec_target_max` | Kein Düngen bei Überkonzentration durch Verdunstung |
| Wasserstand **nicht** EMPTY | Pumpen nicht in trockenen Tank dosieren |
| Mindestintervall seit letztem Feeding abgelaufen | Verhindert Micro-Dosing |
| System nicht in NIGHT / SHUTDOWN / PAUSE | Bestehende Logik beibehalten |
| Kein laufendes pH-Korrektur-Event | Sequenzierung einhalten |

---

## 11. Sequenz-Übersicht (vollständig)

```
WATER_FILLING abgeschlossen  ──► auto_ec_feed_check_pending = true
IDLE (2h-EC-Timer)           ──► EC_PROCESSING

EC_PROCESSING: EC messen
  │
  ├─[EC > ec_target_max] ──► IDLE (warten auf Verdünnung durch nächste Nachfüllung)
  │
  ├─[ec_target_min ≤ EC ≤ ec_target_max] ──► IDLE (im Fenster, nichts tun)
  │
  └─[EC < ec_target_min AND Guards ok]
          │
          ▼
    EC_CALCULATING
    delta_EC = ec_target - ec_aktuell
    total_ml_per_L = delta_EC / EC_K_feed
    dose_A/B/C = Verhältnis × total × tank_vol  (mit CalendarMgr-Cap)
          │
    ┌─────┴─────────────────┐
    │ delta zu klein / Guards│ Dosierung berechnet
    │ nicht erfüllt          │
    ▼                        ▼
  IDLE                EC_FEEDING
                      Pumpe A → Pause → Pumpe B → Pause → Pumpe C
                      (sequenziell, mit Mischpausen)
                            │
                            ▼
                      EC_MIXING (5 min Luftpumpe)
                            │
                            ▼
                      EC_MEASURING (Ergebnis messen)
                            │
                      ┌─────┴────────────────┐
                      │ EC erreicht / max     │ EC noch niedrig
                      │ Versuche (3×) erreicht│ + Mindestintervall ok
                      ▼                       ▼
                 EC_K_feed-Update         [EC_CALCULATING]
                 saveToNVS                (Loop, max 3×)
                      │
                      ▼
               PH_PROCESSING  ← immer nach Düngung!
                      │
                      ▼
                    IDLE
```

---

## 12. Delta zur aktuellen Implementierung

| Aspekt | Aktuell (CalendarMgr) | Hydromat EC-Logik |
|---|---|---|
| Feeding-Trigger | Wasserstand LOW (1×/Tag) | EC < ec_target_min (ereignisbasiert) |
| Dosiermenge | Feste Dosis pro Event (mL/L × vol) | EC-delta-basiert, Hard-Cap aus CalendarMgr |
| Datumslock | NVS-basiert, 1×/Tag | Mindestintervall 4 h ersetzt Datumslock |
| EC-Überwachung | Nicht implementiert | 2h-Periodikcheck + post-fill-Trigger |
| Verdunstungsschutz | Nicht implementiert | Kein Düngen wenn EC > ec_target_max |
| CalendarMgr-Dosisbedeutung | Feste Dosis pro Event | **Absolute Maximalwerte pro Feeding-Event (≈80 % Herstellerempfehlung, führte zu Nute Burn)** |
| CalendarMgr-Schema | ph_min/max + doses | **+ec_target, +ec_tolerance** (neue Felder) |
| EC_K_feed | Nicht vorhanden | Adaptiv per EMA, NVS-persistent |
| Sequenz mit pH | pH nach Feeding (bereits impl.) | Explizit: EC → Mischen → pH → IDLE |

---

*Erstellt: 2026-03-07 | Referenz: FSMINFO.md v1.1, plantOS.yaml CalendarManager, pH_Regellogik.md*
