# Projekt Systemy Operacyjne - Bar mleczny
**Autor:** Natalia Pliś 
**Nr indeksu:** 155212 
**Środowisko:** Linux (Torus)
**Architektura:** x86_64
**Kompilator:** gcc 8.5.0 (C11), linkowanie z -pthread
Repozytorium: https://github.com/NataliaP5/Projekt_Bar_Mleczny

# 1.  Opis projektu
Projekt polega na zbudowaniu symulacji pracy baru mlecznego w środowisku wieloprocesowym.
Poszczególne role w systemie - klient (grupa znajomych), kasjer, pracownik obsługi oraz kierownik - są odwzorowane jako oddzielne procesy, które współdziałają i wzajemnie się synchronizują.
Symulacja odwzorowuje typowe sytuacje w barze: losowy napływ klientów, składanie zamówień, zajmowanie stolików, obsługę płatności oraz reakcje na decyzje kierownika, który może w trakcie działania zmieniać liczbę dostępnych miejsc lub zainicjować ewakuację.

# 2. Założenia i reguły symulacji
W lokalu znajdują się stoliki:
X1 - 1-osobowe,
X2 - 2-osobowe,
X3 - 3-osobowe,
X4 - 4-osobowe.

Maksymalna liczba osób jedzących równocześnie na sali wynika z sumy:
N = X1·1 + X2·2 + X3·3 + X4·4

Klienci przychodzą pojedynczo lub w grupach 2- i 3-osobowych (jeden proces = jedna grupa).
Około 5% klientów nie składa zamówienia - takie osoby nie mają prawa zajmować stolika. 
Pozostali:
- składają zamówienie,
- płacą w kasie,
- odbierają gorące danie od pracownika,
   dopiero wtedy mogą usiąść przy stoliku.

Niedopuszczalne jest, żeby ktoś z gotowym posiłkiem czekał na miejsce siedzące.

Przy jednym stoliku może siedzieć jedna grupa lub kilka grup, ale jedynie wtedy, gdy są równoliczne.

Po zakończeniu jedzenia naczynia zwracane są albo przez każdego klienta z osobna, albo przez jedną osobę w imieniu całej grupy.

# 3. Role procesów

**Klient (grupa znajomych)**
Pojawia się po losowym czasie. Każdy proces klienta reprezentuje jedną grupę (1-3 osoby). Klient losowo decyduje, czy składa zamówienie (ok. 5% przypadków - brak zamówienia i natychmiastowe wyjście). Jeśli składa zamówienie, działa zgodnie z zasadą „nie czekamy z gorącym daniem”: najpierw próbuje zarezerwować miejsce/stolik (stan pending), a dopiero po udanej rezerwacji przechodzi przez kasę (żądanie płatności) i następnie wysyła prośbę o wydanie dania do pracownika. Po odebraniu posiłku aktywuje zajęcie miejsca (seated). Po posiłku kończy „jedzenie”, zwraca naczynia (indywidualnie lub zbiorczo) i opuszcza lokal, zwalniając zajęte miejsca.

**Kasjer**
Odpowiada za obsługę płatności i potwierdzanie opłacenia zamówienia. Odbiera z kolejki komunikatów żądania płatności od klientów, realizuje płatność (nalicza wartość) i odsyła odpowiedź. Dzięki temu klient nie może przejść do etapu wydania posiłku bez pozytywnego potwierdzenia z kasy.

**Pracownik obsługi (worker)**
Odpowiada za wydawanie posiłków po potwierdzeniu płatności oraz za logikę dotyczącą sali: przydział stolików, kontrolę pojemności oraz regułę współdzielenia stolika tylko przez równoliczne grupy. Operuje na stanie w pamięci dzielonej (zajętość stolików, rezerwacje/pending, liczniki), a dostęp do sekcji krytycznych jest synchronizowany semaforami. Worker obsługuje również polecenia kierownika: jednorazowe zwiększenie liczby stolików 3-osobowych (SIGUSR1) oraz rezerwacje miejsc (SIGUSR2 - handshake z managerem i stopniowe "dociąganie" rezerwacji, gdy zwalniają się miejsca).

