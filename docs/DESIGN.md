# SAMS AMBIENCE CONTROL — projekt DSP (DESIGN)

Dokument opisuje tor sygnału, zastosowane modele, parametry, założenia i wyniki weryfikacji wtyczki **SAMS AMBIENCE CONTROL** (do wersji 1.0.0 pod nazwą roboczą „Ambience”) (specyfikacja: [`PROMPT.md`](../PROMPT.md)). Wartości oznaczone **[kalibracja]** nie pochodzą bezpośrednio z literatury — dobrano je odsłuchowo/inżyniersko w zakresie podawanym przez źródła i należy je weryfikować odsłuchem.

## 1. Architektura

```
Source/dsp/        czysty C++ (bez JUCE) — silnik, modele, testowalny i renderowalny offline
  DspCore.h          biquady RBJ, sekcje 1. rzędu, SVF (TPT), linia opóźniająca Lagrange 3. rzędu,
                     all-passy, siatka tercjowa, dopasowywanie charakterystyk (Levenberg–Marquardt)
  AcousticModels.h   ISO 9613-1, Maekawa, prawo masy / Sharp, materiały, model głowy Browna–Dudy,
                     odległość krytyczna, ważenie K (BS.1770)
  RoomModel.h        wczesne odbicia (źródła pozorne) + FDN 8 linii
  Params.h           tabela parametrów (stałe ID), EngineParams, presety fabryczne
  SpatialEngine.*    kompletny tor przetwarzania
Source/            wtyczka JUCE: procesor, stan, presety, UI (Source/ui)
tests/             testy akceptacyjne (PROMPT.md, sekcja 11)
tools/             renderer offline (CLI), sygnały testowe, wykresy charakterystyk
```

Silnik działa próbka po próbce, a wartości sterujące liczy w **tyku kontrolnym co 32 próbki**, wyrównanym do bezwzględnego licznika próbek. Dzięki temu wynik nie zależy od rozmiaru bufora hosta — test daje identyczny render przy buforach 32, 97 i 1024. Parametry są wygładzane filtrem jednobiegunowym (30 ms; Auto Gain 50 ms). Wzmocnienia i opóźnienia są interpolowane liniowo wewnątrz tyku. Latencja wynosi **0 próbek**.

## 2. Tor sygnału

```
wejście (1 lub 2 kanały)
  └─ tryb Input → M = (L+R)/2, S = (L−R)/2 · ψ         (ψ: 0 = źródło mono, 1 = źródło stereo; płynnie)
  └─ kompensacja efektu zbliżeniowego (M, S)
  └─ [opóźnienie propagacji r/c — Doppler]              (domyślnie OFF, crossfade przy przełączaniu)
  ├─ ŚCIEŻKA BEZPOŚREDNIA
  │    poziom wg odległości → pochłanianie powietrza → Obstruction
  │    → Width (S stereo · w, S dekorelowane z M dla źródeł mono, Bass Mono na S)
  │    → Panner: Speakers (amplituda + czas, Mono-safe) | Headphones (model głowy), crossfade
  └─ ŚCIEŻKA POMIESZCZENIA (sygnał sprzed tłumienia odległościowego — pole pogłosowe ma stały poziom)
       wczesne odbicia (6 źródeł pozornych / odbicie od gruntu) + FDN, poziom wg DRR
  → suma → [wyjście mono: (L+R)·norm] → Occlusion (dźwięk bezpośredni + pogłos pomieszczenia źródła)
  → Auto Gain → Output
```

## 3. Modele

