# SAMS AMBIENCE CONTROL @VERSION@

Wtyczka do miksu filmowego: **odległość**, **kierunek lewo–prawo z różnicami czasu i fazy**, **szerokość** oraz **przeszkody** (ściana, drzwi, szyba, zasłona, róg budynku).

![SAMS AMBIENCE CONTROL](https://raw.githubusercontent.com/@REPO@/@TAG@/docs/img/ui.png)

## ⬇️ Co pobrać

| Twój komputer | Plik do pobrania | Co instaluje |
|---|---|---|
| 🪟 **Windows 10 / 11** (64-bit) | **[SAMS-Ambience-Control-@VERSION@-Windows-Setup.exe](https://github.com/@REPO@/releases/download/@TAG@/SAMS-Ambience-Control-@VERSION@-Windows-Setup.exe)** | VST3 + (opcjonalnie) aplikacja Standalone |
| 🍎 **macOS 11 lub nowszy** (Apple Silicon i Intel) | **[SAMS-Ambience-Control-@VERSION@-macOS.pkg](https://github.com/@REPO@/releases/download/@TAG@/SAMS-Ambience-Control-@VERSION@-macOS.pkg)** | VST3 + AU + (opcjonalnie) aplikacja Standalone |

Pobierz **tylko jeden plik** — ten dla swojego systemu. Pliki *Source code (zip / tar.gz)* niżej na liście są potrzebne tylko programistom.

**Który format w moim programie?** Większość programów (Nuendo, Cubase, Reaper, DaVinci Resolve, Premiere Pro, Studio One, Ableton, FL Studio) używa **VST3**. Logic Pro, Final Cut Pro i GarageBand używają **AU**. Pro Tools (AAX) nie jest jeszcze obsługiwany.

---

## 🪟 Instalacja na Windows

1. Pobierz `SAMS-Ambience-Control-@VERSION@-Windows-Setup.exe` i uruchom go dwukrotnym kliknięciem.
2. Jeśli pojawi się niebieskie okno **„System Windows ochronił ten komputer”**: kliknij **Więcej informacji**, a potem **Uruchom mimo to**. *(Dlaczego? Patrz „Bezpieczeństwo” niżej.)*
3. Wybierz, czy instalować **dla wszystkich użytkowników** (zalecane), czy **tylko dla siebie** (nie wymaga uprawnień administratora).
4. Wybierz język i kliknij **Dalej**.
5. **Folder wtyczki** — domyślnie jest to standardowy folder VST3:
   - dla wszystkich: `C:\Program Files\Common Files\VST3`,
   - tylko dla siebie: `%LOCALAPPDATA%\Programs\Common\VST3`.

   Możesz go zmienić przyciskiem **Przeglądaj…**. Jeśli wybierzesz własny folder, dodaj go w ustawieniach wtyczek swojego programu.
6. Zaznacz składniki (wtyczka VST3 jest zawsze; aplikacja Standalone jest opcjonalna) i kliknij **Zainstaluj**.
7. Uruchom ponownie swój program lub przeskanuj wtyczki. SAMS AMBIENCE CONTROL znajdziesz w kategorii *Spatial* / *Fx*.

Deinstalacja: **Ustawienia → Aplikacje → SAMS AMBIENCE CONTROL → Odinstaluj**.

## 🍎 Instalacja na macOS

1. Pobierz `SAMS-Ambience-Control-@VERSION@-macOS.pkg` i kliknij go dwukrotnie.
2. Jeśli macOS pokaże komunikat, że **nie może zweryfikować pakietu**: kliknij **Gotowe** (albo **OK**), otwórz **Ustawienia systemowe → Prywatność i ochrona**, przewiń w dół i przy tym pakiecie kliknij **Otwórz mimo to**. Potwierdź hasłem. *(Na macOS 14 i starszych wystarczy też kliknąć plik prawym przyciskiem → **Otwórz** → **Otwórz**.)*
3. Przejdź przez okno powitalne (**Kontynuuj**).
4. **Miejsce docelowe** — wybierz jedną z opcji:
   - **Zainstaluj dla wszystkich użytkowników** (zalecane): `/Library/Audio/Plug-Ins/VST3` i `/Library/Audio/Plug-Ins/Components`;
   - **Zainstaluj tylko dla mnie**: `~/Library/Audio/Plug-Ins/…`.
5. Opcjonalnie kliknij **Dostosuj**, żeby wybrać formaty: VST3, AU, Standalone (domyślnie VST3 + AU).
6. Kliknij **Instaluj** i podaj hasło.
7. Uruchom ponownie swój program. W Logic Pro, jeśli wtyczka nie pojawi się od razu: **Ustawienia → Menedżer wtyczek → Wyzeruj i przeskanuj wybrane**.

Deinstalacja: usuń `SAMS AMBIENCE CONTROL.vst3` i `SAMS AMBIENCE CONTROL.component` z folderów podanych w punkcie 4 (oraz `SAMS AMBIENCE CONTROL.app` z Aplikacji, jeśli była instalowana).

---

## 🔒 Bezpieczeństwo — skąd ostrzeżenia?

Ta wersja **nie ma jeszcze płatnego podpisu cyfrowego wydawcy** (Apple Developer ID / certyfikat Microsoft Authenticode). Dlatego Windows i macOS pokazują ostrzeżenie przy pierwszym uruchomieniu instalatora. To standardowy komunikat dla programów spoza sklepów z aplikacjami i nie oznacza wykrycia zagrożenia.

- Instalatory są budowane automatycznie przez GitHub Actions bezpośrednio z publicznego kodu źródłowego tego repozytorium. Log budowania jest dostępny w zakładce *Actions*.
- Sumę kontrolną pobranego pliku możesz porównać z wartością SHA-256 widoczną przy pliku na tej stronie.
- Kolejne wydania będą podpisane i notaryzowane, gdy repozytorium otrzyma certyfikaty. Wtedy ostrzeżenia znikną.

> Aktualizacja z wcześniejszej wersji: po prostu zainstaluj nową wersję — zastąpi poprzednią, a zapisane projekty otwierają się bez zmian.
>
> Aktualizacja z wersji 1.0.0 („Ambience”): instalator sam usuwa starą wtyczkę `Ambience`, a zapisane projekty i presety otwierają się bez zmian (pomieszczenia są automatycznie przypisane do wariantów „A”).

## ✨ Co potrafi wersja @VERSION@

- **Odległość:** poziom względem odległości nagrania, pochłanianie powietrza (ISO 9613-1), usuwanie efektu zbliżeniowego mikrofonu, opcjonalny Doppler.
- **16 przestrzeni:** OPEN A/B/C (pole, ulica, podwórko), SMALL, MEDIUM, LARGE i HALL w wariantach **A** (neutralny), **B** (wytłumiony) i **C** (żywy, twarde powierzchnie) — z wczesnymi odbiciami i pogłosem zależnymi od pozycji źródła.
- 🔧 **1.3.2:** poprawiony **Auto Gain** przy włączonym pomieszczeniu (wcześniej wynik był o 4–10 dB za cichy, teraz ±0,5 dB), pogłos bez metalicznego dzwonienia (subtelna modulacja jak w Lexiconie), dłuższy pogłos basu w dużych salach i kościele (bass ratio), szybsze ładowanie przy 96/192 kHz.
- 🔧 **Nowy silnik pomieszczeń (1.3.1):** ogon pogłosu z 16 linii z pełnym mieszaniem, dyfuzją i realnym pochłanianiem powietrza — koniec z „syczącą” górą (np. kościół: pogłos przy 8 kHz 1,3 s zamiast 3,3 s), gęsty ogon bez pojedynczych ech, odbicia rozproszone zamiast „klaskania”. Usunięty efekt phasera: od odbicia od gruntu, od źródeł „przyklejonych” do ściany i przy poszerzaniu mono powyżej 100 %. Pogłos jest średnio o 1–2 dB cichszy (głównie mniej syczenia) — w razie potrzeby podnieś *Room Level*.
- 🆕 **Mono → Stereo** (karta *Output*) — zamienia sygnał mono na naturalne stereo, w pełni zgodne z mono: suma L+R to oryginał, oba kanały mają tę samą barwę, bas zostaje w środku. Działa na ścieżkach mono, przy *Input = Mono* i na klipach dual-mono na ścieżkach stereo. Siłę ustawia *Width*. Plus 3 presety *Mono to Stereo*.
- **Mono Depth** — oddalanie mono dialogu z planu w mono: wyjście L = R, tylko głębia (odległość, powietrze, prawdziwy pogłos mono, przeszkoda), plus 3 presety *Mono Dialogue*.
- **Equalized Ambience** (domyślnie włączone) — automatyczny EQ z odległości i położenia; po wyłączeniu barwa zostaje jak w nagraniu.
- **Przycisk ON/OFF** (bypass bez trzasków) i **sprawdzanie aktualizacji** w ustawieniach (ikona koła zębatego).
- **Panorama:** *Speakers* (poziom + czas, tryb **Mono-safe**) lub *Headphones* (binauralny model głowy).
- **Mono / Stereo:** przełącznik Auto / Mono / Stereo; szerokość 0–200 % zgodna z mono; Bass Mono.
- **Przeszkody:** *Obstruction* (obiekt w pomieszczeniu) i *Occlusion* (zasłona, szyba, drzwi płytowe i pełne, ściana G-K, cegła, beton, karoseria), szczeliny, płynne „otwieranie drzwi”.
- **Auto Gain** (domyślnie wyłączony), **26 presetów filmowych**, presety użytkownika, A/B, Undo/Redo, skalowany interfejs 50–200 %.
- Zero latencji, płynna automatyka, zweryfikowane przez pluginval (poziom 10) i walidator VST3 Steinberga.

📖 Instrukcja obsługi: [README](https://github.com/@REPO@/blob/@TAG@/README.md#instrukcja-obsługi) · Opis techniczny: [DESIGN.md](https://github.com/@REPO@/blob/@TAG@/docs/DESIGN.md)

---

<details>
<summary><b>English — quick install</b></summary>

**Download one file:** Windows → `SAMS-Ambience-Control-@VERSION@-Windows-Setup.exe`, macOS → `SAMS-Ambience-Control-@VERSION@-macOS.pkg`.

- **Windows:** run the installer. On *"Windows protected your PC"* click *More info → Run anyway*. Choose all users or just you, keep the default VST3 folder or pick another one, then click *Install*.
- **macOS:** open the `.pkg`. If macOS says it cannot verify it, go to *System Settings → Privacy & Security → Open Anyway*. On *Destination Select* choose *all users* or *only me*, pick formats under *Customize*, then click *Install*.
- Rescan plug-ins in your DAW. This release is not yet code-signed, which is why the operating system shows a warning; the installers are built by GitHub Actions from this repository's source.

</details>
