# Нативная отрисовка 1.6.14-esp32

Продолжение оптимизации от `c49eacf` (1.6.13). Результаты предыдущего этапа
сохранены в [NATIVE_OPTIMIZATION.md](NATIVE_OPTIMIZATION.md). Сенсорный сбор,
фильтры, частота опроса, формат записи и протокол измерений не менялись.
Ни одна проверка этого этапа не обращается к устройству, COM или USB.

## Причины и изменения

Профиль числового окна 1.6.13 показал на логический шаг 1018 промахов
кеша стиля и 112 промахов координат. Меняющийся цвет рамки сбрасывал
кеши соседних объектов. Разделены paint/geometry generations: локальное
неунаследованное изменение сбрасывает только собственный кеш; font,
padding, border width, content-size, состояния и наследование сохраняют
нужные зависимости. Увеличения размера объекта и новых кешей нет.

Непрозрачный диалог закрывал фон, но общая защита callbacks заставляла
рисовать его снова: за диалогом находились векторные иконки. Проверены
все ветви `icon_draw`: они только читают координаты/цвет и рисуют.
Только эти leaf-объекты отмечены `OCCLUSION_PAINT_INERT`. Неизвестные
DRAW_MAIN/POST/ALL callbacks и остальная пользовательская отрисовка
сохраняют консервативную защиту. Растр и сглаживание не упрощались.

Смена единственной MAIN role-reference теперь атомарна: старый стиль
заменяется в том же узле, без промежуточного удаления и добавления.
Разрешённые inherited/geometry свойства сравниваются до/после без кеша.
Если их значения не изменились, сброс соседних кешей не требуется.
Неоднозначные списки стилей и selectors идут прежним путём.

Клавиатура при PRESSED в текущем эталоне меняет ITEMS-цвета всех клавиш.
Сужение dirty до одной клавиши изменило бы промежуточный RGB. Поэтому
полная инвалидация в этом случае сохранена; тест проверяет и этот
fallback, и совпадение промежуточной/полной отрисовки при вводе.

## Отдельная задержка планировщика

В первом native-порте (`c4cf2cf`) условие `start-last_render>=100000`
ограничивало **все** кадры частотой 10 Гц. В исходном LVGL-приложении
интервал 100 мс относился к обновлению состояния в `main.c`, а не к
каждому touch-кадру. Host benchmark этого ожидания не моделирует.

Теперь наблюдаемые press/hold/release ставят запрос взаимодействия:
следующая отрисовка допускается с интервалом 40 мс, максимум 25 Гц.
Обычная периодическая отрисовка и обновление живых данных остаются
100 мс / 10 Гц. Запрос переживает pending DMA; до completion запрещены
повторный render, PPA и повторное использование буферов. Ожидания
PPA/LCD по-прежнему не удерживают UI mutex.

Это **допуск планировщика**, а не обещание аппаратного отклика за 40 мс:
I²C, RTOS, mutex, PPA и LCD могут добавить задержку. При PPA/submit error
без нового ввода повтор остаётся на периодическом интервале 100 мс.
Новые coarse поля `ui perf`: `interactive_renders` и
`input_render_wait_max_us`. Последнее считает от наблюдения input до
начала CPU render, исключает touch I²C, поворот и LCD.

Тест настоящего `native_task` с source stubs подтвердил: press/release
через 10/20 мс после периодического кадра обслуживаются на 40, а не
100 мс; hold ограничен 25 Гц; non-input dirty — 10 Гц; запрос при позднем
DMA completion сохраняется. Прежние 12 startup failure cleanups,
отсутствие lock при PPA/flush и mapping всех PPA90 пикселей также PASS.
Это fake-clock тест кода, не замер прибора.

## Методика

LVGL `369c7fc`, неизменённая 1.6.13 `c49eacf` и новая версия используют
одинаковые immutable input/helper/contract, RGB565 1280×720 и GCC 14.2
`-O2`. Порядок вариантов меняется по трём раундам; на сцену/тему —
120 warmup и 600 измеряемых логических шагов. Сырой total включает
синхронный host RGB565→RGB888 mock sink; это не время PPA/DSI на приборе.
Engine-only, primitive/cache profile и cold пробы снимаются отдельно.

Критерий изображения — 98 decoded RGB кадров точно к immutable native v1,
а также release/diagnostic и промежуточный incremental/full framebuffer.
Сравнение с LVGL не объявляется побитовым: исходные отличия сглаживания
native v1 остаются прежними. Частота и точность измерений не подменяются.

