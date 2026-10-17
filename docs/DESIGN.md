# SAMS AMBIENCE CONTROL — projekt DSP (DESIGN)

Dokument opisuje tor sygnału, zastosowane modele, parametry, założenia i wyniki weryfikacji wtyczki **SAMS AMBIENCE CONTROL** (do wersji 1.0.0 pod nazwą roboczą „Ambience”) (specyfikacja: [`PROMPT.md`](../PROMPT.md)). Wartości oznaczone **[kalibracja]** nie pochodzą bezpośrednio z literatury — dobrano je odsłuchowo/inżyniersko w zakresie podawanym przez źródła i należy je weryfikować odsłuchem.

## 1. Architektura

```
Source/dsp/        czysty C++ (bez JUCE) — silnik, modele, testowalny i renderowalny offline
  DspCore.h          biquady RBJ, sekcje 1. rzędu, SVF (TPT), linia opóźniająca Lagrange 3. rzędu,
                     all-passy, siatka tercjowa, dopasowywanie charakterystyk (Levenberg–Marquardt)
  AcousticModels.h   ISO 9613-1, Maekawa, prawo masy / Sharp, materiały, model głowy Browna–Dudy,
                     odległość krytyczna, ważenie K (BS.1770)
  RoomModel.h        wczesne odbicia (źródła pozorne, dyfuzja) + FDN 16 linii
  Stereoizer.h       Mono → Stereo: bank 8 pasm LR8, para Hilberta, estymator otoczenia (Keep Mono)
  Eq.h               EQ użytkownika (Input / Output / Reverb only): 5 biquadów RBJ, wygładzanie
  Params.h           tabela parametrów (stałe ID), EngineParams, presety fabryczne
  SpatialEngine.*    kompletny tor przetwarzania
Source/            wtyczka JUCE: procesor, stan, presety, UI (Source/ui; wykres EQ: ui/EqView)
tests/             testy akceptacyjne (PROMPT.md, sekcja 11)
tools/             renderer offline (CLI), sygnały testowe, wykresy charakterystyk
```

Silnik działa próbka po próbce, a wartości sterujące liczy w **tyku kontrolnym co 32 próbki**, wyrównanym do bezwzględnego licznika próbek. Dzięki temu wynik nie zależy od rozmiaru bufora hosta — test daje identyczny render przy buforach 32, 97 i 1024. Parametry są wygładzane filtrem jednobiegunowym (30 ms; Auto Gain 50 ms). Wzmocnienia i opóźnienia są interpolowane liniowo wewnątrz tyku. Latencja wynosi **0 próbek**.

## 2. Tor sygnału

```
wejście (1, 2 lub więcej kanałów; wielokanałowe składane do stereo)
  └─ EQ Input (L i P osobno)                            (pomieszczenie słyszy już skorygowany sygnał)
  └─ tryb Input → M = (L+R)/2, S = (L−R)/2 · ψ         (ψ: 0 = źródło mono, 1 = źródło stereo; płynnie)
  └─ [Mono → Stereo: wyśrodkowanie treści mono ze ścieżki stereo, punkt 3.2c]
  └─ kompensacja efektu zbliżeniowego (M, S)
  └─ [opóźnienie propagacji r/c — Doppler]              (domyślnie OFF, crossfade przy przełączaniu)
  ├─ ŚCIEŻKA BEZPOŚREDNIA
  │    poziom wg odległości → pochłanianie powietrza → Obstruction
  │    → Width (S stereo · w, S dekorelowane z M dla źródeł mono, Bass Mono na S)
  │    → Panner: Speakers (amplituda + czas, Mono-safe) | Headphones (model głowy), crossfade
  └─ ŚCIEŻKA POMIESZCZENIA (sygnał sprzed tłumienia odległościowego — pole pogłosowe ma stały poziom)
       EQ Reverb only → wczesne odbicia (6 źródeł pozornych / odbicie od gruntu) + FDN, poziom wg DRR
  → suma → [wyjście mono: (L+R)·norm] → Occlusion (dźwięk bezpośredni + pogłos pomieszczenia źródła)
  → EQ Output (każdy kanał, w surround bez LFE) → Auto Gain → Output
```

## 3. Modele