**Kierownik baru (manager)**
Inicjalizuje i usuwa zasoby IPC (pamięć dzielona, semafory, kolejka komunikatów), uruchamia procesy kasjera i workera oraz generuje procesy klientów. Nadzoruje przebieg symulacji (cyklicznie raportuje stan: liczba stolików, miejsca zajęte i pending, liczba miejsc zarezerwowanych, liczba naczyń, przychód). Steruje zachowaniem systemu sygnałami.

# 4. Sygnały sterujące
Zaimplementowano zachowania sterowane sygnałami zgodnie z opisem tematu:

Sygnał 1: jednorazowo możliwe jest dwukrotne zwiększenie liczby stolików 3-osobowych (X3) - operacja może zajść tylko raz.

Sygnał 2: kierownik żąda rezerwacji określonej liczby miejsc/stolików, które stają się niedostępne dla klientów; liczba rezerwowanych miejsc/stolików jest ustalana w komunikacji pracownik–kierownik.

Sygnał 3 (pożar): klienci natychmiast opuszczają lokal, zostawiając naczynia; następnie po wyjściu klientów pracę kończy obsługa i zamykana jest kasa.

# 5. IPC i wymagania formalne
W projekcie zastosowano mechanizmy systemowe do komunikacji i synchronizacji procesów (co najmniej dwa różne mechanizmy IPC oraz semafory do ochrony sekcji krytycznych), zgodnie z wymaganiami.

Dodatkowo spełniono wymagania formalne:
- ustawienie minimalnych praw dostępu do struktur IPC, 
- usuwanie struktur IPC po zakończeniu działania, 
- obsługa błędów funkcji systemowych (perror() + errno), 
- unikanie rozwiązań scentralizowanych oraz obowiązkowe użycie fork() i exec(). 

# 6. Raportowanie przebiegu symulacji
Przebieg symulacji jest zapisywany do plików tekstowych (chronologiczny zapis zdarzeń: przyjście klienta, zamówienie, płatność, zajęcie/zwolnienie stolika, wyjście; dodatkowo obsługa sygnałów i ewakuacji).

# 7. Uruchomienie

Budowanie:
- make

Czyszczenie i pełna przebudowa (opcjonalnie):
- make clean && make
  
Przykładowe uruchomienie (z Makefile):
- make run
albo bezpośrednio:
./bin/manager 2 2 2 1 200 4 60 200

Program przyjmuje argumenty: X1 X2 X3 X4 CLIENTS RESERVESEATS ARR_MIN_MS ARR_MAX_MS [SEED]

Opis argumentów:
- X1 X2 X3 X4 - liczby stolików 1/2/3/4-osobowych (wymagane, zakres 0..100)
- CLIENTS - liczba procesów klientów (opcjonalne, domyślnie 120)
  - CLIENTS=0 - tryb ciągły (program działa aż do przerwania)
  - program ogranicza do 10000
- RESERVESEATS - liczba miejsc do rezerwacji po sygnale SIGUSR2 (opcjonalne, domyślnie -1, zakres -1..2000)
   - gdy RESERVESEATS > 0 - po SIGUSR2 rezerwuje automatycznie tyle miejsc
   - gdy RESERVESEATS <= 0 - po SIGUSR2 manager pyta w terminalu o liczbę miejsc (0..2000)
- ARR_MIN_MS - minimalny czas między pojawianiem się klientów w ms (opcjonalne, 0..60000), domyślnie 60
- ARR_MAX_MS - maksymalny czas między pojawianiem się klientów w ms (opcjonalne, 0..60000), domyślnie 200
- SEED - ziarno losowości (opcjonalne, 0..2147483647) - jeśli nie podano, seed jest ustawiany automatycznie na podstawie czasu i PID

Sterowanie w trakcie działania:
- Ctrl+C (SIGINT) - normalne zamykanie (program kończy w kontrolowany sposób)
- SIGTERM - tryb „pożar” (natychmiastowa ewakuacja)
- SIGUSR1 - jednorazowe podwojenie liczby stolików 3-os.
- SIGUSR2 - rezerwacja miejsc (liczba wg RESERVESEATS albo pytanie w terminalu)

