# mangen

`mangen` — многопоточная UNIX-утилита на **C17** для построения
детерминированного манифеста файлов с SHA-256, SHA-384 или SHA-512.

- Автор: **Агибалов Владислав Сергеевич**, НИЯУ МИФИ, ИВТ.
- Год документации: **2026**.
- Целевые ОС: **macOS и Linux** (подтверждение сборки — отдельно на каждой ОС).
- Репозиторий: <https://github.com/multmine1/safeboard_2026_11_C_mangen_>.
- Производственные библиотеки: C17 и POSIX; тесты: GoogleTest/C++17.

## 1. Сборка и установка

Доступен автономный исходный ZIP с вендорными исходниками **Concurrency Kit
0.7.2**, **Mbed TLS 3.6.7 LTS** и **GoogleTest 1.18.0**. Для сборки требуются
CMake 3.24+, компилятор C17, POSIX threads и Make или Ninja. Для тестов
дополнительно нужен компилятор C++17. Во время сборки скачивание библиотек
не требуется.

На macOS и Linux в корне проекта:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/install"
./build/install/bin/mangen --help
./build/install/bin/mangen -v
```

Сборка без GoogleTest и без необходимости C++-компилятора:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --parallel 4
cmake --install build/release --prefix "$PWD/build/release/install"
```

Если сборка или установка завершилась с ошибкой, переходить к выпуску ZIP
нельзя. В режиме `BUILD_TESTING=OFF` приложение не должно зависеть от C++.

## 2. Интерфейс

```text
mangen [DIR_PATH] [OPTIONS]
```

| Опция | Действие |
|---|---|
| `DIR_PATH` | Корень обхода, по умолчанию `.` |
| `-h`, `--help` | Справка |
| `-v`, `--version` | Версия, автор и полный SHA исходного Git-коммита |
| `-e PATTERN` | Исключение basename по wildcard; допускает повторы |
| `--sha256` | SHA-256, по умолчанию |
| `--sha384` | SHA-384 |
| `--sha512` | SHA-512 |
| `-j N`, `--threads N` | Положительное количество потоков |
| `--queue-size N` | Стартовая ёмкость очереди |

При успешной обработке в stdout записываются строки вида:

```text
relative/path.txt : D49A...<полный HEX SHA>
```