### 3.1 Odległość
- **Poziom:** g = (r_rec / r)^(n/6,02), gdzie n = `Rolloff` [dB na podwojenie odległości]. Dla n = 6 jest to prawo odwrotnych kwadratów. Wzmocnienie przy przybliżaniu jest ograniczone do +24 dB.
- **Pochłanianie w powietrzu** wg ISO 9613-1 (wzory 3–5, zał. B). Tłumienie α(f)·max(0, r − r_rec) realizuje kaskada 5 półek wysokich + wzmocnienie szerokopasmowe. Wzmocnienia dopasowuje deterministyczny solwer Levenberga–Marquardta na siatce tercjowej 25 Hz–16 kHz, a częstotliwości półek są rozmieszczane względem punktu, w którym tłumienie osiąga 24 dB. Błąd wynosi ≤ 1 dB wszędzie tam, gdzie tłumienie < 24 dB (test dla 10–1000 m, 44,1–192 kHz). Rysunek: `img/air_absorption.png`.
- **DRR i pogłos:** poziom pola rozproszonego jest ustawiony tak, by zrównał się z dźwiękiem bezpośrednim w odległości krytycznej r_c ≈ 0,057·√(Q·V/T60) (Kuttruff; Q = 2). Energia ogona FDN jest normalizowana na podstawie odpowiedzi impulsowej zmierzonej w `prepare()`, w paśmie 500 Hz – 2 kHz (tam obowiązuje model odległości krytycznej; ciemniejsza góra nie podnosi przez to reszty pasma).
- **Wczesne odbicia:** metoda źródeł pozornych 1. rzędu (Allen i Berkley 1979) w prostopadłościanie. Każde odbicie ma własną drogę, poziom (wsp. odbicia √(1−ᾱ) z Sabine'a), kierunek i opóźnienie względem dźwięku bezpośredniego — stąd ITDG maleje wraz z odległością. Gdy źródło wychodzi poza pomieszczenie, jego pozycja jest przycinana do ścian z marginesem 12 % krótszego wymiaru (0,3–3 m) — źródło „przyklejone” do ściany dawałoby sztuczny filtr grzebieniowy o krótkim opóźnieniu. Odbicie od podłogi w pomieszczeniach ma współczynnik ×0,6 (meble, siedzenia, ludzie) **[kalibracja]**. Każde odbicie jest tłumione w górze: jednobiegunowy filtr ściany (grunt 1,5 kHz, fasady 5 kHz, pomieszczenia 2,5–6 kHz zależnie od HF ratio) razem z pochłanianiem powietrza na średniej drodze odbić (ISO 9613-1, dopasowanie przy 8 kHz), a następnie **rozpraszane**: 3 krótkie all-passy na kanał (0,6–2,9 ms × skala pomieszczenia, g = 0,5–0,6) zamieniają każde dyskretne odbicie w gęstą wiązkę. Dzięki temu odbicia nie tworzą z dźwiękiem bezpośrednim wyraźnego filtra grzebieniowego (efekt „phasera/flangera”, szczególnie przy automatyce odległości). W trybie Outdoor jest tylko odbicie od gruntu (wysokości 1,6 m, współczynnik 0,55 **[kalibracja]**).
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
- **FDN:** 16 linii o długościach będących liczbami pierwszymi, rozłożonych w stosunku 1 : 2,3 wokół czasu średniej drogi swobodnej (0,75·4V/(S·c), 12–35 ms; dolna granica zapewnia gęstość modów w małych pokojach). Macierz sprzężenia: znormalizowana macierz Hadamarda 16×16 (szybka transformata Walsha-Hadamarda) — każda linia zasila wszystkie pozostałe po równo. Macierz Householdera tej wielkości zostawia w każdej linii 7/8 jej własnego sygnału, więc linie dzwonią jak osobne filtry grzebieniowe. Wejście przechodzi przez filtr ściany i 4 all-passy dyfuzyjne (1,3–5,3 ms × skala pomieszczenia), a ogon startuje od **pierwszego** odbicia (pole rozproszone narasta od początku, bez „dziury” przed ogonem). Wyjścia L, P i mono to ortogonalne wiersze macierzy Hadamarda.
- **Modulacja:** każda linia ma powolną, subtelną modulację długości (0,5 ms międzyszczytowo, 16 różnych częstotliwości 0,15–0,8 Hz, deterministyczne oscylatory kwadraturowe, odczyt przez interpolator all-pass 1. rzędu — płaski moduł). Zmienność w czasie rozmywa stałe rezonanse sieci, więc długie ogony nie dzwonią metalicznie (Lexicon „spin/wander”, Dattorro 1997; Schlecht i Habets 2015). Odchylenie wysokości < 2,5 centa. Efekt: rozrzut widma ogona 6–7 dB zamiast do 12,6 dB (idealny szum gaussowski: 5,6 dB). Wynik pozostaje deterministyczny i niezależny od bufora.
- **Zanik zależny od częstotliwości:** w każdej linii (Jot) wzmocnienie dla T60 średnich, półka niska 1. rzędu 250 Hz dla **bass ratio** (Beranek: T60 basu/T60 średnich — 1,1–1,3 w salach, kościele i hali, 0,95 w studiu, 1,0 w małych pokojach), półka wysoka 1. rzędu 2,5 kHz dla ścian (T60·HF ratio) oraz jednobiegunowy filtr **pochłaniania powietrza** dopasowany przy 8 kHz do α(8 kHz) − α(1 kHz) wg ISO 9613-1 (20 °C, 50 %) na czas przelotu linii. W dużych wnętrzach powietrze dominuje w górnych oktawach: kościół (T60 4 s) ma T30 ≈ 1,3 s przy 8 kHz i ≈ 0,7 s przy 16 kHz (wcześniej 3,3 s / 2,6 s, co dawało „syczący”, biały ogon). Pomiary (T30 per oktawa, gęstość ech wg Abela i Huanga, widmo ogona, siła filtra grzebieniowego) są testami regresji: `tests/EngineTests.cpp`, test 14f.
- **Efekt zbliżeniowy:** stosunek odpowiedzi mikrofonu gradientowego 1. rzędu w nowej i nagranej odległości, H(s) = (s + B·c/r)/(s + B·c/r_rec), gdzie B to udział składowej gradientowej (omni 0, kardioida 0,5, ósemka 1). Jest to półka ograniczona — przy oddalaniu usuwa podbicie basu z bliskiego nagrania.
- **Opóźnienie propagacji / Doppler:** linia opóźniająca z interpolacją Lagrange'a 3. rzędu, maks. 1000 m / c ≈ 2,9 s. Nie jest zgłaszane hostowi jako latencja (to efekt, nie kompensacja).

### 3.2 Panorama
- **Speakers:** panner hybrydowy. Pozycja amplitudowa to pₐ = p·(1 − t·(1 − |p|)), a różnica czasu τ = t·p·τ_max, gdzie t = `Time Pan`. Na skrajach panoramy źródło jest zawsze w jednym głośniku. τ_max = 1 ms przy pełnym przesunięciu czasowym (Blauert 1997). Przy **Mono-safe** τ_max = 0,3 ms, a opóźniana jest tylko składowa < 1,2 kHz (rozdział komplementarny M − LP + LP(t−τ), więc przy τ = 0 sygnał jest bitowo nietknięty). Opóźnienie działa wyłącznie na M; składowa S trafia do L/P antysymetrycznie z wagą k = min(G_L, G_P), dlatego **suma mono nie zależy od Width** przy żadnym ustawieniu.
- **Prawa panoramy:** 0 dB (balans), −3 dB (sin/cos), −4,5 dB (średnia geometryczna), −6 dB (liniowe). Dla źródeł stereo wzmocnienia są normalizowane tak, by w centrum wynosiły 1 (przezroczystość).
- **Headphones:** model sferycznej głowy Browna i Dudy (1998): a = 8,75 cm, opóźnienia per ucho wg ich wzoru (3) (ITD Woodwortha), filtr cienia głowy 1-biegun/1-zero z α(θ) (α_min = 0,1, θ_min = 150°). Filtr jest znormalizowany do α(90°), więc źródło z przodu brzmi płasko. Opóźnienie fazowe filtra (przy 750 Hz) jest odejmowane, żeby całkowite ITD w paśmie < 1,5 kHz odpowiadało wzorowi Woodwortha (test: 0,656 ± 0,02 ms). Źródło stereo to dwa wirtualne źródła rozstawione o ±30°·Width. Poniżej 1 m płynnie włącza się paralaksa akustyczna (1/r osobno dla każdego ucha). Rysunek: `img/headphones.png`.

### 3.2a Mono Depth
Tryb ściśle monofoniczny. Sygnał wyjściowy to M ścieżki bezpośredniej (odległość, powietrze, efekt zbliżeniowy, obstruction) plus **dedykowane wyjście mono pomieszczenia**: wczesne odbicia z wagą √(g_L² + g_P²) (bez panoramy) oraz ogon FDN z wszystkich 16 linii (własny, ortogonalny wiersz macierzy Hadamarda, waga √2/4). Energia tego wyjścia jest równa energii L+P wersji stereo (test: ±1,5 dB), więc przełączanie nie zmienia bilansu pogłosu. Składanie stereo pogłosu do mono dałoby tylko 4 linie i ubytki gęstości. Przy wyjściu stereo L = P = mono·g_c (g_c = prawo panoramy w centrum), co jest bitowo równe źródłu w centrum. Na ścieżce mono (1→1) używana jest ta sama ścieżka. Przełączanie crossfade'em 30 ms.

### 3.2b Equalized Ambience
Główny przełącznik automatycznego EQ zależnego od odległości i położenia (domyślnie ON). Obejmuje: pochłanianie powietrza, kompensację efektu zbliżeniowego oraz *position EQ* — półkę wysoką 5 kHz na kanale oddalonym od źródła (tryb Speakers), −2,5 dB·|pan| **[kalibracja]**, działającą tylko na M (Width pozostaje zgodne z mono). Po wyłączeniu odpowiedź częstotliwościowa jest płaska (test ±0,3 dB), a poziom w paśmie środkowym się nie zmienia (±0,5 dB).

### 3.2c Mono → Stereo
Konwersja mono → stereo ze stałą sumą mono i bez zabarwienia kanałów (`Source/dsp/Stereoizer.h`). Wyjście to L = M′ + a·S, P = M′ − a·S, gdzie:

- M′ to wszechprzepustowa wersja M, S to sygnał w **kwadraturze** do M′ (±90°), a więc |M′ + jaS| = |M′ − jaS|: oba kanały mają identyczny moduł w każdej częstotliwości (brak przechyłu i zabarwienia; test: ±0,25 dB), a L + P = 2M′ (test: błąd ≤ −100 dB względem M′).
- Znak kwadratury zmienia się w pasmach ok. 1 oktawy: rozgałęzienia Linkwitza-Rileya 8. rzędu na 200, 420, 850, 1700, 3300, 6500 i 15 000 Hz. Wyjścia LP i HP filtru LR są w fazie, więc (LP − HP)/(LP + HP) jest rzeczywiste i zmienia znak na częstotliwości podziału. Ścieżka M′ przechodzi przez sumy LP + HP (wszechprzepustowe), ścieżka S przez różnice LP − HP; na skrajach S używa tylko HP (dół) i LP (góra), więc bas < 200 Hz i skrajna góra zostają mono. Na koniec para Hilberta Niemitalo (2 × 4 sekcje IIR, błąd ≤ 0,7°) ustawia S w kwadraturze względem M′.
- Różnica faz między kanałami zmienia znak od pasma do pasma, co daje rozłożony obraz (lateralizacja zależna od częstotliwości; Orban 1970, Gerzon 1992) bez echa i bez ubytków w mono.
- a = 0,7·Width **[kalibracja]**. Korelacja L/P dla szumu różowego: 0,81 przy 100 %, 0,41 przy 200 % (bas mono wlicza się do korelacji).
- Aktywne, gdy źródło jest mono: wejście 1-kanałowe, *Input = Mono*, albo detektor treści mono na wejściu stereo. **Detektor (1.5):** energie L, P i korelacja wzajemna kanałów po filtrze górnoprzepustowym 200 Hz, uśredniane ze stałą 0,3 s, ale tylko w fragmentach do 30 dB poniżej niedawnego szczytu (szczyt opada 20 dB/s) i powyżej −100 dBFS — pauzy i cisza nie zmieniają decyzji. Mono, gdy |korelacja| > 0,985 albo jeden kanał jest o ponad 40 dB cichszy (nagranie tylko w kanale 1 rekordera lub kamery); stereo, gdy korelacja < 0,95 (histereza). Rozpoznaje więc treść identyczną, spanoramowaną, minimalnie różną w kanałach, odwróconą w fazie i jednostronną. Taka treść L = a·x, P = b·x jest najpierw składana do wyśrodkowanego źródła M = (a·L + b·P)/√(2(a² + b²)) — moc jest zachowana, a dla identycznych kanałów to zwykłe (L+P)/2. Prawdziwe stereo: test zerowy ≤ −120 dB. Przełączanie crossfade'em 30 ms. Na ścieżce mono z wyjściem mono (1→1) stereo jest niemożliwe; silnik zgłasza stan (aktywne / wejście stereo / wyjście mono / Mono Depth), a linia statusu w UI go wyjaśnia.
- **Struktura (1.4):** jawny bank 8 pasm — drzewo rozgałęzień LR8 z kompensacją all-pass, więc każde pasmo jest w fazie z sumą (M′). S = Σ ±G_b·pasmo_b (znaki naprzemienne, pasmo basowe i najwyższe pominięte), a potem para Hilberta. Ponieważ każde pasmo jest w fazie z M′, S pozostaje w kwadraturze także przy zmiennych w czasie wzmocnieniach G_b.
- **Keep Mono:** G_b = √(udział otoczenia w paśmie b). Estymator bez latencji (co 32 próbki): obwiednia energii 10 ms; *ogon* = max(ogon·spadek T60, κ·E), T60 = 0,6 s, κ = 0,05 (−13 dB) **[kalibracja]** (model zaniku pogłosu, Lebart i in. 2001); *faza zaniku*: spadek E poniżej szczytu (który sam opada z T60) — 3 dB → bezpośredni, 9 dB → otoczenie; *tło* (room tone) = minimum z okien 0,5 s (pierwsze okno 0,2 s; minimum statistics, Martin 2001) × 2. Tło uczy się tylko z sygnału ustalonego (30 ms po wejściu z ciszy) i z opóźnieniem 50 ms (historia obwiedni), więc cyfrowa cisza nie zeruje estymaty, a poszerzanie wraca od razu po pauzie. Udział = max(faza zaniku, (ogon + tło)/E), wygładzany 15 ms. Z Keep Mono a = 1,0·Width (otoczenie poszerzane w pełni; bez Keep Mono a = 0,7·Width). Test (syntetyczny dialog: sylaby + pogłos T60 0,6 s + tło −40 dB): korelacja L/P głosu 0,95 (mono), otoczenia 0,79; tło po 0,5 s cyfrowej ciszy 0,69; suma mono identyczna jak bez Keep Mono (≤ −100 dB). Starsze sesje otwierają się z Keep Mono OFF (brzmią jak w 1.3).
- Poszerzanie mono powyżej 100 % (punkt 3.3) używa tego samego Stereoizera; w trybie Headphones dwa wirtualne źródła rozstawia się o ±30°·Width jak dla stereo. Koszt: ok. 0,5 % rdzenia przy 48 kHz.

### 3.2d Proximity
Położenie słuchacza/mikrofonu w pomieszczeniu: x = W·(0,5 − 0,8·max(0, p − 0,5)), y = L·(0,5 − 0,4·p). p = 0 → środek (odbicia późne i równomierne), p = 0,5 → dotychczasowa pozycja (środek szerokości, 30 % długości), p = 1 → okolice narożnika (silne odbicia tuż po dźwięku bezpośrednim; odpowiednik „Proximity” w pogłosach filmowych). Test: energia odbić 0–20 ms względem 20–80 ms rośnie o > 3 dB przy p = 1.

### 3.2e Surround i Atmos (5.0 … 7.1.4)
Układy: 5.0, 5.1, 7.0, 7.1, 5.1.2, 5.1.4, 7.1.2, 7.1.4 (oraz LCR), z wejścia mono, stereo albo tego samego układu. Pozycje głośników wg ITU-R BS.775 i łóżek Dolby Atmos (L/R ±30°, Ls/Rs ±110°, boczne ±90°, tylne ±150°, górne ±45°/±135° lub ±90° przy elewacji 45°).
- **Dźwięk bezpośredni:** panoramowanie parami ze stałą mocą (zasada Pulkkiego) po przodzie L–C–R (Pan ±100 → ±30°), więc wyśrodkowane źródło mono jest w kanale C (konwencja dialogu). Źródła stereo zachowują obraz L/R; szerokość (S) idzie na L/R. Time Pan, Mono-safe i Headphones nie dotyczą wyjść wielokanałowych.
- **Pomieszczenie:** każde odbicie jest panoramowane po pierścieniu głośników wg swojego kierunku (ściany boczne i tylna → surroundy, sufit → warstwa wysokościowa). Każdy kanał ma własny klaster dyfuzji i własny ortogonalny wiersz macierzy Hadamarda z 16 linii FDN. Moc ogona: C −6 dB, wysokościowe −1,5 dB, LFE 0; suma energii równa stereo (test: ±1 dB), więc DRR i Auto Gain są takie same jak w stereo.
- **Mono Depth** w surround: całość w kanale C. **Bypass:** mono → C, stereo → L/R, wejście wielokanałowe → te same kanały. Wejście wielokanałowe jest składane do źródła stereo (C −3 dB do obu, surroundy i wysokościowe −6 dB), a LFE wejścia przechodzi do LFE wyjścia bez zmian.
- Testy: `tests/EngineTests.cpp` (14j: routing, stała moc, energia względem stereo, wysokościowe, Mono Depth) oraz `tests/PluginHostTests.cpp` — procesor wtyczki w układach 1.0 … 7.1.4 (routing, LFE, bypass, migracja stanu), uruchamiany w CI.

### 3.2f EQ (Input / Output / Reverb only)
Trzy niezależne korektory użytkownika (`Source/dsp/Eq.h`), każdy z 5 sekcjami biquad RBJ liczonymi w podwójnej precyzji (TDF II):
- *Low Cut* — górnoprzepustowy 2. rzędu Butterwortha (12 dB/okt), 20 Hz – 1 kHz, 20 Hz = wyłączony;
- *Low Shelf* — półka niska (Q 0,707), 30–600 Hz, ±18 dB;
- *Peak* — filtr szczytowy, 80 Hz – 12 kHz, ±18 dB, Q 0,3–8;
- *High Shelf* — półka wysoka (Q 0,707), 1–16 kHz, ±18 dB;
- *High Cut* — dolnoprzepustowy 12 dB/okt, 1–20 kHz, 20 kHz (lub ≥ 0,49·fs) = wyłączony.

Położenie w torze: **Input** na L i P przed wszystkim (pomieszczenie i detektor Mono → Stereo słyszą skorygowany sygnał), **Reverb only** na wejściu pomieszczenia (odbicia i ogon; dźwięk bezpośredni bez zmian), **Output** na każdym kanale wyjściowym (w surround bez LFE, na ścieżce mono jeden kanał). Parametry są wygładzane w tyku kontrolnym (30 ms) w skali log-częstotliwości i dB, a współczynniki liczone tylko w trakcie zmian. *EQ On* = Off prowadzi wszystkie pasma płynnie do neutralnych (bez trzasków). Pasmo neutralne (wzmocnienie 0 dB, filtr na granicy) jest pomijane, a EQ w całości neutralny nie przetwarza sygnału — wynik jest bitowo identyczny (test). Pasmo, które się włącza, startuje ze stanu spoczynku. Wykres w UI liczy charakterystykę tymi samymi wzorami (`ParametricEq::responseDb`). *Auto Gain* celowo nie uwzględnia EQ: korekcja jest decyzją miksującego i zmienia poziom tak, jak ją ustawiono. Testy (14k): Input +6 dB przy 1 kHz (±0,1 dB) i bez zmian dwie oktawy niżej, Output Low Cut 200 Hz (50 Hz < −20 dB, 1 kHz ±0,2 dB), EQ Reverb only (półka −12 dB od 2 kHz) zmienia tylko pogłos (8 kHz −11,8 dB, 250 Hz bez zmian; przy Room = Off wyjście bitowo identyczne), przełączanie Off bez trzasków i dokładnie neutralne, ścieżka mono; testy hosta: tekst parametrów, stan i migracja, dźwięk, zakładki edytora.

### 3.3 Szerokość
Dla źródeł stereo S jest mnożone przez Width. Źródła mono powyżej 100 % poszerza Stereoizer (punkt 3.2c): S w kwadraturze do M′, a = 1,0·(Width − 1) **[kalibracja]**, więc każdy kanał ma płaską charakterystykę, a suma mono zachowuje moduł źródła. Wcześniejszy dekorelator z all-passów Schroedera (Kendall 1995) dawał w każdym kanale filtr grzebieniowy ok. ±5 dB (efekt phasera) i został usunięty. Bass Mono to filtr górnoprzepustowy LR4 (2 × SVF Butterwortha) na S.

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
Model deterministyczny: przewidywana zmiana głośności ważonej krzywą K (BS.1770) dla szumu różowego. Na siatce tercjowej sumowane są analityczne charakterystyki toru (odległość, efekt zbliżeniowy, powietrze, przeszkody, panorama/model głowy, energia pomieszczenia, okluzja) i porównywane z mocą wejścia. Źródła stereo zakładają energię S/M = 0,3 **[kalibracja]**. Energia pomieszczenia liczona jest per częstotliwość: odbicia przez ich filtr (ściana + powietrze; dyfuzory są all-passami), ogon z jego widmem zmierzonym oktawowo przy pierwszym `prepare()` (raz na proces, przy 48 kHz). Błąd modelu dla wszystkich 16 przestrzeni i odległości 0,5–150 m: ±0,5 dB (test 14; wcześniej model zawyżał energię pogłosu o 4–8 dB, więc przy włączonym pomieszczeniu wynik był za cichy). Wynik jest ograniczany do ±`Auto Gain Range` i wygładzany (50 ms). Model liczony jest tylko przy włączonym Auto Gain (co 256 próbek). EQ użytkownika (punkt 3.2f) nie wchodzi do modelu.

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
| monoToStereo | Mono to Stereo | Off/On | Off | przełącznik |
| keepMono | Keep Mono | Off/On | **On** (starsze sesje: Off) | przełącznik |
| proximity | Proximity | 0–100 % | 50 % | lin. |
| eqInOn, eqOutOn, eqRevOn | Input / Output / Reverb EQ On | Off/On | On | przełącznik |
| eq{In,Out,Rev}LowCut | … EQ Low Cut | 20–1000 Hz (20 = Off) | Off | log |
| eq{In,Out,Rev}LowShelfFreq | … EQ Low Shelf Freq | 30–600 Hz | 100 Hz | log |
| eq{In,Out,Rev}LowShelfGain | … EQ Low Shelf Gain | ±18 dB | 0 | lin. |
| eq{In,Out,Rev}PeakFreq | … EQ Peak Freq | 80–12 000 Hz | 1 kHz | log |
| eq{In,Out,Rev}PeakGain | … EQ Peak Gain | ±18 dB | 0 | lin. |
| eq{In,Out,Rev}PeakQ | … EQ Peak Q | 0,3–8 | 1 | log |
| eq{In,Out,Rev}HighShelfFreq | … EQ High Shelf Freq | 1–16 kHz | 8 kHz | log |
| eq{In,Out,Rev}HighShelfGain | … EQ High Shelf Gain | ±18 dB | 0 | lin. |
| eq{In,Out,Rev}HighCut | … EQ High Cut | 1–20 kHz (20 k = Off) | Off | log |
| bypass | Bypass | Off/On | Off | przełącznik (bypass hosta, crossfade 20 ms) |

Przełączniki są zaimplementowane jako dwustanowe parametry wyboru. Ich znormalizowana wartość jest kwantowana, więc odtworzony stan jest dokładny (wymóg pluginval). Stan wtyczki ma atrybut `stateVersion` (obecnie 2) oraz przechowuje skalę UI i nazwę presetu. Parametry, których nie ma w zapisanym stanie (np. dodane w nowszej wersji), dostają wartości domyślne — starsze sesje otwierają się z neutralnym EQ. Tekst wartości EQ: „Off” na granicy filtrów, „120 Hz”, „2.50 kHz”, „Q 1.00”; wpisywanie przyjmuje też „2.5k”, „off” i „Q 2”.

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
| CPU (48 kHz, stereo, maszyna wirtualna Linux x64, samo przetwarzanie) | ok. 0,4 % rdzenia (Room Off), ok. 2,3 % (Medium A), ok. 1,5 % (Mono → Stereo, bank 8 pasm) |

Oba wyniki mieszczą się w celach (≤ 1 % i ≤ 3 %). Pomiar pochodzi z maszyny wirtualnej w chmurze i jest informacyjny — należy go powtórzyć na docelowym sprzęcie (`ambience_tests`, test 15).

## 6. Ograniczenia i znane odstępstwa
- Interpolacja Lagrange'a 3. rzędu tłumi najwyższe częstotliwości przy opóźnieniach ułamkowych (przy 48 kHz spadek rośnie powyżej ok. 14 kHz: kilka dB przy 18 kHz, więcej bliżej Nyquista). Dotyczy to ucha dalszego w trybie Headphones, Time Pan przy wyłączonym Mono-safe i Dopplera. Przy fs ≥ 88,2 kHz efekt jest poza pasmem.
- Model głowy jest sferyczny (bez małżowiny i ramion), więc nie daje wskazówek przód/tył ani elewacji. HRTF z plików SOFA to punkt COULD (niezrealizowany).
- Tryb Headphones: odbicia mają ITD (Woodworth, z kompensacją fazy półki cienia jak dla dźwięku bezpośredniego; test: 0,646 ms poniżej 700 Hz) i cień głowy (wspólny filtr górnoprzepustowy na ucho, dokładny dla półki 1. rzędu), a oba uszy słyszą ten sam klaster dyfuzji. Ogon: sygnał S górnoprzepustowy 400 Hz (naturalna spójność basu w polu rozproszonym; Menzer i Faller 2009; test: korelacja < 150 Hz 0,99 wobec 0,74). Model nie uwzględnia elewacji.
- Dekorelator z szumu aksamitnego (velvet noise; Alary, Politis i Välimäki 2017) sprawdzono jako zamiennik all-passów w klastrach odbić: przy długości 8–12 ms i 20–80 impulsach daje zafalowanie tercjowe 4–7 dB (all-passy: 0,1 dB), więc pozostawiono all-passy. Szum aksamitny sprawdza się przy dłuższych dekorelatorach i syntezie ogona, nie przy krótkich klastrach odbić.
- Wartości materiałów, pomieszczeń i dekorelacji wymagają kalibracji odsłuchowej w reżyserce (patrz scenariusze w README).
- Obiekty Atmos (ADM) i formaty ambisoniczne nie są obsługiwane — tylko łóżka kanałowe do 7.1.4.
- AAX nie jest budowany (wymaga AAX SDK i podpisu PACE) — patrz README.
- Przy automatyce odległości opóźnienia odbić zmieniają się płynnie (fizycznie poprawny, lekki Doppler odbić przy szybkich ruchach), a dźwięk bezpośredni nie ma Dopplera, dopóki *Propagation Delay* jest wyłączone.
- Źródło znacznie dalej niż wymiary pomieszczenia (np. 50 m w kościele 50 × 20 m) ma geometrię odbić przyciętą do wnętrza; siła filtra grzebieniowego jest wtedy umiarkowana (0,22, test 14f mierzy do 10 m).

## 7. Literatura
Pełna lista: [`PROMPT.md`](../PROMPT.md), sekcja 14. Dodatkowo: Sharp B. H. (1978), *Prediction methods for the sound transmission of building elements*, Noise Control Engineering 11, 53–63; ISO 9613-2 (tabela współczynników); Zavalishin V., *The Art of VA Filter Design* (SVF/TPT). Silnik pomieszczeń i konwersja stereo:
- Schlecht S. J., Habets E. A. P. (2017). Feedback delay networks: echo density and mixing time. *IEEE/ACM TASLP* 25(2).
- Schlecht S. J., Habets E. A. P. (2015). Time-varying feedback matrices in feedback delay networks and their application in artificial reverberation. *JASA* 138(3).
- Prawda K., Välimäki V., Schlecht S. J. (2019). Improved reverberation time control for feedback delay networks. DAFx-19.
- Dattorro J. (1997). Effect design, part 1: reverberator and other filters. *JAES* 45(9).
- Abel J. S., Huang P. (2006). A simple, robust measure of reverberation echo density. 121st AES Convention.
- Alary B., Politis A., Välimäki V. (2017). Velvet-noise decorrelator. DAFx-17.
- Beranek L. L. (2004). *Concert Halls and Opera Houses* (bass ratio, IACC).
- Zahorik P., Brungart D. S., Bronkhorst A. W. (2005). Auditory distance perception in humans: a summary of past and present research. *Acta Acustica* 91.
- Gerzon M. A. (1992). Signal processing for simulating realistic stereo images. 93rd AES Convention; Orban R. (1970). A rational technique for synthesizing pseudo-stereo from monophonic sources. *JAES* 18(2).
- Niemitalo O. Polyphase IIR Hilbert transformers (współczynniki pary Hilberta).