# 8. Pseudokody:

**8.1 Szukanie stolika + rezerwacja „pending”:**
```text
FUNKCJA: SZUKAJ_I_REZERWUJ_STOLIK(group_size) -> foundTable albo -1

[ZABLOKUJ mutex stanu sali]

JEŻELI lokal jest zamykany LUB trwa pożar:
    [ODBLOKUJ mutex]
    ZWRÓĆ -1

bestTable = -1
bestWaste = bardzo_dużo

DLA KAŻDEGO stolika t:
    // 1) stolik musi mieć pojemność
    JEŻELI t.pojemnosc == 0:
        POMIŃ

    // 2) zasada równolicznych grup:
    // jeśli przy stoliku „ustalono” rozmiar grupy, nowa grupa musi być taka sama
    JEŻELI t.dozwolonyRozmiarGrupy != 0 ORAZ t.dozwolonyRozmiarGrupy != group_size:
        POMIŃ

    // 3) oblicz wolne miejsca
    wolne = t.pojemnosc - (t.zajete + t.pending + t.zarezerwowanePrzezManagera)
    JEŻELI wolne < group_size:
        POMIŃ

    // 4) wybór „najlepszego” stolika: minimalizujemy „marnowanie” miejsc
    waste = wolne - group_size
    JEŻELI waste < bestWaste:
        bestWaste = waste
        bestTable = t.ID

        JEŻELI waste == 0:
            PRZERWIJ (idealne dopasowanie)

JEŻELI bestTable != -1:
    // rezerwacja „pending” = miejsce jest „zaklepane”, ale grupa jeszcze nie siedzi
    t(bestTable).pending += group_size

    JEŻELI t(bestTable).dozwolonyRozmiarGrupy == 0:
        t(bestTable).dozwolonyRozmiarGrupy = group_size

[ODBLOKUJ mutex]
ZWRÓĆ bestTable
```
**8.2 Klient - logika „pending → pay → serve → seated”:**
```text
START KLIENTA:
  Wylosuj czas przyjścia
  Poczekaj ten czas

  group_size = 1..3 (zależnie od ID klienta)
  JEŻELI klient należy do ~5% bez zamówienia:
      Zapisz „no order” i ZAKOŃCZ

  // KROK 1: zdobądź miejsce zanim pójdziesz po gorące danie
  deadline = teraz + 8000 ms
  foundTable = -1

  DOPÓKI foundTable == -1 ORAZ teraz < deadline:
      foundTable = SZUKAJ_I_REZERWUJ_STOLIK(group_size)

      JEŻELI foundTable == -1:
          CZEKAJ na „zdarzenie stolikowe” (ktoś zwolnił miejsce) maksymalnie do deadline

  JEŻELI foundTable == -1:
      Zapisz „left (no seat after 8000ms)” i ZAKOŃCZ

  Zapisz „reserved (pending)”
  Powiadom managera: „klient pending”

  // KROK 2: płatność
  Wyślij do kasjera prośbę o płatność i CZEKAJ na odpowiedź
  JEŻELI płatność nieudana LUB przerwana (zamknięcie/pożar):
      ANULUJ pending na stoliku
      Powiadom managera: „klient wychodzi”
      ZAKOŃCZ

  // KROK 3: wydanie dania
  Wyślij do pracownika prośbę o wydanie i CZEKAJ na odpowiedź
  JEŻELI wydanie nieudane LUB przerwane:
      ANULUJ pending na stoliku
      Powiadom managera: „klient wychodzi”
      ZAKOŃCZ

  // KROK 4: klient siada (pending -> occupied)
  [ZABLOKUJ mutex]
  t(foundTable).pending  -= group_size
  t(foundTable).zajete   += group_size
  [ODBLOKUJ mutex]

  Powiadom managera: „klient seated”
  Jedz (wątki: po 1 na osobę)

  JEŻELI pożar:
      Zapisz „left dishes”
  INACZEJ:
      Zwróć naczynia (losowo: zbiorczo albo pojedynczo)

  // KROK 5: wyjście i zwolnienie miejsc
  [ZABLOKUJ mutex]
  t(foundTable).zajete -= group_size
  JEŻELI t(foundTable).zajete == 0 ORAZ t(foundTable).pending == 0:
      t(foundTable).dozwolonyRozmiarGrupy = 0
  [ODBLOKUJ mutex]

  Wyślij „zdarzenie stolikowe” (obudź czekających)
  Powiadom managera: „klient wyszedł”
  ZAKOŃCZ
```

