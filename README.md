# BAR MLECZNY

# Repozytorium
https://github.com/NataliaP5/Projekt_Bar_Mleczny

# Opis projektu
Projekt polega na zbudowaniu symulacji pracy baru mlecznego w środowisku wieloprocesowym.
Poszczególne role w systemie — klient (grupa znajomych), kasjer, pracownik obsługi oraz kierownik — są odwzorowane jako oddzielne procesy, które współdziałają i wzajemnie się synchronizują.
Symulacja odwzorowuje typowe sytuacje w barze: losowy napływ klientów, składanie zamówień, zajmowanie stolików, obsługę płatności oraz reakcje na decyzje kierownika, który może w trakcie działania zmieniać liczbę dostępnych miejsc lub zainicjować ewakuację.

# Założenia
W lokalu znajdują się stoliki:
X1 – 1-osobowe,
X2 – 2-osobowe,
X3 – 3-osobowe,
X4 – 4-osobowe.

Maksymalna liczba osób jedzących równocześnie na sali wynika z sumy:
N = X1·1 + X2·2 + X3·3 + X4·4

Klienci przychodzą pojedynczo lub w grupach 2- i 3-osobowych (jeden proces = jedna grupa).
Około 5% klientów nie składa zamówienia – takie osoby nie mają prawa zajmować stolika. 
Pozostali:
- składają zamówienie,
- płacą w kasie,
- odbierają gorące danie od pracownika,
- dopiero wtedy mogą usiąść przy stoliku.

Niedopuszczalne jest, żeby ktoś z gotowym posiłkiem czekał na miejsce siedzące.

Przy jednym stolikuvmoże siedzieć jedna grupa lub kilka grup, ale jedynie wtedy, gdy są równoliczne.

Po zakończeniu jedzenia naczynia zwracane są albo przez każdego klienta z osobna, albo przez jedną osobę w imieniu całej grupy.

# Role procesów
Klient (grupa znajomych): Pojawia się w losowej chwili, decyduje o złożeniu zamówienia, przechodzi przez kasę, zajmuje stolik (jeśli jest dostępny), spożywa posiłek, oddaje naczynia i wychodzi.

Kasjer: Obsługuje płatności, rejestruje zamówienia i potwierdza zapłatę. Informacja o opłaconym zamówieniu pozwala klientowi odebrać posiłek i rozpocząć proces zajmowania miejsca.

Pracownik obsługi: Wydaje dania, prowadzi „plan stolików” (zajęte/wolne/zarezerwowane), pilnuje limitu N oraz zasad łączenia grup przy jednym stole. Reaguje też na decyzje kierownika (zmiana liczby stolików, rezerwacje, ewakuacja).

Kierownik baru: Nadzoruje całość i steruje zachowaniem systemu za pomocą sygnałów:
- Sygnał 1 – jednorazowe podwojenie liczby stolików 3-osobowych (X3),
- Sygnał 2 – żądanie zarezerwowania określonej liczby miejsc lub stolików, które stają się niedostępne dla zwykłych klientów,
- Sygnał 3 – pożar: klienci natychmiast opuszczają bar, po czym pracownicy kończą pracę, a kasa jest zamykana.

# Raport z działania
Przebieg symulacji zapisywany jest do pliku (lub kilku plików) tekstowego. Raport obejmuje m.in.:
- chronologiczny zapis zdarzeń (przyjście klienta, złożenie zamówienia, płatność, zajęcie i zwolnienie stolika, wyjście z baru),
- informacje o klientach, którzy nie złożyli zamówienia,
- momenty i skutki wysłania sygnałów przez kierownika (zmiana liczby stolików 3-osobowych, rezerwacje, pożar),
- przebieg ewakuacji po sygnale 3.

# Opis testów

- Test 1 – Standardowy ruch:
Umiarkowany napływ klientów, brak sygnałów od kierownika.
Sprawdzenie poprawności podstawowej obsługi (zamówienie → płatność → stolik → posiłek → wyjście) i nieprzekraczania limitu N.

- Test 2 – Klienci bez zamówienia:
Podwyższony udział klientów, którzy nic nie zamawiają.
Weryfikacja, że nie zajmują stolików i nie są liczeni jako osoby spożywające posiłek.

- Test 3 – Przydział stolików dla różnych grup:
Mieszane grupy 1-, 2- i 3-osobowe.
Sprawdzenie, czy nie ma przekroczeń pojemności stolików i czy różne grupy siadają razem tylko przy równej liczebności.

- Test 4 – Zwiększenie liczby stolików 3-osobowych (sygnał 1):
W trakcie działania wysyłany jest sygnał 1.
Kontrola jednorazowego podwojenia X3 i poprawnego uwzględnienia nowych stolików w przydziale.

- Test 5 – Rezerwacje (sygnał 2):
Kierownik żąda rezerwacji części miejsc/stolików (sygnał 2).
Sprawdzenie, czy zarezerwowane miejsca nie są przydzielane klientom i czy system poprawnie zmniejsza dostępną pojemność sali.

- Test 6 – Pożar i ewakuacja (sygnał 3):
Wysyłany jest sygnał 3 w trakcie normalnej pracy.
Weryfikacja natychmiastowego opuszczenia lokalu przez klientów, zakończenia pracy obsługi oraz poprawnego zamknięcia symulacji.

# Technologie i uruchomienie
Projekt jest przewidziany do realizacji w języku C / C++ z wykorzystaniem mechanizmów systemu Unix/Linux do obsługi procesów, komunikacji i synchronizacji.

gcc -o bar_mleczny main.c -lpthread
./bar_mleczny