В настоящем манифесте HEX всегда полный (64/96/128 символов) и заглавный;
строка с троеточием выше служит **только иллюстрацией формата**. Выход
отсортирован по байтам относительных имён и не содержит диагностики.
Внутри имени экранируются `\`, перевод строки и возврат каретки.

Выполнение:

```sh
./build/install/bin/mangen ./data --sha256 --threads 4 > manifest.txt 2> errors.txt
printf 'exit=%s\n' "$?"
```

Коды завершения: **0** — полный успех; **1** — ошибки обхода/чтения,
возможен *частичный* манифест; **2** — неправильные аргументы. При статусе 1
доступные файлы могут быть включены, но отсутствие остальных файлов нельзя
считать подтверждённым. Ссылка на обычный файл обрабатывается, ссылка на
каталог не обходится. Манифест не является атомарным снимком изменяющейся ФС.

## 3. Архитектура

| Компонент | Ответственность |
|---|---|
| `libs/matcher` | Wildcard → NFA → DFA → минимизация |
| `libs/queue` | MPMC-очередь заданий |
| `libs/catalog` | Локальные наборы, сортировка, состояния и ожидание |
| `libs/hash` | Потоковый SHA-2 через Mbed TLS |
| `libs/hash_pool` | Параллельная выдача и выполнение хэшей |
| `libs/walker` | Рекурсивное обнаружение файлов |
| `libs/manifest` | Упорядоченный вывод и экранирование |
| `app` | CLI, оркестрация, статусы процесса |

В библиотеке `matcher` wildcard-шаблоны сопоставляются по графемным
кластерам Unicode с NFC-нормализацией. Алгоритмы NFA, DFA и минимизации
подробно описаны в `docs/PDF/architecture.pdf`.

## 4. Все вспомогательные скрипты

Все скрипты проекта расположены **непосредственно в `scripts/`**.
Дополнительных скриптов в папках документации нет.

| Скрипт | Действие |
|---|---|
| `scripts/generate_test_trees.sh` | Два дерева по 6403 записи: полное и с chmod-ограничениями |
| `scripts/verify_test_trees.sh` | 12 запусков: три SHA × два дерева × 1/4 потока; независимый системный SHA |
| `scripts/test_portability.sh` | Сборка, CTest и установка на текущей macOS/Linux в режимах с/без тестов |
| `scripts/test_memory.sh` | ASan/UBSan; LSan на Linux |
| `scripts/build_docs.sh` | XeLaTeX → три PDF |
| `scripts/sign_manifest.sh` | Подпись манифеста через OpenSSH |
| `scripts/verify_manifest_signature.sh` | Проверка подписи через доверенного подписанта |
| `scripts/test_manifest_signature.sh` | Позитивная и негативная проверка SSH-подписи |
| `scripts/package_release.sh` | ZIP из коммита A, SHA-256 для коммита B |
| `scripts/verify_release.sh` | Сверка ZIP и доверенного TXT |
| `scripts/test_release_checksum.sh` | Позитивная и негативная проверка ZIP SHA-256 |

Проверка большого дерева на macOS или Linux **без root**:

```sh
TEST_ROOT=$(bash scripts/generate_test_trees.sh)
bash scripts/verify_test_trees.sh "$TEST_ROOT" ./build/install/bin/mangen
```

Генератор печатает путь к новому временному каталогу. Он не изменяет данные
в существующих пользовательских каталогах. Проверка вычисляет эталонный SHA
системными средствами: `shasum -a N` на macOS, либо GNU `sha256sum`,
`sha384sum`, `sha512sum` на Linux. Проверяются точный набор строк, сортировка,
все значения HEX, многопоточность и код завершения. Проверку chmod нужно
выполнять обычным пользователем: root может обходить запреты прав.

Компиляторы и санитарные проверки:

```sh
bash scripts/test_portability.sh
bash scripts/test_memory.sh "$TEST_ROOT/clean"
```

Результат macOS не заменяет запуск тестов на Linux. При необходимости
`CC=/usr/bin/gcc bash scripts/test_portability.sh` выбирает один компилятор.

## 5. Документация

| PDF | Содержание | LaTeX |
|---|---|---|
| `docs/PDF/architecture.pdf` | Модули, инварианты, ресурсы, параллелизм | `docs/latex/architecture/main.tex` |
| `docs/PDF/user-guide.pdf` | CLI, сборка на macOS/Linux, безопасность | `docs/latex/user-guide/main.tex` |
| `docs/PDF/verification.pdf` | Матрица и результаты испытаний, ограничения | `docs/latex/verification/main.tex` |

Общие настройки верстки — `docs/latex/common/preamble.tex`, принципы
оформления — `docs/format.md`. PDF собираются XeLaTeX без `--shell-escape`:

```sh
bash scripts/build_docs.sh
```

Требуются `xelatex` и `latexmk`. Документы содержат описание алгоритмов,
сборки, испытаний и ограничений проверенной реализации.

## 6. Защита манифеста при передаче через ненадёжный канал

**Контрольная сумма без доверенного источника не обеспечивает аутентичность**:
атакующий может заменить и содержимое, и его SHA-256. Для подтверждения
неизменности и источника манифеста используется штатная подпись OpenSSH:
`ssh-keygen -Y sign/verify`, namespace `mangen-manifest`.

Подписант использует собственный SSH-ключ Ed25519; он может уже существовать,
создавать новый необязательно. Секретный ключ никогда не помещается в ZIP.

```sh
bash scripts/sign_manifest.sh manifest.txt "$HOME/.ssh/id_ed25519"
```

Получатель должен получить **публичный ключ подписанта заранее и по
независимому доверенному каналу**. В файле `trusted_signers` одна запись
формата `IDENTITY ssh-ed25519 PUBLIC_KEY`, где `IDENTITY` заранее согласована.
Пример проверки (подставить реальные значения):

```sh
bash scripts/verify_manifest_signature.sh manifest.txt manifest.txt.sig trusted_signers maintainer@example.org
```

Результат 0 означает: байты файла подписаны доверенным ключом и не изменены.
Это **не** подтверждает, что файлы на диске совпадают с манифестом после
передачи: содержимое файлов при необходимости отдельно пересчитывается.
Получение `trusted_signers` вместе с подменяемым файлом без независимой
проверки ключа делает схему уязвимой к замене подписанта.

Самопроверка схемы с временным тестовым ключом:

```sh
bash scripts/test_manifest_signature.sh
```

## 7. Поставка ZIP и два Git-коммита

**Коммит A** содержит код, тесты, документацию LaTeX/PDF, данный README и
скрипты проверки. Архив строится **только после** создания коммита A.
Убедиться, что в него попала полная актуальная реализация `app`, `walker`,
`manifest`, вендорные исходники и необходимые лицензии.

```sh
bash scripts/build_docs.sh
git status --short
git add -A -- CMakeLists.txt README.md .gitignore LICENSE app libs scripts docs
git diff --cached --check
git commit -m "release: prepare source and documentation"
SOURCE_COMMIT=$(git rev-parse HEAD)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 4
cmake --install build --prefix "$PWD/build/install"
./build/install/bin/mangen -v | grep -F "$SOURCE_COMMIT"
bash scripts/package_release.sh
```

Перед коммитом A другие изменения проекта и точные удаляемые исторические
файлы проверяются с помощью `git status` и `git ls-files`; нельзя удалять
файлы по общему шаблону без проверки. При необходимости их добавляют в
**тот же коммит A**, после предметного просмотра. Команды выше не выполняют
автоматического удаления чужих файлов.

Упаковщик использует `git archive` для содержимого коммита A, исключая
ранее опубликованную папку `safeboard/`. Во временном дереве он создаёт
`VERSION-COMMIT` с полным SHA коммита A и формирует рядом два файла:
`safeboard/mangen-<id>.zip` и `safeboard/mangen-<id>.sha256.txt`.
TXT содержит SHA-256 **всего ZIP** и комментарий с исходным коммитом A.
Контрольную сумму нельзя включать внутрь этого ZIP: тогда изменится
его содержимое и, следовательно, сама сумма.

При конфигурации CMake из Git SHA получается командой `git rev-parse HEAD`;
при конфигурации из ZIP он считывается из `VERSION-COMMIT`. В обоих случаях
SHA встраивается в программу при компиляции и выводится опциями
`-v` и `--version`. После создания коммита A приложение необходимо
пересобрать с повторной конфигурацией CMake. До второго коммита
проверяется совпадение `-v` с SHA коммита A, включая сборку из ZIP.

**Коммит B** содержит ZIP и файл его контрольной суммы:

```sh
git status --short
ID=$(git rev-parse --short=12 HEAD)
bash scripts/verify_release.sh \
    "safeboard/mangen-${ID}.zip" \
    "safeboard/mangen-${ID}.sha256.txt"