**8.3 Pracownik - wydanie dania + zwrot naczyń:**
```text
PĘTLA PRACOWNIKA:
  DOPÓKI pracownik nie jest zatrzymywany:
      Odbierz wiadomość

      JEŻELI to prośba o wydanie dania:
          ok = PRAWDA
          [ZABLOKUJ mutex]
          JEŻELI wskazany stolik nie istnieje: ok = FAŁSZ
          JEŻELI na stoliku nie ma pending dla tej grupy: ok = FAŁSZ
          JEŻELI pożar LUB zamykanie: ok = FAŁSZ
          [ODBLOKUJ mutex]
          Odeślij odpowiedź do klienta: ok/nie ok

      JEŻELI to zwrot naczyń:
          [ZABLOKUJ mutex]
          zwiększ licznik zwróconych naczyń o podaną liczbę
          [ODBLOKUJ mutex]
```

**8.4 SIGUSR2 - rezerwacja miejsc (ustalenie liczby + rezerwowanie w czasie):**
```text
MANAGER po SIGUSR2:
  Przekaż sygnał do pracownika

PRACOWNIK po SIGUSR2:
  Poproś managera: „ile miejsc rezerwujemy?”

MANAGER:
  Ustal liczbę miejsc:
    - jeśli podana w parametrach → użyj
    - inaczej → zapytaj w terminalu
  Zwiększ licznik „pozostało do zarezerwowania”
  Wyślij do pracownika start rezerwacji

DOPÓKI „pozostało do zarezerwowania” > 0:
  Co pewien czas wyślij do pracownika „tick”

PRACOWNIK po tick:
  [ZABLOKUJ mutex]
  Zarezerwuj tyle miejsc, ile aktualnie jest możliwe (po 1 miejscu)
  Zmniejsz „pozostało do zarezerwowania”
  [ODBLOKUJ mutex]
```

# 9. Testy

**Test 1 - Skrajne obciążenie**

Cel: sprawdzić stabilność (brak zawieszenia/deadlock), poprawne sprzątanie IPC i działanie w ekstremalnym napływie.

Parametry uruchomienia: ./bin/manager 1 0 0 0 5000 0 1 3 

Opis: Jeden stolik 1-osobowy, 5000 klientów w krótkim czasie

Przykładowy fragment `logs/client.log`:
```text
[4626155851] Client 2536 left (no seat after 8000ms)
[4626155854] Client 2537 left (no seat after 8000ms)
[4626156106] Client 2557 left (no seat after 8000ms)
[4626156107] Client 2556 left (no seat after 8000ms)
[4626156107] Client 2549 left (no seat after 8000ms)
[4626156107] Client 2547 left (no seat after 8000ms)
[4626156107] Client 2552 left (no seat after 8000ms)
[4626156107] Client 2548 left (no seat after 8000ms)
[4626156107] Client 2555 left (no seat after 8000ms)
[4626156107] Client 2546 left (no seat after 8000ms)
```

Wnioski: Program nie wiesza się, klienci czekają 8s, jeśli stolik się nie zwolni to wychodzą - w efekcie wielu klientów opuszcza bar mleczny nie robiąc rezerwacji miejsca i zamówienia. Po zakończeniu nie zostają zasoby IPC. TEST ZDANY

**Test 2 - Odsetek klientów bez zamówienia około 5%**

Cel: Weryfikacja, że około 5% klientów nie składa zamówienia i opuszcza lokal, nie zajmując stolika.