## Финальные три тихих раунда

**p50 / p95, мс на логический шаг**; по 1800 шагов на строку/вариант.
Это не время отдельного физического кадра. Покой не вызывает flush.

| Сцена / тема | LVGL | 1.6.13 (`c49`) | 1.6.14 |
|---|---:|---:|---:|
| Покой / тёмная | 0,0058 / 0,0060 | 0,0081 / 0,0089 | 0,0081 / 0,0134 |
| Покой / светлая | 0,0060 / 0,0102 | 0,0080 / 0,0087 | 0,0081 / 0,0085 |
| Углы / тёмная | 0,5707 / 0,6368 | 0,4710 / 0,5074 | 0,4567 / 0,5086 |
| Углы / светлая | 0,6589 / 1,0227 | 0,4770 / 0,6625 | 0,4580 / 0,5646 |
| График / тёмная | 0,3975 / 1,0237 | 0,2556 / 0,8889 | 0,2456 / 0,8785 |
| График / светлая | 0,5481 / 1,5864 | 0,3686 / 1,2640 | 0,3678 / 1,1988 |
| Modal / тёмная | 0,2111 / 0,2291 | 0,2959 / 0,3308 | 0,1917 / 0,2082 |
| Modal / светлая | 0,2244 / 0,3567 | 0,3347 / 0,5261 | 0,2042 / 0,3372 |
| Переход / тёмная | 2,6569 / 3,5591 | 2,1873 / 2,7745 | 2,1690 / 2,7465 |
| Переход / светлая | 3,0314 / 4,0757 | 2,6587 / 3,7109 | 2,5954 / 3,7076 |

Modal total median меньше 1.6.13 на **35–39%**, теперь также примерно
на 9% меньше LVGL. У переходов total median уменьшился лишь на 0,8–2,4%:
в нём велика доля mock sink, которую нельзя выдать за стоимость дисплея.
В покое остались около 2–3 мкс/шаг дополнительной работы к LVGL; кадров нет.

Coarse engine render исключает mock flush. Далее **арифметическое среднее**
case-total clock / 1800, не median/p95 и не чистый layout timer:

| Сцена / тема | 1.6.13 engine, мс/шаг | 1.6.14 engine, мс/шаг |
|---|---:|---:|
| Modal / тёмная | 0,144253 | 0,037941 |
| Modal / светлая | 0,176595 | 0,045663 |
| Переход / тёмная | 0,782039 | 0,738393 |
| Переход / светлая | 0,925292 | 0,857737 |

Собственная отрисовка modal дешевле на **74%**, переходов — на **5,6–7,3%**.
Pure engine clock LVGL в стенде недоступен. Частные primitive clocks могут
перекрываться; они не складываются с total/pump/copy.

Число refresh за 1800 логических шагов у всех трёх вариантов одинаково:
покой 0; углы, график, modal — 1800; переходы — 2520. Изменение production
port scheduling не входит в эти host refresh counts.

Отдельный diagnostic profile подтвердил механизм:

| На шаг | 1.6.13 | 1.6.14 |
|---|---:|---:|
| Modal style misses | 1018 | 26 |
| Modal layout misses | 112 | 0 |
| Modal fill pixels | 321 686 | 142 136 |
| Transition style misses | 1439,2 | 47 |
| Transition layout misses | 106,8 | 0 |

Dirty work modal остаётся 81 928 пикселей/шаг: ускорение не получено
пропуском обновления данных. C49/new scene RGB совпали 30/30. Standard
и cache diagnostic каждого native backend дали те же итоговые RGB и coarse
refresh/dirty work, что release, 30/30 на adapter. Fine counters обычной
сборки отключены и не сравнивались как измеренные нули.

## Холодный первый кадр и память

Свежий процесс на backend/тему, до замера не было pump/ticks/render;
проверены frames=0 до и frames=1 после. UI init/update измеряется отдельно.
Это **по одному diagnostic sample**, не оценка p50/p95 и не аппаратный boot.

| Вариант / тема | UI init/update, мс | Первый draw с mock sink, мс | Engine без sink, мс |
|---|---:|---:|---:|
| 1.6.13 / тёмная | 1,3841 | 4,4331 | 1,9890 |
| 1.6.13 / светлая | 1,2723 | 4,3175 | 1,8720 |
| 1.6.14 / тёмная | 1,3264 | 4,0791 | 1,8440 |
| 1.6.14 / светлая | 1,2217 | 4,3611 | 1,8100 |