### 3.1 Odległość
- **Poziom:** g = (r_rec / r)^(n/6,02), gdzie n = `Rolloff` [dB na podwojenie odległości]. Dla n = 6 jest to prawo odwrotnych kwadratów. Wzmocnienie przy przybliżaniu jest ograniczone do +24 dB.
- **Pochłanianie w powietrzu** wg ISO 9613-1 (wzory 3–5, zał. B). Tłumienie α(f)·max(0, r − r_rec) realizuje kaskada 5 półek wysokich + wzmocnienie szerokopasmowe. Wzmocnienia dopasowuje deterministyczny solwer Levenberga–Marquardta na siatce tercjowej 25 Hz–16 kHz, a częstotliwości półek są rozmieszczane względem punktu, w którym tłumienie osiąga 24 dB. Błąd wynosi ≤ 1 dB wszędzie tam, gdzie tłumienie < 24 dB (test dla 10–1000 m, 44,1–192 kHz). Rysunek: `img/air_absorption.png`.
- **DRR i pogłos:** poziom pola rozproszonego jest ustawiony tak, by zrównał się z dźwiękiem bezpośrednim w odległości krytycznej r_c ≈ 0,057·√(Q·V/T60) (Kuttruff; Q = 2). Energia ogona FDN jest normalizowana na podstawie odpowiedzi impulsowej zmierzonej w `prepare()`.
- **Wczesne odbicia:** metoda źródeł pozornych 1. rzędu (Allen i Berkley 1979) w prostopadłościanie. Każde odbicie ma własną drogę, poziom (wsp. odbicia √(1−ᾱ) z Sabine'a), kierunek i opóźnienie względem dźwięku bezpośredniego — stąd ITDG maleje wraz z odległością. Gdy źródło wychodzi poza pomieszczenie, jego pozycja jest przycinana do ścian (margines 0,3 m). W trybie Outdoor jest tylko odbicie od gruntu (wysokości 1,6 m, współczynnik 0,55 **[kalibracja]**).
- **Przestrzenie [kalibracja]** (16; A = neutralny, B = wytłumiony, C = żywy):

  | Typ | Wymiary [m] | T60 [s] | T60 HF/mid | Uwagi |
  |---|---|---|---|---|
  | Open A (field) | — | — | — | tylko odbicie od gruntu |
  | Open B (street) | 14 × 60, bez sufitu | 1,1 | 0,4 | fasady + grunt, ogon ×0,3 |
  | Open C (courtyard) | 20 × 24, bez sufitu | 0,9 | 0,5 | ściany + grunt, ogon ×0,4 |
  | Small A / B / C | 4×3×2,5 / 3,5×3×2,4 / 3×2,5×2,4 | 0,4 / 0,25 / 0,9 | 0,55 / 0,45 / 0,8 | C = łazienka z kafelkami |
  | Medium A / B / C | 7×6,5×3,3 / 6×5×2,7 / 9×7×3,2 | 0,6 / 0,45 / 1,0 | 0,5 / 0,45 / 0,7 | B = umeblowany, C = pusta sala |
  | Large A / B / C | 12×12×4,2 / 15×12×6 / 30×20×8 | 0,9 / 0,7 / 2,2 | 0,5 / 0,45 / 0,6 | B = studio, C = magazyn |
  | Hall A / B / C | 25×20×10 / 30×22×12 / 50×20×20 | 1,8 / 1,3 / 4,0 | 0,45 / 0,5 / 0,55 | B = teatr, C = kościół |

  Stan w wersji 1 miał 6 przestrzeni (Off, Outdoor, Small, Medium, Large, Hall). Przy wczytywaniu projektu lub presetu z v1 indeks jest mapowany na warianty A (`migrateRoomIndexV1`, `stateVersion` = 2).
- **FDN:** 8 linii, macierz Householdera, jednobiegunowe tłumienie w pętli (Jot i Chaigne 1991), dekorelowane wyjścia L/P, brak modulacji (determinizm).
- **Efekt zbliżeniowy:** stosunek odpowiedzi mikrofonu gradientowego 1. rzędu w nowej i nagranej odległości, H(s) = (s + B·c/r)/(s + B·c/r_rec), gdzie B to udział składowej gradientowej (omni 0, kardioida 0,5, ósemka 1). Jest to półka ograniczona — przy oddalaniu usuwa podbicie basu z bliskiego nagrania.
- **Opóźnienie propagacji / Doppler:** linia opóźniająca z interpolacją Lagrange'a 3. rzędu, maks. 1000 m / c ≈ 2,9 s. Nie jest zgłaszane hostowi jako latencja (to efekt, nie kompensacja).

### 3.2 Panorama
- **Speakers:** panner hybrydowy. Pozycja amplitudowa to pₐ = p·(1 − t·(1 − |p|)), a różnica czasu τ = t·p·τ_max, gdzie t = `Time Pan`. Na skrajach panoramy źródło jest zawsze w jednym głośniku. τ_max = 1 ms przy pełnym przesunięciu czasowym (Blauert 1997). Przy **Mono-safe** τ_max = 0,3 ms, a opóźniana jest tylko składowa < 1,2 kHz (rozdział komplementarny M − LP + LP(t−τ), więc przy τ = 0 sygnał jest bitowo nietknięty). Opóźnienie działa wyłącznie na M; składowa S trafia do L/P antysymetrycznie z wagą k = min(G_L, G_P), dlatego **suma mono nie zależy od Width** przy żadnym ustawieniu.
- **Prawa panoramy:** 0 dB (balans), −3 dB (sin/cos), −4,5 dB (średnia geometryczna), −6 dB (liniowe). Dla źródeł stereo wzmocnienia są normalizowane tak, by w centrum wynosiły 1 (przezroczystość).
- **Headphones:** model sferycznej głowy Browna i Dudy (1998): a = 8,75 cm, opóźnienia per ucho wg ich wzoru (3) (ITD Woodwortha), filtr cienia głowy 1-biegun/1-zero z α(θ) (α_min = 0,1, θ_min = 150°). Filtr jest znormalizowany do α(90°), więc źródło z przodu brzmi płasko. Opóźnienie fazowe filtra (przy 750 Hz) jest odejmowane, żeby całkowite ITD w paśmie < 1,5 kHz odpowiadało wzorowi Woodwortha (test: 0,656 ± 0,02 ms). Źródło stereo to dwa wirtualne źródła rozstawione o ±30°·Width. Poniżej 1 m płynnie włącza się paralaksa akustyczna (1/r osobno dla każdego ucha). Rysunek: `img/headphones.png`.

### 3.2a Mono Depth
Tryb ściśle monofoniczny. Sygnał wyjściowy to M ścieżki bezpośredniej (odległość, powietrze, efekt zbliżeniowy, obstruction) plus **dedykowane wyjście mono pomieszczenia**: wczesne odbicia z wagą √(g_L² + g_P²) (bez panoramy) oraz ogon FDN z wszystkich 8 linii (wzór znaków {+,+,+,+,−,−,−,−}·0,5). Energia tego wyjścia jest równa energii L+P wersji stereo (test: ±1,5 dB), więc przełączanie nie zmienia bilansu pogłosu. Składanie stereo pogłosu do mono dałoby tylko 4 linie i ubytki gęstości. Przy wyjściu stereo L = P = mono·g_c (g_c = prawo panoramy w centrum), co jest bitowo równe źródłu w centrum. Na ścieżce mono (1→1) używana jest ta sama ścieżka. Przełączanie crossfade'em 30 ms.

### 3.2b Equalized Ambience
Główny przełącznik automatycznego EQ zależnego od odległości i położenia (domyślnie ON). Obejmuje: pochłanianie powietrza, kompensację efektu zbliżeniowego oraz *position EQ* — półkę wysoką 5 kHz na kanale oddalonym od źródła (tryb Speakers), −2,5 dB·|pan| **[kalibracja]**, działającą tylko na M (Width pozostaje zgodne z mono). Po wyłączeniu odpowiedź częstotliwościowa jest płaska (test ±0,3 dB), a poziom w paśmie środkowym się nie zmienia (±0,5 dB).

### 3.3 Szerokość
Dla źródeł stereo S jest mnożone przez Width. Źródła mono poszerza dekorelator (4 all-passy Schroedera, 3,1 / 4,7 / 6,3 / 11,3 ms, g = 0,6; zasada Kendalla 1995) — powstały sygnał S jest odejmowany w sumie mono. Głębokość przy 200 % wynosi 0,8 **[kalibracja]**. Bass Mono to filtr górnoprzepustowy LR4 (2 × SVF Butterwortha) na S.

### 3.4 Przeszkody
- **Obstruction:** krzywa Maekawy ΔL = 10·log10(3 + 20N), N = 2δf/c, ograniczona do 25 dB, realizowana kaskadą 5 półek (błąd ≤ 1,5 dB w zakresie 125 Hz–8 kHz). Działa tylko na dźwięk bezpośredni.
- **Occlusion:** R(f) wg Sharpa (1978): prawo masy do f_c/2, przejście do minimum koincydencji, powyżej f_c wzrost 9 dB/okt. Przegroda podwójna: rezonans f0 = 60·√((m1+m2)/(m1·m2·d)), wzrost powyżej f0 ograniczony do R1+R2+6 dB i do R_masa+12 dB (mostki akustyczne) **[kalibracja]**. Całość jest ograniczona do 70 dB (przenoszenie boczne) **[kalibracja]**. Przeciek przez szczeliny liczony jest jako izolacyjność wypadkowa τ = (1−p)·τ_ściany + p. Kaskada: półka niska, 4 półki wysokie oraz 2 filtry szczytowe na f_c i f0. Rysunek: `img/occlusion.png`.
- `Amount` skaluje dopasowane wzmocnienia (w dB), więc nie powstaje filtrowanie grzebieniowe; przy 0 % tor jest bitowo przezroczysty.

| Materiał | m [kg/m²] | f_c [Hz] | η | Przeciek wbudowany | Uwagi |
|---|---|---|---|---|---|
| Heavy Curtain | 0,7 | — | — | — | krzywa empiryczna 1–10 dB **[kalibracja]** |
| Glass 6 mm | 15 | 2000 | 0,03 | — | |
| Hollow Door | 9 | 6000 | 0,03 | 0,3 % | |
| Solid Wood Door | 25 | 700 | 0,03 | 0,05 % | |
| Drywall (stud wall) | 2 × 10 | 2800 | 0,03 | — | pustka 75 mm → f0 ≈ 98 Hz |
| Brick 12 cm | 220 | 200 | 0,1 | — | |
| Concrete 20 cm | 460 | 95 | 0,1 | — | |
| Car Body | 12 | 10000 | 0,1 | — | limit 40 dB, rezonans kabiny 80 Hz **[kalibracja]** |

`Thickness` skaluje masę (×k) oraz f_c (÷k). `Leak` 0–100 % odpowiada udziałowi otwartej powierzchni 0–10 % (skala logarytmiczna).

### 3.5 Auto Gain
Model deterministyczny: przewidywana zmiana głośności ważonej krzywą K (BS.1770) dla szumu różowego. Na siatce tercjowej sumowane są analityczne charakterystyki toru (odległość, efekt zbliżeniowy, powietrze, przeszkody, panorama/model głowy, energia pomieszczenia, okluzja) i porównywane z mocą wejścia. Źródła stereo zakładają energię S/M = 0,3 **[kalibracja]**. Wynik jest ograniczany do ±`Auto Gain Range` i wygładzany (50 ms). Model liczony jest tylko przy włączonym Auto Gain (co 256 próbek).

## 4. Parametry

| ID | Nazwa | Zakres | Domyślnie | Skala |
|---|---|---|---|---|
| inputMode | Input | Auto / Mono / Stereo | Auto | wybór |
| renderMode | Render Mode | Speakers / Headphones | Speakers | wybór |
| pan | Pan | −100…+100 | 0 | lin. |
| timePan | Time Pan | 0–100 % | 50 % | lin. |
| panLaw | Pan Law | 0 / −3 / −4,5 / −6 dB | −3 dB | wybór |
| monoSafe | Mono-safe | Off/On | On | przełącznik |
| distance | Distance | 0,1–1000 m | 1 m | log |
| recDistance | Recorded Distance | 0,1–10 m | 1 m | log |
| rolloff | Rolloff | 0–12 dB/dd | 6 | lin. |
| airAbsorption | Air Absorption | Off/On | On | przełącznik |
| temperature | Temperature | −10…40 °C | 20 | lin. |
| humidity | Humidity | 10–100 % | 50 | lin. |
| micPattern | Mic Pattern | Omni / Cardioid / Figure-8 | Omni | wybór |
| propDelay | Propagation Delay | Off/On | Off | przełącznik |
| room | Room | Off, Open A–C, Small A–C, Medium A–C, Large A–C, Hall A–C (16) | Off | wybór |
| roomLevel | Room Level | −24…+12 dB | 0 | lin. |
| decay | Decay | 50–200 % | 100 % | lin. |
| width | Width | 0–200 % | 100 % | lin. |
| bassMono | Bass Mono | Off, 40…300 Hz | Off | wybór |
| obstacle | Obstacle | None / Obstruction / Occlusion | None | wybór |
| material | Material | 8 materiałów | Solid Wood Door | wybór |
| amount | Amount | 0–100 % | 100 % | lin. |
| obstacleSize | Obstacle Size | 0–3 m | 1 m | lin. |
| thickness | Thickness | 0,25–4× | 1× | log |
| leak | Leak | 0–100 % | 0 | lin. |
| autoGain | Auto Gain | Off/On | **Off** | przełącznik |
| autoGainRange | Auto Gain Range | 0–36 dB | 24 dB | lin. |
| output | Output | −24…+24 dB | 0 | lin. |
| monoDepth | Mono Depth | Off/On | Off | przełącznik |
| eqAmbience | Equalized Ambience | Off/On | **On** | przełącznik |
| bypass | Bypass | Off/On | Off | przełącznik (bypass hosta, crossfade 20 ms) |

Przełączniki są zaimplementowane jako dwustanowe parametry wyboru. Ich znormalizowana wartość jest kwantowana, więc odtworzony stan jest dokładny (wymóg pluginval). Stan wtyczki ma atrybut `stateVersion` (obecnie 2) oraz przechowuje skalę UI i nazwę presetu.

## 5. Weryfikacja (stan na ostatni commit)

| Kryterium (PROMPT §11) | Wynik |
|---|---|
| 1. Podwojenie odległości −6,02 dB ±0,1 | ✅ |
| 2. ISO 9613-1: 3,7 / 9,7 / 32,8 / 117 dB/km ±2 %; filtr ≤ 1 dB (< 24 dB) | ✅ (10–1000 m, 44,1–192 kHz) |
| 3. ITD 0,66 ± 0,02 ms; ILD ≈ 0 dB przy 200 Hz, ≥ 12 dB > 4 kHz | ✅ |
| 4. Prawo −3 dB: L²+R² stałe ±0,1 dB | ✅ |
| 5. Mono-safe: ≤ 3 dB / tercję | ✅ (8 pozycji × 2 Time Pan × 2 Width) |
| 6. Width nie zmienia sumy mono; Width 0 → L = R | ✅ (≤ −100 dB) |
| 7. Okluzja: nachylenie 6 ± 1,5 dB/okt, Leak, Amount 0 = null | ✅ |
| 8. Maekawa ±1,5 dB | ✅ |
| 9. Automatyka bez nieciągłości, NaN/Inf | ✅ |
| 10. Niezależność od fs ±0,5 dB | ✅ |
| 11. Niezależność od bufora / determinizm | ✅ (bitowo identyczne) |
| 12. Stan i presety | ✅ (pluginval, testy presetów) |
| 13. Neutralność, latencja 0 | ✅ (null ≤ −120 dBFS) |
| pluginval, strictness 10 | ✅ |
| Walidator VST3 Steinberga | ✅ 47/47 |
| CPU (48 kHz, stereo, maszyna wirtualna Linux x64, samo przetwarzanie) | ok. 0,5 % rdzenia (Room Off), ok. 1,5 % (Medium A) |

Oba wyniki mieszczą się w celach (≤ 1 % i ≤ 3 %). Pomiar pochodzi z maszyny wirtualnej w chmurze i jest informacyjny — należy go powtórzyć na docelowym sprzęcie (`ambience_tests`, test 15).

## 6. Ograniczenia i znane odstępstwa
- Interpolacja Lagrange'a 3. rzędu tłumi najwyższe częstotliwości przy opóźnieniach ułamkowych (przy 48 kHz spadek rośnie powyżej ok. 14 kHz: kilka dB przy 18 kHz, więcej bliżej Nyquista). Dotyczy to ucha dalszego w trybie Headphones, Time Pan przy wyłączonym Mono-safe i Dopplera. Przy fs ≥ 88,2 kHz efekt jest poza pasmem.
- Model głowy jest sferyczny (bez małżowiny i ramion), więc nie daje wskazówek przód/tył ani elewacji. HRTF z plików SOFA to punkt COULD (niezrealizowany).
- W trybie Headphones wczesne odbicia są panoramowane amplitudowo (bez modelu głowy).
- Wartości materiałów, pomieszczeń i dekorelacji wymagają kalibracji odsłuchowej w reżyserce (patrz scenariusze w README).
- Formaty wielokanałowe (5.1/7.1/Atmos) nie są obsługiwane. Panner jest osobnym modułem, co pozwala je dodać.
- AAX nie jest budowany (wymaga AAX SDK i podpisu PACE) — patrz README.

## 7. Literatura
Pełna lista: [`PROMPT.md`](../PROMPT.md), sekcja 14. Dodatkowo: Sharp B. H. (1978), *Prediction methods for the sound transmission of building elements*, Noise Control Engineering 11, 53–63; ISO 9613-2 (tabela współczynników); Zavalishin V., *The Art of VA Filter Design* (SVF/TPT).