Parametry uruchomienia:  ./bin/manager 2 2 2 2 400 0 20 60

Przykładowy fragment `logs/client.log`:
```text
[4627678120] Client 376: no order (leaves immediately)
```
Wynik `grep -c "no order" logs/client.log`:
`19`

Wnioski: 4,75% klientów nie złożyło zamówienia i od razu opuściło lokal, nie zajmując stolika oraz nie składając zamówienia. TEST ZDANY

**Test 3 - Przydział stolików dla różnych grup**

Cel: Kontrola zasad pojemności stolików oraz łączenia tylko równolicznych grup.

Parametry uruchomienia:  ./bin/manager 0 0 0 10 1000 0 0 5

Opis: Jest 10 stolików 4-osobowych - jeśli ktoś już siedzi, kolejni mogą próbować się dosiąść. Dużo klientów i szybkie przyjścia - większa szansa, że ktoś będzie próbował.

Przykładowy fragment `logs/client.log`:
```text
[4629196531] Client 2 group=3 reserved table=1 (pending)
[4629196532] Client 2 seated table=1 group=3
[4629202534] Client 2 left table=1
[4629202534] Client 28 group=2 reserved table=1 (pending)
[4629202535] Client 28 seated table=1 group=2
[4629202557] Client 295 group=2 reserved table=1 (pending)
[4629202558] Client 295 seated table=1 group=2
[4629208560] Client 295 left table=1
```

Wnioski: Gdy przy stoliku 4-osobowym siedziała grupa 3-osobowa nikt nie mógł się dosiąść, a gdy usiadła przy nim grupa 2-osobowa to mogła się dosiąść tylko inna grupa 2-osobowa. Brak przekroczeń pojemności stolików oraz brak sytuacji mieszania nierównolicznych grup przy jednym stole. TEST ZDANY

**Test 4 - Sygnał 1: zwiększenie liczby stolików 3-osobowych**

Cel: Sprawdzenie jednorazowego „podwojenia X3” i poprawnego uwzględniania nowych stolików.

Parametry uruchomienia: ./bin/manager 1 1 2 1 400 0 50 250

Opis: W trakcie działania wysłanie sygnału 1 do kierownika oraz próba wysłania ponownie.

```text
[STATUS] tables=5 seats=13 occ=13 pend=0 res=0 reserve_remaining=0 dishes=56 revenue=1192 closing=0 fire=0
[SIGUSR1] forwarded to worker
[STATUS] tables=7 seats=19 occ=16 pend=0 res=0 reserve_remaining=0 dishes=59 revenue=1294 closing=0 fire=0
[SIGUSR1] forwarded to worker
[STATUS] tables=7 seats=19 occ=18 pend=0 res=0 reserve_remaining=0 dishes=62 revenue=1384 closing=0 fire=0
```
`logs/worker.log`:
```text
[4630579294] SIGUSR1: added 2 new 3-seat tables (once)
[4630580692] SIGUSR1: ignored (boost already used)
```
Wnioski: Po pierwszym sygnale liczba dostępnych stolików 3-os. dwukrotnie się zwiększa, kolejna próba jest ignorowana. TEST ZDANY

**Test 5 - Sygnał 2: rezerwacje miejsc/stolików**

Cel: Weryfikacja rezerwacji oraz komunikacji pracownik-kierownik dotyczącej liczby rezerwowanych miejsc.

Opis: Wysłanie sygnału 2, ustalenie liczby rezerwowanych miejsc i obserwowacja wpływu na dostępność.
- Opcja I - ilość miejsc do rezerwacji podana jako parametr uruchomienia
  Parametry uruchomienia: ./bin/manager 2 2 2 1 500 5 30 150
  ```text
  [STATUS] tables=7 seats=16 occ=16 pend=0 res=0 reserve_remaining=0 dishes=42 revenue=868 closing=0 fire=0
  [SIGUSR2] forwarded to worker (reservation handshake)
  [STATUS] tables=7 seats=16 occ=16 pend=0 res=0 reserve_remaining=5 dishes=42 revenue=868 closing=0 fire=0
  [STATUS] tables=7 seats=16 occ=14 pend=0 res=2 reserve_remaining=3 dishes=46 revenue=904 closing=0 fire=0
  [STATUS] tables=7 seats=16 occ=11 pend=0 res=4 reserve_remaining=1 dishes=50 revenue=922 closing=0 fire=0
  [STATUS] tables=7 seats=16 occ=11 pend=0 res=5 reserve_remaining=0 dishes=50 revenue=922 closing=0 fire=0
  ```
