# Zadanie: wtyczka VST3 „Ambience" — psychoakustyczna symulacja odległości, kierunku i przeszkód (miks filmowy)

## 0. Rola, cel, zasady

Działaj jako doświadczony inżynier DSP i programista wtyczek audio (C++20, JUCE), znający psychoakustykę, akustykę wnętrz i izolacyjność akustyczną przegród.

**Cel:** zaprojektuj, zaimplementuj, przetestuj i udokumentuj wtyczkę **VST3** (nazwa robocza **Ambience**), która realistycznie umieszcza źródło dźwięku w przestrzeni:

- **odległość** (bliżej / dalej),
- **kierunek lewo–prawo** z różnicami czasu i fazy (nie zwykły panning amplitudowy),
- **szerokość obrazu** (zawężanie / poszerzanie panoramy),
- **przeszkoda** między źródłem a słuchaczem/mikrofonem (ściana, drzwi, szyba, zasłona, róg budynku).

**Główne zastosowanie:** postprodukcja dźwięku w filmie — dopasowanie perspektywy dialogu do planu (zbliżenie ↔ plan ogólny), źródła poza kadrem, dźwięki zza ściany, drzwi lub okna, „impreza u sąsiadów", przejazdy.

**Zasady:**

- Każdy model DSP opieraj na zjawiskach opisanych w literaturze (sekcja 14); przy wzorach w kodzie podawaj źródło. Wartości bez źródła oznacz jawnie jako „kalibracja odsłuchowa".
- Priorytety: **MUST** — wymagane w v1; **SHOULD** — jeśli nie komplikuje architektury; **COULD** — opcjonalne / później.
- Jeśli coś w tej specyfikacji jest sprzeczne lub niewykonalne — zgłoś to i zaproponuj alternatywę, zamiast po cichu pomijać.

## 1. Wymagania wynikające z pracy przy filmie (MUST, dotyczą całej wtyczki)

