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

# 2. Struktura projektu

manager.c - uruchamia całą symulację: tworzy/otwiera IPC, startuje procesy worker i cashier, generuje procesy client, wypisuje statusy, obsługuje sygnały (Ctrl+C, SIGUSR1, SIGUSR2, pożar) i sprząta IPC na końcu.

client.c - logika pojedynczej grupy klientów (1-3 osoby): rezerwuje miejsce (pending), płaci w kasie, prosi o wydanie dania, siada (occupied), „je” (wątki), zwraca naczynia i wychodzi; reaguje na pożar/zamykanie.

worker.c - obsługa „zaplecza”: obsługuje prośby klientów o wydanie dania i zwrot naczyń, realizuje logikę rezerwacji miejsc po SIGUSR2 oraz obsługuje SIGUSR1 (dodanie stolików X3).

cashier.c - obsługuje płatności: odbiera żądania płatności z kolejki IPC i odsyła klientom odpowiedź (potwierdzenie/odmowa).

ipc.c - tworzenie/otwieranie/usuwanie zasobów SysV IPC (shm, semafory, kolejki wiadomości) + funkcje operujące na stanie sali (rezerwacja pending, przejście do occupied, zwalnianie miejsc, rezerwacje stałe, budzenie klientów eventem).

ipc.h - definicje struktur IPC, typy wiadomości i deklaracje funkcji z ipc.c.