- Opcja II - ilość miejsc wybrana dopiero po wysłaniu sygnału
  Parametry uruchomienia: ./bin/manager 2 2 2 1 500 0 50 250
  ```text
  [STATUS] tables=7 seats=16 occ=16 pend=0 res=0 reserve_remaining=0 dishes=24 revenue=572 closing=0 fire=0
  [SIGUSR2] forwarded to worker (reservation handshake)

  [RESERVE] Podaj liczbe miejsc do rezerwacji (0..2000): 5
  [STATUS] tables=7 seats=16 occ=14 pend=0 res=2 reserve_remaining=3 dishes=41 revenue=790 closing=0 fire=0
  [STATUS] tables=7 seats=16 occ=12 pend=0 res=3 reserve_remaining=2 dishes=44 revenue=802 closing=0 fire=0
  [STATUS] tables=7 seats=16 occ=11 pend=0 res=5 reserve_remaining=0 dishes=46 revenue=824 closing=0 fire=0
  ```
`logs/manager.log`:
```text
[4632434327] SIGUSR2: forwarded to worker pid=4100625
[4632439292] RESERVE_ASK from worker pid=4100625 -> want=5, reserve_remaining=5
```
`logs/worker.log`:
```text
[4631654152] SIGUSR2: sent RESERVE_ASK to manager
[4631654173] RESERVE progress: requested_rem=5 got=0 left=5
```
(Logi RESERVE progress aż uda się zarezerwować żądaną ilość)

Wnioski: Zarezerwowane miejsca/stoliki nie są przydzielane zwykłym klientom oraz spada dostępna pojemność sali. Gdyby w danym momencie nie było tyle wolnych miejsc ile chce się zarezerwować, to rezerwacja oczekuje aż miejsce się zwolni i dokłada je do rezerwacji. TEST ZDANY

**Test 6 - Sygnał 3: pożar i ewakuacja**

Cel: Weryfikacja natychmiastowego opuszczenia lokalu przez klientów i poprawnego zamknięcia pracy obsługi/kasy.

Parametry uruchomienia: ./bin/manager 2 1 2 1 1000 0 80 350

Opis: W trakcie działania wysłanie sygnału 3.

```text
[STATUS] tables=6 seats=14 occ=12 pend=0 res=0 reserve_remaining=0 dishes=65 revenue=1184 closing=0 fire=0
[FIRE] evacuating clients NOW!
FINAL_STATUS (FIRE) tables=6 seats=14 occ=12 pend=0 res=0 reserve_remaining=0 dishes=65 revenue=1184 closing=1 fire=1
```
`logs/client.log`:
```text
[4632125726] Client 111: all eater threads finished
[4632125726] Client 111 FIRE: left dishes on table=0 group=1
[4632125726] Client 111 left table=0
```
`logs/cashier.log`:
```text
[4632125780] Cashier stopping.
```
`logs/worker.log`:
```text
[4632125730] Worker stopping.
```
`logs/manager.log`:
```text
[4632125830] Cleaning IPC and exiting.
```

Wnioski: Klienci kończą natychmiast i zostawiają naczynia, po wyjściu klientów kończą pracownicy obsługi i kasa. TEST ZDANY

# 10. Problemy, wnioski