1. **Synchronizacja z obrazem:** zero latencji w domyślnej konfiguracji; opóźnienie propagacji (r/c) domyślnie wyłączone. Każdą latencję zgłaszaj hostowi.
2. **Zgodność z mono** (downmix do mono): patrz 3.4 i 5.
3. **Automatyka:** wszystkie parametry automatyzowalne i wygładzane (bez „zipper noise" i trzasków); parametry wyboru (materiał, pomieszczenie, tryby) przełączane crossfade'em 20–50 ms.
4. **Wydajność:** sesje filmowe mają setki ścieżek — cel ≤ 1 % jednego rdzenia na instancję (48 kHz, stereo, `Room` = Off) i ≤ 3 % z pomieszczeniem (CPU klasy Apple M1 / Intel Core i5 10. gen.). Zweryfikuj benchmarkiem.
5. **Determinizm:** te same ustawienia i to samo wejście → identyczny render (żadnej losowości bez stałego ziarna; stan resetowany w `prepareToPlay` / `reset`).
6. **Trwałość projektów:** stałe ID parametrów, wersjonowany stan z migracją — projekt otwarty po latach musi brzmieć identycznie.
7. **Neutralność:** przy ustawieniach domyślnych wtyczka jest przezroczysta (stereo→stereo: null test ≤ −120 dBFS; mono→stereo: jedynie wybrane prawo panoramy).

## 2. Odległość (MUST)

Model łączy główne wskazówki percepcji odległości (przegląd: Zahorik, Brungart i Bronkhorst 2005):

1. **Poziom** — prawo odwrotnych kwadratów: −6 dB na podwojenie odległości, liczone względem odległości, z jakiej nagrano materiał (`Recorded Distance`, 0,1–10 m, domyślnie 1 m).
   - `Distance` 0,1–1000 m, skala logarytmiczna, domyślnie = `Recorded Distance`.
   - `Rolloff` 0–12 dB na podwojenie, domyślnie 6 (źródło punktowe; 3 = źródło liniowe, np. ruch uliczny).
   - Wzmocnienie przy przybliżaniu ogranicz do +24 dB.
2. **Pochłanianie w powietrzu** wg ISO 9613-1 (`Temperature` −10…40 °C, domyślnie 20; `Humidity` 10–100 %, domyślnie 50; ciśnienie 101,325 kPa). Tłumienie α(f)·max(0, `Distance` − `Recorded Distance`) odwzoruj filtrem (np. kaskadą filtrów półkowych/LP) z błędem ≤ 1 dB wszędzie tam, gdzie tłumienie < 24 dB.
3. **Stosunek dźwięku bezpośredniego do pogłosowego (DRR)** — najsilniejsza absolutna wskazówka odległości w pomieszczeniu (Bronkhorst i Houtgast 1999; Zahorik 2002). Pole pogłosowe ma w przybliżeniu stały poziom, a dźwięk bezpośredni maleje z odległością:
   DRR ≈ 20·log10(r_c / r), gdzie odległość krytyczna (promień pogłosowy) r_c ≈ 0,057·√(Q·V / T60) [m] (Kuttruff).
4. **Wczesne odbicia** — metoda źródeł pozornych (Allen i Berkley 1979) dla pomieszczenia prostopadłościennego, co najmniej 1. rzędu; każde odbicie ma własną drogę, poziom, pochłanianie i kierunek. Dzięki temu odstęp między dźwiękiem bezpośrednim a pierwszym odbiciem (ITDG) naturalnie maleje wraz z odległością. Gdy `Distance` przekracza wymiary pomieszczenia — zachowaj sensowne działanie (np. ogranicz geometrię odbić) i opisz to w `DESIGN.md`.
5. **Pogłos późny** — wydajna sieć FDN (Jot i Chaigne 1991; np. 8 linii, macierz Householdera/Hadamarda, czas zaniku zależny od częstotliwości), dekorelowana L/P, deterministyczna.
   - `Room`: Off / Outdoor / Small Room / Medium Room / Large Room / Hall. Domyślnie **Off** — wielu mikserów używa własnych pogłosów na szynach AUX; wtedy działają tylko wskazówki bezpośrednie.
   - Orientacyjnie (Q = 2): Small ≈ 30 m³, T60 ≈ 0,4 s; Medium ≈ 150 m³, 0,6 s; Large ≈ 600 m³, 0,9 s; Hall ≈ 5000 m³, 1,8 s. Outdoor = brak pola pogłosowego (COULD: odbicie od podłoża i rzadkie, odległe odbicia od budynków).
   - `Room Level` −24…+12 dB (korekta względem fizycznego DRR), `Decay` 50–200 % (skalowanie T60).
6. **Opóźnienie propagacji i efekt Dopplera** (`Propagation Delay`, domyślnie **OFF**) — opóźnienie r/c na interpolowanej linii opóźniającej (Lagrange 3. rzędu lub Thiran), np. do przejazdów; bez trzasków przy automatyce; nie zgłaszaj go jako latencji.
7. **Kompensacja efektu zbliżeniowego** (SHOULD) — materiał nagrany z bliska mikrofonem kierunkowym ma podbity bas, który przy oddalaniu powinien znikać (model mikrofonu gradientowego 1. rzędu). `Mic Pattern`: Omni (domyślnie, brak korekty) / Cardioid / Figure-8.
8. **Pole bliskie < 1 m** (SHOULD, tryb Headphones) — osobne kąty i odległości dla każdego ucha (paralaksa akustyczna) oraz wzrost ILD w niskich częstotliwościach (Brungart i Rabinowitz 1999; Duda i Martens 1998).
9. Słuchacze systematycznie nie doszacowują dużych odległości (kompresyjna funkcja potęgowa, wykładnik ≈ 0,4; Zahorik 2002) — dlatego `Rolloff` i `Room Level` pozwalają efekt świadomie przerysować.

## 3. Panorama lewo–prawo z różnicami czasu i fazy (MUST)

Podstawa: teoria dupleksowa (Rayleigh 1907) — poniżej ok. 1,5 kHz kierunek wyznaczają głównie międzyuszne różnice czasu/fazy (ITD/IPD), powyżej — różnice poziomu wynikające z cienia akustycznego głowy (ILD). Parametr `Render Mode`:

1. **Speakers** (domyślny — miks na monitorach i w kinie): panner hybrydowy amplitudowo-czasowy oparty na lokalizacji sumarycznej (summing localization) w bazie stereo ±30°. Pełne przesunięcie źródła pozornego daje ok. 15–18 dB różnicy poziomów albo ok. 1 ms różnicy czasu (0,6–1,5 ms zależnie od badania), a oba mechanizmy się sumują (time–intensity trading; Blauert 1997). `Time Pan` 0–100 % (domyślnie 50 %) określa udział składowej czasowej (różnica czasu = przesunięcie fazy rosnące z częstotliwością).
2. **Headphones** (binauralnie): model sferycznej głowy Browna i Dudy (1998):
   - ITD wg Woodwortha: ITD(θ) = (a/c)·(θ + sin θ), a = 8,75 cm, c = 343 m/s (maks. ≈ 0,66 ms),
   - filtr cienia głowy (1 biegun / 1 zero) osobno dla każdego ucha, z α(θ): α_min = 0,1, θ_min = 150°,
   - `Time Pan` nieaktywny (ITD/ILD wynikają z modelu głowy),
   - COULD: echo ramion i małżowiny; HRTF z plików SOFA (AES69).
3. `Pan` −100…+100 (L…P): w trybie Headphones mapowany na azymut ±90°, w trybie Speakers na pozycję w bazie stereo. `Pan Law` 0 / −3 / −4,5 / −6 dB (domyślnie −3 dB, stała moc).
4. **Mono-safe** (MUST, domyślnie ON, tryb Speakers): ogranicza lub zawęża pasmowo różnice czasu tak, aby suma L+R różniła się od sumy tego samego ustawienia z `Time Pan` = 0 o ≤ 3 dB w każdym paśmie tercjowym 20 Hz–20 kHz. Tryb Headphones jest z natury niezgodny z mono — UI to sygnalizuje; gdy Mono-safe jest wyłączone, a korelacja < 0, UI ostrzega.
5. Ruch źródła jest płynny: frakcyjne linie opóźniające z interpolacją, bez trzasków i słyszalnej modulacji wysokości dźwięku.

## 4. Tryb mono / stereo (MUST)

1. Obsługiwane układy szyn: 1→1, 1→2, 2→2.
2. Przełącznik w UI `Input`:
   - **Auto** (domyślnie) — według układu ścieżki,
   - **Mono** — L+R sumowane do źródła punktowego (dialog, foley),
   - **Stereo** — źródło stereo (atmosfery, muzyka): `Pan` przesuwa/obraca cały obraz z zachowaniem obu kanałów, a przy `Pan` = 0 obraz pozostaje nietknięty; w trybie Headphones kanały L i P to dwa wirtualne źródła rozstawione wokół azymutu `Pan` zgodnie z `Width`.
3. Przy wyjściu mono (1→1) kontrolki panoramy i szerokości są wyszarzone; działają odległość, przeszkoda i pomieszczenie (pogłos zsumowany do mono).

## 5. Szerokość panoramy (MUST)

1. `Width` 0–200 % (0 = mono, 100 = bez zmian, 200 = maksymalnie szeroko) działa na obraz źródła, w domenie M/S.
2. Źródło mono: poszerzanie przez dekorelację zgodną z mono — sygnał S wytwarzany z M (dekorelator allpass; Kendall 1995).
3. Składowa S trafia do L i P antysymetrycznie (±, równa amplituda, bez różnicy czasu), więc w trybie Speakers suma mono (L+R) nie zależy od `Width` przy dowolnym `Pan` i `Time Pan`. Przy skrajnym `Pan` szerokość naturalnie maleje do zera (obraz nie wychodzi poza głośnik).
4. `Bass Mono` (SHOULD): Off / 40–300 Hz — poniżej tej częstotliwości sygnał pozostaje mono.
5. W UI korelometr.

## 6. Przeszkoda między źródłem a słuchaczem (MUST)

`Obstacle`: None / Obstruction / Occlusion. Wspólny parametr `Amount` 0–100 % — płynne przejście „brak przeszkody ↔ pełna przeszkoda" (np. automatyka otwierania drzwi). `Amount` skaluje tłumienie (interpolacja parametrów filtrów w dB), a nie miesza sygnałów — unikaj filtrowania grzebieniowego.

1. **Obstruction** — przeszkoda w tym samym pomieszczeniu (filar, samochód, róg budynku, tłum). Dyfrakcja na krawędzi wg Maekawy (1968): ΔL ≈ 10·log10(3 + 20·N), N = 2δ/λ, δ = różnica dróg (`Obstacle Size`, 0–3 m), praktyczny limit ok. 20–25 dB. Tłumiony jest tylko dźwięk bezpośredni, pogłos nie — DRR spada, źródło brzmi dalej i „zza czegoś".
2. **Occlusion (transmisja przez przegrodę)** — źródło w innym pomieszczeniu (za ścianą, drzwiami, oknem). Filtr izolacyjności R(f):
   - prawo masy: R ≈ 20·log10(m·f) − 47 dB (padanie polowe; +6 dB na oktawę i na podwojenie masy),
   - spadek izolacyjności w okolicy częstotliwości koincydencji f_c,
   - rezonans masa–powietrze–masa f0 dla przegród podwójnych,
   - przeciek przez szczeliny (`Leak` 0–100 % → udział otwartej powierzchni ok. 0–10 %, skala logarytmiczna): izolacyjność wypadkowa R = −10·log10(Σ S_i·τ_i / S) — szczeliny „rozjaśniają" dźwięk zza drzwi.

   Filtr obejmuje dźwięk bezpośredni i pogłos pomieszczenia źródła (słyszymy „pokój za ścianą"). Parametry: `Material`, `Thickness` (0,25–4×, skaluje masę), `Leak`.
3. Materiały — wartości orientacyjne: zweryfikuj w literaturze (np. Hopkins 2007) i skalibruj odsłuchowo. Kontrola dla przegród masywnych: Rw ≈ 37,5·log10(m) − 42 dB (EN ISO 12354-1).

| Materiał (UI) | m [kg/m²] | f_c | Rw orient. | Charakter |
|---|---|---|---|---|
| Heavy Curtain | 0,5–1 | — | ~5 dB | tłumi głównie powyżej 1 kHz |
| Glass 6 mm | 15 | ~2 kHz | ~31 dB | wyraźny spadek izolacyjności przy f_c |
| Hollow Door | 8–10 | — | ~20 dB | z typowymi nieszczelnościami |
| Solid Wood Door | ~25 | ~0,5–1 kHz | ~30 dB | |
| Drywall (stud wall) | ~20 (2×12,5 mm) | ~2,5–3 kHz | ~35–42 dB | przegroda podwójna, f0 ≈ 60–100 Hz |
| Brick 12 cm | ~220 | ~150–250 Hz | ~45 dB | |
| Concrete 20 cm | ~460 | ~90–100 Hz | ~57 dB | słychać głównie bas |
| Car Body | — | — | ~25–35 dB | bas przechodzi, rezonanse kabiny |

## 7. Auto Gain (MUST, domyślnie OFF)

1. Kompensuje szerokopasmową zmianę głośności wprowadzoną przez wtyczkę (odległość, przeszkoda, pomieszczenie, prawo panoramy, szerokość), tak aby głośność wyjścia ≈ głośność wejścia (ważenie K wg ITU-R BS.1770) — zmieniają się tylko barwa i przestrzeń.
2. Deterministyczny i wyliczany z modelu (np. energia odpowiedzi całego toru w pasmach tercjowych dla widma szumu różowego z ważeniem K), a nie z bieżącej analizy sygnału → brak pompowania, powtarzalny render, zgodność z automatyką. Zmiany wygładzane (ok. 50 ms).
3. `Auto Gain Range` — limit kompensacji 0–36 dB (domyślnie 24 dB); bieżąca wartość kompensacji widoczna w UI.
4. COULD: tryb adaptacyjny mierzący sygnał (głośność momentary z bramkowaniem).

## 8. Interfejs (MUST)

1. **Styl:** minimalistyczny i elegancki; ciemny motyw (praca w zaciemnionej reżyserce), jeden kolor akcentu, nowoczesny bezszeryfowy font osadzony w binarce (np. Inter, licencja OFL), kontrast tekstu ≥ 4,5:1; bez skeuomorfizmu i ozdobników.
2. **Widok sceny z góry** (element centralny): słuchacz/mikrofon; źródło jako przeciągany punkt (kąt = `Pan`, promień = `Distance` w skali logarytmicznej z okręgami 1/3/10/30/100/300/1000 m); przeszkoda narysowana jako ściana między nimi (kolor/faktura wg materiału, grubość wg `Thickness`); szerokość źródła jako łuk. Przeciąganie punktu zapisuje automatykę `Pan` + `Distance` (gesty begin/end change).
3. **Panel główny** (≤ ok. 8 kontrolek): Distance, Pan, Width, Room (typ + poziom), Obstacle (typ, Material/Size, Amount), Output (±24 dB). Reszta w zwijanym panelu **Advanced**: Recorded Distance, Rolloff, Temperature, Humidity, Time Pan, Pan Law, Mic Pattern, Obstacle Size, Thickness, Leak, Room Level, Decay, Bass Mono, Propagation Delay, Auto Gain Range.
4. **Zawsze widoczne przełączniki:** `Input` (Auto/Mono/Stereo), `Render Mode` (Speakers/Headphones), `Mono-safe`, `Auto Gain` (domyślnie OFF).
5. **Mierniki:** wyjście (peak + RMS), korelometr, wskaźnik kompensacji Auto Gain.
6. **Skalowanie** 50–200 %: przeciąganie narożnika + menu (75/100/125/150/200 %), stałe proporcje, grafika wektorowa (ostra na HiDPI/Retina), rozmiar zapamiętywany w stanie wtyczki; rozmiar bazowy ok. 900×560 px.
7. **Ergonomia:** dwuklik = wartość domyślna, Ctrl/Cmd + przeciąganie = precyzja, kółko myszy, wpisywanie wartości z klawiatury, podpowiedzi z jednostkami. SHOULD: Undo/Redo, porównanie A/B.
8. Teksty UI i nazwy parametrów po angielsku (standard branżowy), z jednostkami (m, dB, %, Hz, °C).

## 9. Presety (MUST)

1. Presety fabryczne w menu wtyczki i jako programy hosta. Presety użytkownika: zapis, wczytywanie, usuwanie — w udokumentowanym folderze, w wersjonowanym formacie XML/JSON (hostowe `.vstpreset` działają niezależnie).
2. Wszystkie presety fabryczne mają Auto Gain OFF i Output 0 dB. Proponowana lista (wartości do dopracowania odsłuchowego):

| Preset | Kluczowe ustawienia |
|---|---|
| Init (Neutral) | wartości domyślne |
| Dialogue – Close-Up | Recorded 0,5 m → 0,7 m, Room Small (niski Room Level) |
| Dialogue – Medium Shot | Recorded 0,5 m → 2,5 m, Room Small |
| Dialogue – Wide Shot | Recorded 0,5 m → 8 m, Room Medium |
| Across the Room | 5 m, Pan −30, Room Medium |
| Off-Screen Left / Off-Screen Right | Pan −85 / +85, 4 m, Room Small |
| Across the Street | 25 m, Room Outdoor |
| Far Away | 150 m, Room Outdoor |
| Behind Closed Door | Occlusion: Solid Wood Door, Leak 10 %, 3 m, Room Small |
| Door Ajar | Occlusion: Solid Wood Door, Leak 60 %, 3 m, Room Small |
| Next Room (Drywall) | Occlusion: Drywall, 4 m, Room Medium |
| Neighbours (Brick Wall) | Occlusion: Brick, 5 m, Room Medium |
| Through Concrete | Occlusion: Concrete, 8 m, Room Large |
| Through Window | Occlusion: Glass, Leak 5 %, 15 m, Room Outdoor |
| Behind Curtain | Occlusion: Heavy Curtain, 2 m, Room Small |
| Around the Corner | Obstruction, Obstacle Size 1,5 m, 8 m, Room Outdoor |
| Street from Inside a Car | Occlusion: Car Body, 6 m, Room Outdoor |
| Wide Ambience | Input Stereo, Width 150 %, Bass Mono 120 Hz |
| Narrow Ambience | Input Stereo, Width 50 % |

## 10. Wymagania techniczne (MUST)

1. C++20, **JUCE 8** + CMake. Sprawdź licencję JUCE (AGPLv3 lub komercyjna) i opisz wybór w README.
   - Formaty: **VST3** (MUST), Standalone do testów (SHOULD), AU (SHOULD, macOS), **AAX** (COULD — Pro Tools, standard w postprodukcji filmowej, obsługuje wyłącznie AAX; wymaga AAX SDK od Avid i podpisu PACE).
   - Kody: producent `[Xxxx]`, wtyczka `Ambc` (4 znaki; ze względu na AU: pierwsza litera wielka, pozostałe małe).
2. Platformy: Windows 10/11 x64, macOS 11+ jako Universal Binary (arm64 + x86_64); Linux — COULD.
3. Częstotliwości próbkowania 44,1–192 kHz; bufory 16–4096 próbek, zmienne w trakcie pracy. Charakterystyki niezależne od fs (np. filtry TPT/SVF wg Zavalishina lub biquady z pre-warpingiem).
4. Real-time safety w `processBlock`: żadnych alokacji, blokad, I/O ani logowania; ochrona przed denormalami; brak NaN/Inf przy skrajnych wartościach parametrów.
5. Architektura: DSP w osobnych klasach, testowalnych bez GUI (`DistanceModel`, `AirAbsorption`, `Panner`, `WidthProcessor`, `Obstacle`, `RoomModel`, `AutoGain`). Panner jako wymienny moduł, aby później dodać formaty wielokanałowe (5.1/7.1, np. VBAP — Pulkki 1997). Parametry przez `AudioProcessorValueTreeState` (lub równoważnie) ze stałymi ID.
6. Bypass zgodny z VST3 (parametr bypass hosta), bez trzasków.
7. Proponowany tor sygnału (uzasadnij lub popraw w `DESIGN.md`):
   wejście → tryb `Input` → kompensacja efektu zbliżeniowego → [opóźnienie propagacji] → rozgałęzienie:
   - **ścieżka bezpośrednia:** poziom wg odległości + pochłanianie powietrza → Obstruction → Width → Panner,
   - **ścieżka pomieszczenia** (sygnał sprzed tłumienia odległościowego, bo pole pogłosowe ma stały poziom): wczesne odbicia + FDN, poziom wg DRR,

   → suma → Occlusion → Auto Gain → Output.
8. Kod i komentarze po angielsku; README i instrukcja użytkownika po polsku (SHOULD: również po angielsku).

## 11. Testy i kryteria akceptacji (MUST)

Automatyczne testy (Catch2 lub JUCE UnitTest), uruchamiane w CI:

1. **Odległość:** podwojenie `Distance` → −6,0 ± 0,1 dB (`Rolloff` 6, powietrze i pomieszczenie wyłączone).
2. **Powietrze:** współczynnik α wg ISO 9613-1 przy 10 °C, 70 % RH, 101,325 kPa: 3,7 / 9,7 / 32,8 / 117 dB/km dla 1000 / 1995 / 3981 / 7943 Hz (±2 %; zgodność z tabelą w ISO 9613-2).
3. **Headphones, `Pan` = −100:** opóźnienie międzykanałowe z korelacji wzajemnej (pasmo < 1,5 kHz) = 0,66 ± 0,02 ms; ILD ≈ 0 dB (±1,5 dB) przy 200 Hz i ≥ 12 dB powyżej 4 kHz.
4. **Prawo panoramy −3 dB:** L² + R² stałe (±0,1 dB) w całym zakresie `Pan` przy `Time Pan` = 0.
5. **Mono-safe ON:** dla dowolnych `Pan` / `Time Pan` / `Width` odchyłka sumy L+R względem tego samego ustawienia z `Time Pan` = 0 wynosi ≤ 3 dB w każdym paśmie tercjowym (szum różowy).
6. **Width (Speakers):** suma L+R nie zależy od `Width` przy dowolnym `Pan` (błąd ≤ −100 dB); `Width` 0 % przy `Pan` = 0 → L = R.
7. **Occlusion:** w zakresie prawa masy (między f0 a f_c) nachylenie R(f) ≈ +6 dB/oktawę (±1,5 dB); `Leak` podnosi poziom wysokich częstotliwości; `Amount` 0 % = brak przeszkody (null test).
8. **Obstruction:** tłumienie zgodne ze wzorem Maekawy (±1,5 dB) w pasmach 125 Hz–8 kHz.
9. **Automatyka:** szybkie sweepy `Pan` / `Distance` / `Amount` i zmiana materiału na sinusie 1 kHz → brak nieciągłości (skok między próbkami poniżej progu), brak NaN/Inf/denormali.
10. **Niezależność od fs:** charakterystyki przy 44,1 / 48 / 96 / 192 kHz zgodne ±0,5 dB do 16 kHz.
11. **Niezależność od bufora** (SHOULD): przy stałych parametrach render z buforem 32 i 1024 próbek różni się o ≤ −120 dBFS.
12. **Stan i presety:** zapis/odczyt bez utraty danych; migracja starszej wersji stanu.
13. **Neutralność i latencja:** warunki z punktów 1.1 i 1.7.

Dodatkowo:

- **pluginval** (strictness 10) oraz walidator VST3 Steinberga — bez błędów.
- **Narzędzie CLI do renderu offline** (WAV → WAV z presetem/parametrami) i skrypt generujący sygnały testowe (sweep, szum różowy, impulsy; nagrania mowy wyłącznie na licencji CC0) oraz wykresy odpowiedzi — agent nie słyszy, więc weryfikuje pomiarami.
- **Benchmark CPU** (punkt 1.4).
- **CI (GitHub Actions):** build Windows + macOS (+ Linux), testy, pluginval, artefakty `.vst3`.

**Scenariusze odsłuchowe** (ocenia człowiek; opisz je w instrukcji):

- dialog nagrany z 0,5 m przesunięty na 8 m w Room Medium brzmi jak z planu ogólnego w tym samym wnętrzu, a nie jak „ściszony i zmatowiony";
- muzyka zza ściany z cegły: słychać głównie bas i stopę, bez sybilantów i blach;
- automatyka `Amount` 100 → 0 % („otwierają się drzwi") przebiega płynnie, bez trzasków;
- przejazd samochodu (automatyka Distance + Pan, Propagation Delay ON) daje naturalny efekt Dopplera;
- po zsumowaniu do mono obraz nie traci wyraźnie barwy (Mono-safe ON).

## 12. Sposób pracy

1. Zacznij od `docs/DESIGN.md`: schemat toru sygnału, wybrane modele i wzory z odnośnikami, tabela parametrów (ID, nazwa, zakres, wartość domyślna, jednostka, skala, czas wygładzania), założenia i ryzyka. Jeśli możesz — przedstaw go do akceptacji przed implementacją; jeśli pracujesz autonomicznie — zapisz założenia i kontynuuj.
2. Pracuj etapami; po każdym: build, zielone testy, commit.
   M1 szkielet (CMake, JUCE, CI, parametry, stan, bypass) → M2 odległość + powietrze → M3 panner (Speakers/Headphones, Mono-safe) → M4 mono/stereo + Width → M5 przeszkody → M6 pomieszczenie (ER + FDN) → M7 Auto Gain → M8 UI + skalowanie → M9 presety → M10 walidacja, benchmark, dokumentacja.
3. Żadnych atrap: bez pustych funkcji, „TODO" i zmyślonych danych w oddanym kodzie.
4. Na koniec raport: co zrobiono, wyniki testów i benchmarku, znane ograniczenia, propozycje dalszych kroków.

## 13. Poza zakresem v1

Formaty wielokanałowe (5.1/7.1/Atmos — ale architektura ma na nie pozwalać), personalizowane HRTF, edytor trajektorii ruchu we wtyczce (ruch realizuje automatyka hosta), zabezpieczenia licencyjne i funkcje sieciowe.

## 14. Literatura (punkt wyjścia)

- Blauert J. (1997). *Spatial Hearing: The Psychophysics of Human Sound Localization*. MIT Press.
- Rayleigh (1907). On our perception of sound direction. *Philosophical Magazine* 13, 214–232.
- Woodworth R. S. (1938). *Experimental Psychology*. Holt.
- Brown C. P., Duda R. O. (1998). A structural model for binaural sound synthesis. *IEEE Transactions on Speech and Audio Processing* 6(5), 476–488.
- Duda R. O., Martens W. L. (1998). Range dependence of the response of a spherical head model. *JASA* 104(5), 3048–3058.
- Brungart D. S., Rabinowitz W. M. (1999). Auditory localization of nearby sources. Head-related transfer functions. *JASA* 106(3), 1465–1479.
- Zahorik P. (2002). Assessing auditory distance perception using virtual acoustics. *JASA* 111(4), 1832–1846.
- Zahorik P., Brungart D. S., Bronkhorst A. W. (2005). Auditory distance perception in humans: a summary of past and present research. *Acta Acustica united with Acustica* 91, 409–420.
- Bronkhorst A. W., Houtgast T. (1999). Auditory distance perception in rooms. *Nature* 397, 517–520.
- ISO 9613-1:1993. Acoustics — Attenuation of sound during propagation outdoors — Part 1: Calculation of the absorption of sound by the atmosphere.
- Allen J. B., Berkley D. A. (1979). Image method for efficiently simulating small-room acoustics. *JASA* 65(4), 943–950.
- Jot J.-M., Chaigne A. (1991). Digital delay networks for designing artificial reverberators. 90th AES Convention, Paryż.
- Kuttruff H. (2016). *Room Acoustics* (wyd. 6). CRC Press.
- Kendall G. S. (1995). The decorrelation of audio signals and its impact on spatial imagery. *Computer Music Journal* 19(4), 71–87.
- Maekawa Z. (1968). Noise reduction by screens. *Applied Acoustics* 1(3), 157–173.
- Hopkins C. (2007). *Sound Insulation*. Butterworth-Heinemann.
- Fahy F., Gardonio P. (2007). *Sound and Structural Vibration* (wyd. 2). Academic Press.
- EN ISO 12354-1:2017. Building acoustics — Estimation of acoustic performance of buildings from the performance of elements — Part 1.
- ITU-R BS.1770 (aktualna wersja). Algorithms to measure audio programme loudness and true-peak audio level.
- Pulkki V. (1997). Virtual sound source positioning using vector base amplitude panning. *JAES* 45(6), 456–466.
- Zavalishin V. *The Art of VA Filter Design*. Native Instruments.