git add -- "safeboard/mangen-${ID}.zip" "safeboard/mangen-${ID}.sha256.txt"
git commit -m "release: publish ZIP and SHA-256"
git log -2 --oneline
```

После коммита B исходный ZIP **не пересоздаётся**. Он соответствует исходникам
коммита A, дополнительному `VERSION-COMMIT` и имеет ту SHA-256, которая
зафиксирована в B. Git SHA коммита B не встраивается в этот ZIP:
он создаётся после упаковки и служит публикацией выпуска.

Получатель проверяет ZIP **до распаковки**:

```sh
bash scripts/verify_release.sh safeboard/mangen-<id>.zip safeboard/mangen-<id>.sha256.txt
```

Заменить `<id>` на значение, напечатанное упаковщиком. Подлинность ZIP
предполагает доверенное получение TXT или независимой подписи релиза.
Самопроверка контрольных сумм с намеренным повреждением копии ZIP:

```sh
bash scripts/test_release_checksum.sh
```

## 8. Точность утверждений о поддержке

Пользовательский журнал от ноября 2026 года подтверждает на macOS:
163/163 CTest, 6401/6401 SHA-256 после восстановления доступа,
идентичность `-j 1` и `-j 4`, успешный ASan/UBSan. Проверка Linux
**не подтверждена** данным журналом. Дополнительно на Ubuntu 24.04.3 x86-64
с GCC 13.3.0 выполнена синтаксическая проверка C17 пяти доступных
библиотек и настройка Concurrency Kit; полный Linux CMake/CTest пока
не выполнялся. Подробности приведены в `docs/PDF/verification.pdf`.

## 9. Лицензии

Лицензия собственного кода находится в `LICENSE`. Условия, уведомления
и файлы лицензий сторонних библиотек сохраняются в их соответствующих
каталогах `libs/third_party/` при формировании офлайнового ZIP.