- Synchronizacja pamięci dzielonej i unikanie wyścigów - aktualizacje zajętości stolików musiały być atomowe. Zastosowano semafor-mutex oraz rozdzielenie stanu na pending_seats (rezerwacja w trakcie) i occupied_seats (realnie zajęte), żeby uniknąć sytuacji, w której dwa procesy „przydzielą” to samo miejsce.
- Czekanie na miejsce bez busy-waitingu - zamiast aktywnego odpytywania, klienci oczekują na zdarzenia stolikowe na semaforze zdarzeniowym z limitem czasu (timeout). Dzięki temu program jest stabilny przy dużym obciążeniu.
- EINTR i przerwane wywołania systemowe - blokujące operacje (np. msgrcv, semop, waitpid, operacje na plikach) mogą być przerywane przez sygnały; w kodzie uwzględniono obsługę errno == EINTR, aby procesy kontynuowały pracę poprawnie.
- Koordynacja zakończenia symulacji i sprzątanie IPC - po zakończeniu (lub pożarze) trzeba było zagwarantować, że procesy potomne kończą pracę, a zasoby SysV IPC są usuwane z systemu. Zadbano o kolejność kończenia procesów oraz IPC_RMID dla zasobów.

Wnioski: Systemy wieloprocesowe z SysV IPC wymagają bardzo ostrożnej synchronizacji (mutex + jasno zdefiniowane stany pośrednie), obsługi sytuacji wyjątkowych (sygnały, EINTR) oraz konsekwentnego sprzątania zasobów. Dobre logowanie znacząco ułatwia debugowanie i weryfikację poprawności.

# 11. Elementy dodatkowe

Poniższe elementy są ponad minimum opisu tematu i służą zwiększeniu stabilności/testowalności lub czytelności działania symulacji („wyróżniające elementy”):

- Szczegółowe logowanie per-proces - osobne pliki logs/*.log dla managera/worker/cashier/klientów z timestampami i zdarzeniami (ułatwia analizę przebiegu i dowodzenie testów).

- Cykliczny status managera oraz kolorowanie wyjścia terminala - okresowo wypisywane podsumowanie stanu (tables, seats, occ, pend, res, reserve_remaining, dishes, revenue, fire) pozwala szybko ocenić, czy system się nie „zatrzymał” oraz jak sygnały wpływają na symulację.
  <img width="1166" height="300" alt="image" src="https://github.com/user-attachments/assets/713ae083-6b6f-4de7-823d-8592a431b434" />


- Reprodukowalność poprzez seed - możliwość ustawienia ziarna losowości (stabilniejsze testowanie i porównywanie wyników).

- Oczekiwanie z limitem czasu zamiast aktywnego odpytywania - klient nie „kręci się w pętli”, tylko czeka na zdarzenia z timeoutem (co ogranicza obciążenie CPU i poprawia stabilność przy dużej liczbie klientów).

- Dwa tryby rezerwacji (SIGUSR2) - rezerwacja może być zadana parametrem uruchomienia albo interaktywnie po sygnale (handshake worker-manager), co ułatwia demonstrację na prezentacji i w testach.

# 12. Permalinki do kluczowych fragmentów kodu

- `fork()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L132-L140 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L541-L566
- `execl()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L136-L137 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L563
- `waitpid()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L616-L624
- `exit()` / `_exit()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L565

- `sigaction()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L327-L340
- `kill()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L392 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L399 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L495

- `ftok()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L12-L13

- `msgget()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L40 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L75-L76
- `msgctl()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L92
- `msgrcv()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/cashier.c#L39-L46
- `msgsnd()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/cashier.c#L90-L92

- `shmget()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L30-L31 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L69-L70
- `shmat()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L48-L49 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L78-L79
- `shmdt()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L85
- `shmctl()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L36 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L95

- `semget()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L33 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L72-L73
- `semctl()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L53 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L98
- `semop()`- https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L104-L106 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L112-L114 https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L120-L123
- `semtimedop()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/ipc.c#L137-L140

- `pthread_create()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/client.c#L197-L201
- `pthread_join()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/client.c#L219-L223

- `open()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/common.c#L42
- `write()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/common.c#L61-L62
- `close()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/common.c#L69
- `unlink()` - https://github.com/NataliaP5/Projekt_Bar_Mleczny/blob/c9ddfab1baaec62b30677a3eab9614aaf29405fd/src/manager.c#L629