Before/after gauges подтверждают реальное построение холодных масок и LUT.
Разница одного sample не доказывает устойчивое ускорение холодного старта.
В каждом native варианте текущий host payload одинаков: **289 858–334 511 Б**,
process peak **334 592 Б**, allocation failures 0. Заголовки allocator/CRT,
внешние буферы и RSS сюда не входят; live blocks не доказывает ноль malloc/frame.

Новых heap-кешей нет. Добавлена одна 64-битная geometry generation;
два coarse port counters — отдельные `uint32_t`. RV32 из actual ELF обоих
профилей: объект **392 Б**, renderer context **2048 Б** со stats **424 Б**,
shadow descriptors **448 Б**, gradient static **2600 Б** — прежние размеры.
Object/shadow allocated payload уже включён в heap, повторно не прибавляется.
Эти static/host данные не являются измерением свободного heap на P4.

## Готовые пакеты и gate

Обе actual IDF-сборки имеют descriptor и manifest **1.6.14-esp32**;
LVGL components/link inputs/symbols отсутствуют. Все шесть проверенных
compile commands: fine=0/timing=0. Версия host fixture остаётся
`1.6.1-esp32` у всех вариантов, actual PROJECT_VER не участвует в host benchmark.

| Профиль | App BIN, Б | SHA256 |
|---|---:|---|
| P4 revision 100–199 | 1 427 360 | `666d91519d5dbcd00263c394ae28e6ced9d4a7a70d63338b0a86f571f737c334` |
| P4 revision 300–399 | 1 429 584 | `2cbfcd5dd26671871e9d005895e356b88991b8b28455cd6fe238b70f631ecb10` |

- 98 кадров обоих flags RGB-exact к immutable native v1 и друг другу;
  geometry/semantics 0, все прежние input assertions PASS.
- Numeric valid/invalid/disabled, keyboard conservative fallback, press/move/
  release/press-lost/mode/repeat/cursor, cache inheritance/geometry и unknown
  callbacks: incremental/full RGB и event checks PASS обоих flags.
- Atomic-role: 18 случаев на mode, sole MAIN без нового узла; unknown
  styles/selectors идут последовательным fallback, font/pad/border/inherited
  и warm/cold geometry PASS.
- Lifetime64 и dirty-page40 обоих flags; gradient/profile/CLI, callback,
  clipped4bpp/span regressions PASS. Sanitizer runtimes в установленном GCC
  отсутствуют, sanitizer-запуск не заявляется.

Полные локальные пакеты — `build/package/`, `build-rev3/package/`. Устройства
не подключались и не прошивались, аппаратная загрузка CPU и LCD latency
не измерялись. Независимая повторная сверка также подтвердила 98/98 RGB
и оба пакета 1.6.14: размеры, версии и SHA256 совпали.

## Свидетельства

- [Полная таблица/методика](docs/native-opt-1.6.14/RESULTS.md),
  [CSV таблица](docs/native-opt-1.6.14/table.csv),
  [54 000 raw samples](docs/native-opt-1.6.14/benchmark-raw_samples.csv),
  [pooled phases](docs/native-opt-1.6.14/benchmark-aggregate.json).
- [Churn/fill](docs/native-opt-1.6.14/cache-churn-summary.json),
  [cold samples](docs/native-opt-1.6.14/cold-summary.json),
  [RAM](docs/native-opt-1.6.14/ram-summary.json),
  [port source-stub result](docs/native-opt-1.6.14/native-port-test.txt).
- [Функциональный gate](docs/native-opt-1.6.14/functional/FINAL_GATE.json),
  [hashes](docs/native-opt-1.6.14/optimized-source.json),
  [c49 snapshot](docs/native-opt-1.6.14/baseline-c49-source.json).
- [Rev1 proof](docs/native-opt-1.6.14/rev1-native-final-build-proof.json),
  [rev3 proof](docs/native-opt-1.6.14/rev3-native-final-build-proof.json),
  [package hashes](docs/native-opt-1.6.14/packages.json),
  [RV32 rev1](docs/native-opt-1.6.14/target-layout-rev1.json),
  [RV32 rev3](docs/native-opt-1.6.14/target-layout-rev3.json).

Полные локальные benchmark artifacts:
`.tools/native-optimization-1.6.14/final-0dee13b5/`.
Все финальные PNG:
`.tools/native-1.6.14/final-20261023-005512-0dee13b5/equivalence/release/`.
Повторяемый runner: `python tab5/tests/native_baseline_followup.py --help`.
