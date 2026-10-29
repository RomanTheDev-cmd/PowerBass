#!/bin/bash
# Проверка шага «make_installers.sh обновляет Проекты/Plaguins/<плагин>» на копии проекта во временной папке.
# Настоящие «Установщики», «Plaguins» и Корзина не трогаются (TRASH_DIR указывает во временную папку).
#   bash PowerBass/Tools/test_plaguins.sh <плагин>   (из папки «Проекты»; плагин — PowerSynth, PowerKick, PowerBass или PowerComp)
set -u
P="$1"; REAL="/Users/roman/Library/Mobile Documents/com~apple~CloudDocs/Главная рабочая папка ИИ/Проекты"
T="$(mktemp -d)"; ROOT="$T/Проекты"; D="$ROOT/$P"; PL="$ROOT/Plaguins/$P"; TR="$T/trash"
mkdir -p "$D" "$TR"
ditto "$REAL/$P/CMakeLists.txt" "$D/CMakeLists.txt"; ditto "$REAL/$P/Tools" "$D/Tools"; ditto "$REAL/$P/Сборки" "$D/Сборки"
VER="$(sed -n 's/^project([A-Za-z]* VERSION \([0-9.]*\).*/\1/p' "$D/CMakeLists.txt")"
MAC="$P-$VER-macOS.pkg"; WIN="$P-$VER-Windows-x64-Setup.exe"
realsum() { find "$REAL/Plaguins" "$REAL/$P/Установщики" -type f -exec shasum {} \; | sort; }
before="$(realsum)"
fails=0; check() { if eval "$2"; then echo "  ok   $1"; else echo "  FAIL $1"; fails=$((fails+1)); fi; }

echo "1) прежние версии и чужой файл в папке плагина"
mkdir -p "$PL"; echo old > "$PL/$P-0.9.0-macOS.pkg"; echo old > "$PL/$P-0.9.0-Windows-x64-Setup.exe"; echo note > "$PL/заметка.txt"
TRASH_DIR="$TR" bash "$D/Tools/make_installers.sh" > "$T/run1.txt" 2>&1; r=$?
check "make_installers завершился успешно" '[ $r = 0 ]'
check "в Plaguins новый .pkg, равный установщику" 'cmp -s "$D/Установщики/$MAC" "$PL/$MAC"'
check "в Plaguins новый Setup.exe, равный установщику" 'cmp -s "$D/Установщики/$WIN" "$PL/$WIN"'
check "прежних версий в Plaguins нет" '[ ! -e "$PL/$P-0.9.0-macOS.pkg" ] && [ ! -e "$PL/$P-0.9.0-Windows-x64-Setup.exe" ]'
check "прежние версии в Корзине" '[ $(find "$TR" -name "$P-0.9.0-*" | wc -l) = 2 ]'
check "чужой файл остался" '[ -e "$PL/заметка.txt" ]'
check "в папке плагина ровно 3 файла" '[ $(ls "$PL" | wc -l) = 3 ]'
check "отчёт называет Plaguins" 'grep -q "Plaguins" "$T/run1.txt"'

echo "2) повторная сборка той же версии"
old_mac="$(shasum "$PL/$MAC")"
TRASH_DIR="$TR" bash "$D/Tools/make_installers.sh" > "$T/run2.txt" 2>&1; r=$?
check "make_installers завершился успешно" '[ $r = 0 ]'
check "копии равны новым установщикам" 'cmp -s "$D/Установщики/$MAC" "$PL/$MAC" && cmp -s "$D/Установщики/$WIN" "$PL/$WIN"'
check "прежняя копия той же версии в Корзине, а не затёрта" '[ $(find "$TR" -name "$MAC" | wc -l) = 1 ] && [ $(find "$TR" -name "$WIN" | wc -l) = 1 ]'

echo "3) папки Plaguins ещё нет"
mv "$ROOT/Plaguins" "$T/plaguins-before-3"
TRASH_DIR="$TR" bash "$D/Tools/make_installers.sh" > "$T/run3.txt" 2>&1; r=$?
check "make_installers завершился успешно" '[ $r = 0 ]'
check "папка создана, в ней оба установщика" 'cmp -s "$D/Установщики/$MAC" "$PL/$MAC" && cmp -s "$D/Установщики/$WIN" "$PL/$WIN"'

echo "4) настоящие файлы не тронуты"
check "Проекты/Plaguins и Установщики без изменений" '[ "$(realsum)" = "$before" ]'

echo "$P: провалов $fails (временная папка $T)"; tail -3 "$T/run1.txt"
[ $fails = 0 ]