common.c - funkcje pomocnicze wspólne: logowanie do logs/*.log, pomiar czasu (now_ms), sleep, parsowanie argumentów, itp.

common.h - deklaracje narzędzi z common.c, makra typu DIE_PERROR, stałe globalne itp.

shared_state.h - definicje struktur stanu w pamięci dzielonej: SharedState (globalne liczniki/flagi) i Table (stan stolika). Wspólne dla wszystkich procesów.

Makefile - kompilacja projektu, czyszczenie, ewentualnie target run.

# 3. Założenia i reguły symulacji
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

# 4. Role procesów

**Klient (grupa znajomych)**
Pojawia się po losowym czasie. Każdy proces klienta reprezentuje jedną grupę (1-3 osoby). Klient losowo decyduje, czy składa zamówienie (ok. 5% przypadków - brak zamówienia i natychmiastowe wyjście). Jeśli składa zamówienie, działa zgodnie z zasadą „nie czekamy z gorącym daniem”: najpierw próbuje zarezerwować miejsce/stolik (stan pending), a dopiero po udanej rezerwacji przechodzi przez kasę (żądanie płatności) i następnie wysyła prośbę o wydanie dania do pracownika. Po odebraniu posiłku aktywuje zajęcie miejsca (seated). Po posiłku kończy „jedzenie”, zwraca naczynia (indywidualnie lub zbiorczo) i opuszcza lokal, zwalniając zajęte miejsca.

**Kasjer**
Odpowiada za obsługę płatności i potwierdzanie opłacenia zamówienia. Odbiera z kolejki komunikatów żądania płatności od klientów, realizuje płatność (nalicza wartość) i odsyła odpowiedź. Dzięki temu klient nie może przejść do etapu wydania posiłku bez pozytywnego potwierdzenia z kasy.

**Pracownik obsługi (worker)**
Odpowiada za wydawanie posiłków po potwierdzeniu płatności oraz za logikę dotyczącą sali: przydział stolików, kontrolę pojemności oraz regułę współdzielenia stolika tylko przez równoliczne grupy. Operuje na stanie w pamięci dzielonej (zajętość stolików, rezerwacje/pending, liczniki), a dostęp do sekcji krytycznych jest synchronizowany semaforami. Worker obsługuje również polecenia kierownika: jednorazowe zwiększenie liczby stolików 3-osobowych (SIGUSR1) oraz rezerwacje miejsc (SIGUSR2 - handshake z managerem i stopniowe "dociąganie" rezerwacji, gdy zwalniają się miejsca).

**Kierownik baru (manager)**
Inicjalizuje i usuwa zasoby IPC (pamięć dzielona, semafory, kolejka komunikatów), uruchamia procesy kasjera i workera oraz generuje procesy klientów. Nadzoruje przebieg symulacji (cyklicznie raportuje stan: liczba stolików, miejsca zajęte i pending, liczba miejsc zarezerwowanych, liczba naczyń, przychód). Steruje zachowaniem systemu sygnałami.

# 5. Sygnały sterujące
Zaimplementowano zachowania sterowane sygnałami zgodnie z opisem tematu:

Sygnał 1: jednorazowo możliwe jest dwukrotne zwiększenie liczby stolików 3-osobowych (X3) - operacja może zajść tylko raz.

Sygnał 2: kierownik żąda rezerwacji określonej liczby miejsc/stolików, które stają się niedostępne dla klientów; liczba rezerwowanych miejsc/stolików jest ustalana w komunikacji pracownik–kierownik.

Sygnał 3 (pożar): klienci natychmiast opuszczają lokal, zostawiając naczynia; następnie po wyjściu klientów pracę kończy obsługa i zamykana jest kasa.

# 6. IPC i wymagania formalne
W projekcie zastosowano mechanizmy systemowe do komunikacji i synchronizacji procesów (co najmniej dwa różne mechanizmy IPC oraz semafory do ochrony sekcji krytycznych), zgodnie z wymaganiami.

Dodatkowo spełniono wymagania formalne:
- ustawienie minimalnych praw dostępu do struktur IPC, 
- usuwanie struktur IPC po zakończeniu działania, 
- obsługa błędów funkcji systemowych (perror() + errno), 
- unikanie rozwiązań scentralizowanych oraz obowiązkowe użycie fork() i exec(). 

# 7. Raportowanie przebiegu symulacji
Przebieg symulacji jest zapisywany do plików tekstowych (chronologiczny zapis zdarzeń: przyjście klienta, zamówienie, płatność, zajęcie/zwolnienie stolika, wyjście; dodatkowo obsługa sygnałów i ewakuacji).

# 8. Uruchomienie

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

# 9. Pseudokody:

**9.1 Szukanie stolika + rezerwacja „pending”:**
```text
FUNKCJA: SZUKAJ_I_REZERWUJ_STOLIK(group_size) -> table_index albo -1

[ZABLOKUJ SEM_MUTEX]

JEŻELI closing == 1 LUB fire_alarm == 1:
    [ODBLOKUJ SEM_MUTEX]
    ZWRÓĆ -1

best = -1
bestWaste = bardzo_dużo

DLA i = 0..tables_count-1:
    t = tables[i]

    // zasada równolicznych grup
    JEŻELI t.group_size_allowed != 0 ORAZ t.group_size_allowed != group_size:
        POMIŃ

    used  = t.reserved_fixed + t.occupied_seats + t.pending_seats
    free  = t.capacity - used

    JEŻELI free < group_size:
        POMIŃ

    waste = free - group_size
    JEŻELI waste < bestWaste:
        bestWaste = waste
        best = i
        JEŻELI waste == 0:
            PRZERWIJ

JEŻELI best != -1:
    tables[best].pending_seats += group_size
    JEŻELI tables[best].group_size_allowed == 0:
        tables[best].group_size_allowed = group_size

[ODBLOKUJ SEM_MUTEX]
ZWRÓĆ best
```
**9.2 Klient - logika „pending → pay → serve → seated”:**
```text
START KLIENTA:
  group_size = 1..3 (proces = jedna grupa)

  // (opcjonalnie) ~5% klientów bez zamówienia:
  JEŻELI wylosowano "no order":
      Zapisz log i ZAKOŃCZ

  // KROK 1: zdobądź miejsce zanim pójdziesz po gorące danie
  table = -1
  DOPÓKI stop == 0:
      table = SZUKAJ_I_REZERWUJ_STOLIK(group_size)
      JEŻELI table != -1:
          pending = 1
          PRZERWIJ

      // brak miejsca -> czekaj na zdarzenie stolikowe (bez deadline)
      CZEKAJ na SEM_TABLE_EVENT (może zostać przerwane sygnałem)

  JEŻELI pending == 0 LUB stop == 1:
      JEŻELI fire_alarm == 1:
          Zapisz "evacuated before reserving seat"
      INACZEJ:
          Zapisz "stopped before reserving seat"
      ZAKOŃCZ

  Zapisz "reserved table (pending)"
  Powiadom managera: MSG_CLIENT_PENDING

  // KROK 2: płatność (kasjer)
  Wyślij MSG_PAY_REQ i CZEKAJ na MSG_PAY_REPLY
  JEŻELI stop == 1 LUB fire_alarm == 1 LUB płatność nieudana:
      ANULUJ pending: cancel_reservation(group_size, table)
      Powiadom managera: MSG_CLIENT_LEFT
      ZAKOŃCZ

  Zapisz "PAID"

  // KROK 3: wydanie dania (worker)
  Wyślij MSG_SERVE_REQ i CZEKAJ na MSG_SERVE_REPLY
  JEŻELI stop == 1 LUB fire_alarm == 1 LUB wydanie nieudane:
      ANULUJ pending: cancel_reservation(group_size, table)
      Powiadom managera: MSG_CLIENT_LEFT
      ZAKOŃCZ

  // KROK 4: klient siada (pending -> occupied)
  activate_seating(group_size, table)
  Powiadom managera: MSG_CLIENT_SEATED
  Zapisz "seated"

  // KROK 5: jedzenie (wątki)
  Jedz (wątki: po 1 na osobę)

  JEŻELI fire_alarm == 1:
      Zapisz "left dishes (fire)"
  INACZEJ:
      Wyślij zwrot naczyń do worker (zbiorczo lub pojedynczo)

  // KROK 6: wyjście
  finish_eating_and_leave(group_size, table)
  Powiadom managera: MSG_CLIENT_LEFT
  ZAKOŃCZ
```

**9.3 Pracownik - wydanie dania + zwrot naczyń:**
```text
PĘTLA WORKERA:
  DOPÓKI stop == 0:
      Odbierz wiadomość z msg_work_req

      JEŻELI MSG_SERVE_REQ od klienta:
          ok = 1
          [ZABLOKUJ SEM_MUTEX]
          JEŻELI closing == 1 LUB fire_alarm == 1:
              ok = 0
          JEŻELI wskazany stolik nie istnieje:
              ok = 0
          JEŻELI na stoliku nie ma wystarczającego pending dla tej grupy:
              ok = 0
          [ODBLOKUJ SEM_MUTEX]
          Odeślij MSG_SERVE_REPLY (ok)

      JEŻELI MSG_DISH_RETURN_REQ:
          [ZABLOKUJ SEM_MUTEX]
          dishes_returned_total += value
          [ODBLOKUJ SEM_MUTEX]

      JEŻELI obsługa SIGUSR2 / rezerwacje:
          okresowo rezerwuj miejsca (reserved_fixed) gdy są wolne
          i zmniejszaj reserve_remaining
```

**9.4 SIGUSR2 - rezerwacja miejsc (ustalenie liczby + rezerwowanie w czasie):**
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

# 10. Testy

**Test 1 - Duży napływ klientów przy minimalnej pojemności**

Cel: sprawdzić stabilność (brak zawieszenia/deadlock), poprawne sprzątanie IPC i działanie w ekstremalnym napływie.

Parametry uruchomienia: ./bin/manager 1 0 0 0 5000 0 0 1

Opis: Jest jeden stolik 1-osobowy i 5000 klientów, którzy próbują zarezerwować miejsce.

```text
[STATUS] tables=1 seats=1 occ=1 pend=0 res=0 reserve_remaining=0 cashq=0 dishes=18 revenue=290 closing=0 fire=0
[STATUS] tables=1 seats=1 occ=1 pend=0 res=0 reserve_remaining=0 cashq=0 dishes=19 revenue=308 closing=0 fire=0
[STATUS] tables=1 seats=1 occ=1 pend=0 res=0 reserve_remaining=0 cashq=0 dishes=19 revenue=308 closing=0 fire=0
```
Przykładowy fragment `logs/client.log`:
```text
[5232775161] Client 2823 group=1 reserved table=0 (pending)
[5232775162] Client 2823: selected 'Kotlet schabowy' price=22 per person (group=1 total=22)
[5232775529] Client 2823: PAID for 'Kotlet schabowy' total=22
[5232775743] Client 2823 seated table=0 group=1
[5232775743] Client 2823: spawning 1 eater threads
[5232775744] Client 2823 | person 1: eating 2 sec
[5232777745] Client 2823: all eater threads finished
[5232777868] Client 2823 finished -> returned dishes and left
[5232777998] Client 2823 left table=0
[5232777998] Client 2907 group=1 reserved table=0 (pending)
[5232777998] Client 2907: selected 'Pomidorowka' price=14 per person (group=1 total=14)
[5232778382] Client 2907: PAID for 'Pomidorowka' total=14
[5232778640] Client 2907 seated table=0 group=1
[5232778640] Client 2907: spawning 1 eater threads
[5232778640] Client 2907 | person 1: eating 4 sec
[5232782641] Client 2907: all eater threads finished
[5232782751] Client 2907 finished -> returned dishes and left
[5232782861] Client 2907 left table=0
```

Wnioski: Program nie wiesza się, obsługa klienta działa poprawnie. Po zakończeniu działania procesy i zasoby SysV IPC są poprawnie sprzątane. TEST ZDANY

**Test 2 - Duża kolejka do kasjera**

Cel: Sprawdzić zachowanie systemu przy masowym napływie klientów i dużej kolejce do kasjera (cashq), czy nie ma zacięć/deadlocków i czy kolejka stopniowo maleje.

Opis: 5000 klientów pojawia się niemal natychmiast (ARR_MIN_MS=0, ARR_MAX_MS=0). Większość szybko rezerwuje stolik (pending), a następnie ustawia się do kasy - powstaje bardzo duża kolejka, która w kolejnych statusach maleje.

Parametry uruchomienia:  ./bin/manager 1000 1000 1000 1000 5000 0 0 0

```text
LIMIT_REACHED_STATUS (STOP_SPAWNING) tables=4000 seats=10000 occ=300 pend=9122 res=0 reserve_remaining=0 cashq=4561 dishes=0 revenue=4538 closing=0 fire=0
[LIMIT] limit reached: stop spawning, continue service until all clients exit
[STATUS] tables=4000 seats=10000 occ=304 pend=9118 res=0 reserve_remaining=0 cashq=4558 dishes=0 revenue=4580 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=398 pend=9024 res=0 reserve_remaining=0 cashq=4512 dishes=1 revenue=6070 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=480 pend=8934 res=0 reserve_remaining=0 cashq=4468 dishes=14 revenue=7450 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=542 pend=8853 res=0 reserve_remaining=0 cashq=4425 dishes=46 revenue=8750 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=595 pend=8763 res=0 reserve_remaining=0 cashq=4380 dishes=95 revenue=10296 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=596 pend=8669 res=0 reserve_remaining=0 cashq=4332 dishes=188 revenue=11740 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=577 pend=8575 res=0 reserve_remaining=0 cashq=4284 dishes=301 revenue=13238 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=518 pend=8479 res=0 reserve_remaining=0 cashq=4235 dishes=456 revenue=14792 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=515 pend=8378 res=0 reserve_remaining=0 cashq=4183 dishes=560 revenue=16286 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=527 pend=8272 res=0 reserve_remaining=0 cashq=4132 dishes=654 revenue=17968 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=528 pend=8170 res=0 reserve_remaining=0 cashq=4080 dishes=755 revenue=19544 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=546 pend=8064 res=0 reserve_remaining=0 cashq=4028 dishes=843 revenue=21114 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=562 pend=7961 res=0 reserve_remaining=0 cashq=3976 dishes=930 revenue=22678 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=563 pend=7862 res=0 reserve_remaining=0 cashq=3921 dishes=1028 revenue=24248 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=576 pend=7752 res=0 reserve_remaining=0 cashq=3866 dishes=1125 revenue=26008 closing=0 fire=0
[STATUS] tables=4000 seats=10000 occ=568 pend=7648 res=0 reserve_remaining=0 cashq=3809 dishes=1237 revenue=27462 closing=0 fire=0
```

Wnioski: System działa stabilnie przy bardzo dużym obciążeniu; kolejka do kasjera jest widoczna w statusie i stopniowo maleje, a jednocześnie rosną dishes i revenue, co potwierdza ciągłą obsługę klientów. TEST ZDANY.

**Test 3 - Odsetek klientów bez zamówienia około 5%**

Cel: Weryfikacja, że około 5% klientów nie składa zamówienia i opuszcza lokal, nie zajmując stolika.

Parametry uruchomienia:  ./bin/manager 2 2 2 2 400 0 20 60

Przykładowy fragment `logs/client.log`:
```text
[4627678120] Client 376: no order (leaves immediately)
```
Wynik `grep -c "no order" logs/client.log`:
`19`

Wnioski: 4,75% klientów nie złożyło zamówienia i od razu opuściło lokal, nie zajmując stolika oraz nie składając zamówienia. TEST ZDANY

**Test 4 - Przydział stolików dla różnych grup**

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

**Test 5 - Sygnał 1: zwiększenie liczby stolików 3-osobowych**

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

**Test 6 - Sygnał 2: rezerwacje miejsc/stolików**

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

**Test 7 - Sygnał 3: pożar i ewakuacja**

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

**Test 8 - ciężkie obciążenie + wszystkie sygnały**

Cel: Sprawdzić stabilność systemu pod dużym obciążeniem oraz poprawną obsługę sygnałów w takich warunkach.

Parametry uruchomienia: ./bin/manager 500 500 500 500 8000 0 0 0

Opis: Uruchomiono symulację z 2000 stolikami (5000 miejsc) i 8000 klientów generowanych bez opóźnień. W trakcie działania wysłano SIGUSR1 (zwiększenie liczby stolików 3-os.), następnie SIGUSR2 i zarezerwowano 250 miejsc, a na końcu wywołano pożar (SIGTERM) powodujący natychmiastową ewakuację oraz zakończenie pracy systemu.

```text
[STATUS] tables=2000 seats=5000 occ=251 pend=4429 res=0 reserve_remaining=0 cashq=2322 dishes=606 revenue=13068 closing=0 fire=0
[STATUS] tables=2000 seats=5000 occ=275 pend=4404 res=0 reserve_remaining=0 cashq=2316 dishes=637 revenue=13814 closing=0 fire=0
[SIGUSR1] forwarded to worker
[STATUS] tables=2500 seats=6500 occ=287 pend=5680 res=0 reserve_remaining=0 cashq=2931 dishes=673 revenue=14552 closing=0 fire=0
[STATUS] tables=2500 seats=6500 occ=246 pend=5718 res=0 reserve_remaining=0 cashq=2953 dishes=682 revenue=14616 closing=0 fire=0
[STATUS] tables=2500 seats=6500 occ=251 pend=5709 res=0 reserve_remaining=0 cashq=2965 dishes=771 revenue=15350 closing=0 fire=0
[STATUS] tables=2500 seats=6500 occ=278 pend=5679 res=0 reserve_remaining=0 cashq=2959 dishes=830 revenue=16596 closing=0 fire=0
[SIGUSR2] forwarded to worker (reservation handshake)
[RESERVE] Podaj liczbe miejsc do rezerwacji (0..2000): 250
[STATUS] tables=2500 seats=6500 occ=408 pend=5540 res=250 reserve_remaining=0 cashq=2920 dishes=1004 revenue=21442 closing=0 fire=0
[STATUS] tables=2500 seats=6500 occ=447 pend=5498 res=250 reserve_remaining=0 cashq=2905 dishes=1048 revenue=22716 closing=0 fire=0
[STATUS] tables=2500 seats=6500 occ=474 pend=5467 res=250 reserve_remaining=0 cashq=2904 dishes=1114 revenue=24104 closing=0 fire=0
[STATUS] tables=2500 seats=6500 occ=502 pend=5436 res=250 reserve_remaining=0 cashq=2901 dishes=1185 revenue=25576 closing=0 fire=0
[STATUS] tables=2500 seats=6500 occ=503 pend=5427 res=250 reserve_remaining=0 cashq=2913 dishes=1285 revenue=27088 closing=0 fire=0
[STATUS] tables=2500 seats=6500 occ=492 pend=5432 res=250 reserve_remaining=0 cashq=2921 dishes=1388 revenue=28496 closing=0 fire=0
[STATUS] tables=2500 seats=6500 occ=541 pend=5380 res=250 reserve_remaining=0 cashq=2911 dishes=1450 revenue=30302 closing=0 fire=0
[FIRE] evacuating clients NOW!
FINAL_STATUS (FIRE_END) tables=2500 seats=6500 occ=0 pend=0 res=250 reserve_remaining=0 cashq=0 dishes=1500 revenue=31186 closing=1 fire=1
```

Wnioski: Program działa stabilnie mimo dużej liczby procesów i obsługi wielu sygnałów w trakcie działania. Po SIGUSR1 wzrosła liczba stolików i pojemność sali, po SIGUSR2 pojawiła się rezerwacja res=250. Po pożarze system przeszedł w tryb ewakuacji i zakończył działanie z wyczyszczonym stanem sali (occ=0, pend=0, cashq=0) oraz poprawnym ustawieniem flag (closing=1, fire=1). TEST ZDANY.

# 11. Problemy, wnioski

- Synchronizacja pamięci dzielonej i unikanie wyścigów - aktualizacje zajętości stolików musiały być atomowe. Zastosowano semafor-mutex oraz rozdzielenie stanu na pending_seats (rezerwacja w trakcie) i occupied_seats (realnie zajęte), żeby uniknąć sytuacji, w której dwa procesy „przydzielą” to samo miejsce.
- Czekanie na miejsce bez busy-waitingu - zamiast aktywnego odpytywania, klienci oczekują na zdarzenia stolikowe na semaforze zdarzeniowym z limitem czasu (timeout). Dzięki temu program jest stabilny przy dużym obciążeniu.
- EINTR i przerwane wywołania systemowe - blokujące operacje (np. msgrcv, semop, waitpid, operacje na plikach) mogą być przerywane przez sygnały; w kodzie uwzględniono obsługę errno == EINTR, aby procesy kontynuowały pracę poprawnie.
- Koordynacja zakończenia symulacji i sprzątanie IPC - po zakończeniu (lub pożarze) trzeba było zagwarantować, że procesy potomne kończą pracę, a zasoby SysV IPC są usuwane z systemu. Zadbano o kolejność kończenia procesów oraz IPC_RMID dla zasobów.

Wnioski: Systemy wieloprocesowe z SysV IPC wymagają bardzo ostrożnej synchronizacji (mutex + jasno zdefiniowane stany pośrednie), obsługi sytuacji wyjątkowych (sygnały, EINTR) oraz konsekwentnego sprzątania zasobów. Dobre logowanie znacząco ułatwia debugowanie i weryfikację poprawności.

# 12. Elementy dodatkowe

Poniższe elementy są ponad minimum opisu tematu i służą zwiększeniu stabilności/testowalności lub czytelności działania symulacji („wyróżniające elementy”):

- Cykliczny status managera oraz kolorowanie wyjścia terminala - okresowo wypisywane podsumowanie stanu (tables, seats, occ, pend, res, reserve_remaining, dishes, revenue, fire) pozwala szybko ocenić, czy system się nie „zatrzymał” oraz jak sygnały wpływają na symulację.
  <img width="1166" height="300" alt="image" src="https://github.com/user-attachments/assets/713ae083-6b6f-4de7-823d-8592a431b434" />

- Szczegółowe logowanie per-proces - osobne pliki logs/*.log dla managera/worker/cashier/klientów z timestampami i zdarzeniami (ułatwia analizę przebiegu i dowodzenie testów).

- Reprodukowalność poprzez seed - możliwość ustawienia ziarna losowości (stabilniejsze testowanie i porównywanie wyników).

- Opcjonalnie oczekiwanie z limitem czasu zamiast aktywnego odpytywania - klient nie „kręci się w pętli”, tylko czeka na zdarzenia z timeoutem (co ogranicza obciążenie CPU i poprawia stabilność przy dużej liczbie klientów).

- Dwa tryby rezerwacji (SIGUSR2) - rezerwacja może być zadana parametrem uruchomienia albo interaktywnie po sygnale (handshake worker-manager), co ułatwia demonstrację na prezentacji i w testach.

# 13. Permalinki do kluczowych fragmentów kodu

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

