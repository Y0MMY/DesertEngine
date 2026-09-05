# Как устроена погода в Unreal Engine — разбор R3

Исследование R3 программы «Небо и облака». Забрано 2026-08-20.
Автор: ресерчер. Код не правился; продукт задачи — этот документ.

Задача, поставленная тимлидом: до написания программы W выяснить, как погоду решает эталон, — потому
что на облаках уже один раз оказалось, что «у UE тут смотреть не на что» было ложным утверждением.

---

## 0. Как читать этот документ

Каждое утверждение ниже помечено уровнем источника. Это не украшение: половина того, что в вебе
называется «weather system in Unreal», к движку отношения не имеет, а маркетплейс — не эталон по
определению.

| метка | что это | статус для нас |
|---|---|---|
| 🟩 **EPIC/ДВИЖОК** | официальная документация Unreal Engine на `dev.epicgames.com/documentation/unreal-engine` | эталон |
| 🟦 **EPIC/ДРУГОЙ ПРОДУКТ** | официальная документация Epic по Twinmotion или Fortnite/UEFN | не эталон движка, но показывает, что Epic считает погодой, когда её делает |
| 🟨 **СООБЩЕСТВО** | community-туториал, размещённый на `dev.epicgames.com/community` | мнение автора, не Epic. Домен тот же — уровень другой |
| 🟥 **МАРКЕТПЛЕЙС** | сторонний продукт (Fab/Marketplace) | **не эталон.** Записан только чтобы понимать, чего ждёт рынок |
| ⬜ **НЕ НАЙДЕНО** | искал, не нашёл; названо, что именно искал | честный пробел |

---

## 1. Страница, которую дал владелец

**Ссылка:** <https://dev.epicgames.com/community/learning/tutorials/nwb3/unreal-engine-how-to-build-a-dynamic-weather-system>

**Уровень: 🟨 СООБЩЕСТВО.** Это прямо написано в `<title>` самой страницы: *«How to Build a Dynamic
Weather System | Community tutorial»*. Автор — **SilkroadLabs**, дата — **21 июня 2024**, версия
движка — **UE 5.4**; автор и дата взяты из автоматически созданной ветки форума
<https://forums.unrealengine.com/t/community-tutorial-how-to-build-a-dynamic-weather-system/1911764>
(там же единственный ответ, от `pelorustech`, 24 июня 2024, — общая похвала без технического
содержания).

### 1.1 Как добывалось тело

Тимлид прав: тело страницы рендерится скриптом. Что пробовал:

- `WebFetch` по прямому URL — вернулся только `<title>`;
- `curl` с браузерным User-Agent — 5290 байт, это SPA-скелет: `<head>` с og-метаданными и пустой `<body>`;
- шесть вариантов внутреннего API (`/community/api/learning/{tutorial,post,posts,tutorials,content}/nwb3`,
  `/community/api/v1/...`) — **403 от Cloudflare** на всех;
- Wayback Machine — `{"archived_snapshots": {}}`, снимков нет вообще, CDX-запрос пуст;
- **сработало:** текстовый прокси `https://r.jina.ai/<url>` — 9672 байта готового markdown,
  275 строк, полный текст с оглавлением, FAQ и заключением.

### 1.2 Что в нём на самом деле есть

Отвечаю на вопросы тимлида по пунктам, и ответ в основном отрицательный.

**Какая архитектура предлагается.** Дословно: *«Create a new Blueprint Class derived from Actor.
Name it WeatherController. Add variables for different weather states (e.g., Clear, Rain, Snow)»* и
*«Inside the WeatherController, create a WeatherState enumeration with values for each weather type.
Add functions to change weather states and trigger corresponding effects»*.

То есть: **актор-владелец `WeatherController` + перечисление `WeatherState`**.

**Чем представлено состояние.** **Перечислением.** Ни data asset, ни структуры, ни кривой в тексте
нет — я искал слова `Data Asset`, `Struct`, `Curve`, `Timeline`, `Lerp`, `Interp` по всему телу
страницы, совпадений ноль.

**Как ведётся переход между состояниями.** **Никак — механизм не описан.** Единственное упоминание
переходов — в разделе тестирования: *«Ensure smooth transitions between weather states without visual
glitches»*, то есть требование к результату без указания, чем оно достигается.

**Какие системы дёргаются и через какие поимённо названные параметры.** Системы названы:
Volumetric Cloud actor, Niagara или Cascade (дождь и снег), Lumen, fog/mist, post-processing volumes.
**Поимённо названных параметров нет ни одного.** Максимальная точность текста — *«Adjust parameters
such as density, coverage, and lighting»*, то есть слова «density» и «coverage» строчными буквами, без
привязки к именам свойств компонента или материала.

### 1.3 Оценка

Это SEO-статья про UE вообще, а не разбор погоды: примерно треть объёма занимают системные требования
UE 5.4, инструкция «как создать проект в лаунчере», сравнение Blueprints и C++, раздел про публикацию
на платформах и FAQ вида «How do I start using Unreal Engine 5.4?».

**Как источник архитектурных решений страница непригодна.** Из неё извлекается ровно одна мысль —
«состояние погоды владеет отдельный актор, состояний конечное число» — и она тривиальна.

Полезность у страницы всё же есть, но косвенная: она **подтверждает главный вывод §2**. Если бы у
Epic была первосторонняя система погоды, туториал 2024 года на портале Epic начинался бы с неё, а не
с «создайте Blueprint от Actor».

---

## 2. ГЛАВНЫЙ ВОПРОС: есть ли у Epic первосторонняя система погоды

### 2.1 Ответ

**В Unreal Engine первосторонней системы погоды НЕТ.** Ни в составе движка, ни отдельным плагином.
Нет типа, который владел бы состоянием погоды; нет ассета «погодный пресет»; нет механизма перехода
между погодами; нет первосторонних осадков.

Это прямой ответ, которого тимлид просил: **архитектуру мы выбираем сами, у UE берём только контракты
параметров.**

### 2.2 Чем это доказано

**Отрицательное доказательство (⬜ НЕ НАЙДЕНО), с перечнем того, что искал:**

- поиск по `dev.epicgames.com` со словом weather возвращает **только community-туториалы**
  (`nwb3`, `VYOM` — «Advanced Dynamic Weather with Landscape Automaterial and Mesh Blending`,
  `5nKZ` — «Rain and Thunder Tutorial», `Oz5G` — «Random Precipitation Algorithm») и **ни одной**
  страницы в `documentation/unreal-engine/`;
- в C++ API Reference (<https://dev.epicgames.com/documentation/unreal-engine/API>) класса со словом
  Weather найти не удалось; искал `UWeather*`, `AWeather*`, `WeatherComponent`, `WeatherSubsystem`;
- в release notes UE 5.5 / 5.6 / 5.7 слово weather среди новых фич не встречается; проверял через
  страницы релиз-ноутов и обзорные материалы по 5.6/5.7 — там PCG, Nanite Foliage, Substrate,
  MegaLights, Day Sequence, но не погода;
- в разделе «Environmental Light with Fog, Clouds, Sky and Atmosphere»
  (<https://dev.epicgames.com/documentation/en-us/unreal-engine/environmental-light-with-fog-clouds-sky-and-atmosphere-in-unreal-engine>)
  перечислены Sky Atmosphere, Volumetric Cloud, Exponential Height Fog, Sky Light, Directional Light —
  и никакого объединяющего их «погодного» слоя;
- по сэмплам **City Sample / Matrix Awakens** и **Electric Dreams** документации по осадкам,
  мокрым поверхностям или погодному состоянию найти не удалось. Официальная страница City Sample
  (<https://dev.epicgames.com/documentation/unreal-engine/city-sample-project-unreal-engine-demonstration>)
  перечисляет World Partition, Nanite, Lumen, Chaos, Rule Processor, Mass AI, Niagara, MetaHumans,
  MetaSounds, TSR — про погоду там нет ничего. **Это «не нашёл», а не «нет»:** проекты
  распространяются через Fab архивом, и содержимое их контент-папок я проверить не мог. Если тимлиду
  нужен точный ответ по City Sample — это отдельная задача со скачиванием проекта.

**Положительное доказательство того, что́ у Epic вместо погоды, — три штуки.**

### 2.3 🟩 Day Sequence — первосторонний плагин, но это ВРЕМЯ СУТОК, не погода

Источник: <https://dev.epicgames.com/documentation/en-us/unreal-engine/day-sequence-time-of-day-plugin-for-unreal-engine>

Это самая близкая к погоде первосторонняя вещь в движке, и разобрать её надо целиком, потому что
именно её механику мы можем позаимствовать.

**Что это.** Дословно: *«The **Day Sequence** plugin is a collection of actors and assets you can use
and create to automatically generate a 24-hour day cycle»*. Статус в документации — **Experimental**,
с прямым предупреждением *«use caution when shipping with it»*.

**Кто владеет состоянием: `Day Sequence Actor`.** Его свойства (все — дословно из таблицы документации):

| свойство | смысл |
|---|---|
| `Day Sequence Collection` | ассет данных со списком записей |
| `Collection Bias` | пользовательский биас для последовательностей коллекции |
| `Time of Day Preview` | время суток для превью в редакторе |
| `Sequence Update Interval` | как часто применяются визуалы цикла; рекомендованный диапазон 0…0.5 |
| `Run Day Cycle` | идёт ли цикл в рантайме |
| `Day Interp Curve` | *«User-provided interpolation curve that maps day cycle times to desired cycle times (usually from 0 to 24 hours). When disabled, the cycle interpolates linearly»* |
| `Day Length` | длительность суток в игровом времени, по умолчанию 24 ч |
| `Time Per Cycle` | длительность суток в реальном времени, по умолчанию **5 минут** |
| `Initial Time of Day` | стартовое время, по умолчанию **6:00** |

Готовые наследники: `Base Day Sequence Actor`, `Sun Moon Day Sequence Actor` (документация упоминает
и класс `ASunPositionDaySequenceActor`).

**Чем представлено состояние: последовательностями Sequencer, а НЕ набором чисел.** `Day Sequence` —
дословно *«a custom sequence asset with a playback range that can represent a full day / night
cycle»*, «похож на Level Sequence, но диапазон воспроизведения всегда ровно одни сутки». Актор
динамически собирает `Root Sequence` из подпоследовательностей.

**Что он анимирует** — список компонентов из инструкции «с нуля», дословно: два Directional Light
(солнце и луна), Sky Atmosphere, Volumetric Clouds, Sky Light, Exponential Height Fog, Post Process
Volumes; плюс материалы sky sphere. Подпись к иллюстрации Root Sequence прямо говорит про трек,
*«that animates the components of the Day Sequence Actor for things like the volumetric clouds and sky
sphere along with their materials»*.

**Как ведётся переход — вот это главное и вот это переносимо.**

Механизм называется **`Day Sequence Modifier Volume`**: *«This volume can inject procedurally
generated sequences, or user-created sequences into a Day Sequence Actor at runtime. These sequences
can be enabled, disabled, and weighted (to blend with other sub-sequences) dynamically during
gameplay»*. Его свойства:

| свойство | значения |
|---|---|
| `Mode` | `Volume` (вес плавно 0→1 при пересечении границы объёма) или `Global` (вес всегда 1.0) |
| `Blend Amount` | ширина области, где эффективный вес лежит строго между 0 и 1 |
| `Blend Policy` | `Ignored` / `Minimum` (по умолчанию) / `Maximum` / `Override` — как внутренний вес комбинируется с `User Blend Weight` |
| `User Blend Weight` | пользовательский вес |
| `Bias` | иерархический биас (кто кого перекрывает) |
| `Smooth Blending` | сглаживание оценки внутри области смешения; консольная переменная `DaySequence.UpdateIntervalOverride`; в документации помечено *«This can be an expensive option. Use with caution»* |
| `Day / Night Cycle` | `Default` / `Fixed Time` / `Start at Specified Time` / `Random Fixed Time` / `Random Start Time` |

**И вот прямая связь с погодой, единственная в первосторонней документации** — подпись к примеру:
*«Below is an example of a modifier volume changing the time of day and the amount of cloud coverage
with a separately assigned Day Sequence asset»*. То есть Epic сам показывает, что покрытие облаков
меняется через modifier volume — но **как пример возможности, а не как система погоды.** Слова
weather, rain, snow, precipitation на странице нет ни разу (проверено grep'ом по забранному тексту).

**Условная активация — второй переносимый механизм.** `Day Sequence Collection` — *«custom data asset
that has an array of collection entries. A collection entry is composed of a Day Sequence, a bias
offset, and a condition set»*. `Day Sequence Condition Tag` — *«an abstraction of a boolean condition
which can be associated with a Day Sequence»*; `Day Sequence Condition Set` — контейнер «тег →
ожидаемое значение», активный, если **все** теги совпали с ожиданием. Документация оговаривает, что
условия задумывались *«usually scalability settings»*, то есть под качество, а не под погоду; но
структурно это готовый механизм «набор последовательностей, включаемых по состоянию мира».

**`Procedural Day Sequence`** — три штуки в поставке: `Sine Sequence` (анимирует произвольное свойство
синусоидой), `Sun Angle Sequence` (линейно), `Sun Position Sequence` (физически корректно по
географии). Ограничение названо прямо: *«Currently, a procedural day sequence can only be added using
C++»*.

**Вывод по Day Sequence.** Epic решил ровно половину задачи, которую владелец ставит нам: сделал
единого владельца состояния окружения, ассеты-состояния, весовое смешение с политиками и условную
активацию — **но осью взял время суток, а не погоду.** Погода в этот каркас вписывается (условными
тегами и modifier volume'ами), и сообщество именно об этом и спрашивает Epic на форуме
(<https://forums.unrealengine.com/t/how-to-implement-weather-using-the-new-day-sequence-plugin/2278984>,
вопрос «как сделать overcast/raining/snowing разными последовательностями») — но из коробки этого нет.

### 2.4 🟩 Environment Light Mixer — единая панель, но не система

Источник: <https://dev.epicgames.com/documentation/unreal-engine/environment-light-mixer-in-unreal-engine>

*«an editor window where you can create and edit a Level's environment lighting components for sky,
clouds, atmosphere lights, and sky lighting»*; в панели компонентов — Sky Atmosphere, Volumetric
Clouds, до двух Directional Light и Sky Light.

Это **редакторский агрегатор существующих компонентов**, без собственного состояния, без ассета и без
рантайма. Записан здесь, чтобы «в UE есть единое окно окружения» не спутали с «в UE есть система
погоды».

### 2.5 🟦 Twinmotion — у Epic погода ЕСТЬ, но в другом продукте

Источник: <https://dev.epicgames.com/documentation/en-us/twinmotion/ambience-settings>

Это самая ценная находка после Day Sequence, и она меняет формулировку ответа. Не «Epic не умеет
погоду» — **Epic умеет, и в Twinmotion она первосторонняя, просто в Unreal Engine её не вынесли.**
Twinmotion — продукт Epic, построенный на UE, поэтому его панель Ambience показывает, какой набор
контролов Epic считает погодой, когда делает её для художника.

Полный набор (дословные имена и диапазоны из документации):

**Погода и сезон**

| контрол | диапазон | смысл |
|---|---|---|
| `Precipitation` (слайдер) | 0–100 % | *«Increases or decreases the amount and intensity of precipitation (rain or snow) in the scene»* |
| `Precipitation` (тумблер) | вкл/выкл, по умолчанию вкл | недоступен в режиме Path tracer |
| `Season` | непрерывная шкала по сезонам | сезон сцены |
| `Foliage seasonal` | 0–100 % | сезонные эффекты на деревьях: смена цвета, потеря листвы, накопление снега |
| `Brightness boost` | 0.00–1.00 | яркость частиц осадков |
| `Surface effects` | вкл/выкл | эффекты погоды **на поверхностях**: лужи, снег |
| `Wetness` | −1.0…1.0 | количество дождя на поверхностях (только дождь) |
| `Puddle size` | −1.0…1.0 | размер луж (только дождь) |
| `Offset X` / `Offset Y` | −1.0…1.0 | сдвиг луж (только дождь) |
| `Vegetation growth` | 0.00–1.00 | возраст растительности |

**Ветер:** `Enable`, `Speed` 0.00–5.00, `Direction` 0°–360°.

**Туман:** `Enable`, `Density` 0–100 %, `Height` −200.0…200.0 м, цвет.

**Облака:** слайдер облачности от ясного неба до полного покрытия, выбор 2D/Volumetric;
для объёмных — `Clouds preset` (**Small cumulus, Large cumulus, Cirrus, Altocumulus, Cumulonimbus,
Stratus, Nimbostratus** плюс пользовательские), `Random seed`, `Height` 250–4000 м, `Scale` 0.00–1.00,
`Vertical extent` 0–100 %, `Flat bottom` 0–100 %, `Puffiness` 0–100 %, `Density` 0–100 %, цвет,
`Cirrus clouds` 0–100 %.

**Небо (Dynamic):** `Turbidity` 0.0–1.0, `Atmosphere density` 0.0–20.0, `Ambient` 0.00–2.00,
`Moon intensity` 0.00–10.00 люкс, `Stars intensity` 0.0–3.00.

Три вещи, которые отсюда надо забрать:

1. **Осадки у Epic — один слайдер интенсивности плюс переключатель «дождь или снег» через сезон**,
   а не два независимых эмиттера.
2. **Мокрость поверхностей — отдельная ветка контролов** (`Surface effects`, `Wetness`, `Puddle size`),
   то есть Epic развёл «частицы в воздухе» и «след на поверхности» на уровне UI.
3. **Список пресетов облаков Twinmotion почти дословно совпадает с нашей библиотекой из девяти видов**
   (`PLAN_CLOUD_TYPES.md`, T1): cumulus/cirrus/altocumulus/cumulonimbus/stratus/nimbostratus. Наша
   библиотека шире — у нас есть congestus, stratocumulus и линзовидное.

Отдельно: релиз-ноуты Twinmotion 2025.1 сообщают, что *«weather sliders (under Season) have been split
into separate sliders for greater flexibility»* — чтобы можно было получить дождь без облаков или снег
при цветной осенней листве (<https://dev.epicgames.com/documentation/twinmotion/twinmotion-2025-1-release-notes>).
**Это эволюционный урок в чистом виде: Epic шёл от одной оси «погода» к независимым осям и пришёл к
независимым.** Ровно та же дилемма стоит перед нами в §5.

### 2.6 🟦 Fortnite / UEFN — погода как спавнимые VFX, состояния нет

Источник: <https://dev.epicgames.com/documentation/fortnite/using-vfx-spawner-devices-in-fortnite-creative>

Устройство `VFX Spawner` предлагает эффекты с дословными именами `Snow` (*«A gentle sprinkle of
snow»*), `Rain` (*«A steady drizzle of rain»*), `Small Tornado`, плюс `SparkRain` и варианты тумана
(`Fog`, `Area of Fog`, `Light Fog`). Настройки устройства — тип (непрерывный/burst), частота спавна,
фазы, видимость по командам.

**Погодного состояния нет** — это набор независимо размещаемых VFX. Для нас это подтверждение с
третьей стороны: даже в собственной игре Epic погода на уровне контента — это эмиттеры, а не система.

### 2.7 🟥 Маркетплейс — Ultra Dynamic Sky / Ultra Dynamic Weather

Источник: <https://www.ultradynamicsky.com/Documentation/V9/9-5>

**Это не эталон.** Записано, потому что тимлид просил отделить маркетплейс явно, а также потому что
это фактический отраслевой стандарт, и владелец продукта почти наверняка видел именно его, когда
формулировал «система, которая управляет всем».

Архитектура UDW, дословно по документации:

- **Состояние погоды — плоский набор значений.** *«Weather State refers to the set of values which
  determine the weather conditions on UDW»*: `Cloud Coverage`, `Fog`, `Wind Intensity`, `Rain`, `Snow`,
  `Dust`, `Thunder/Lightning`. Отдельно **material state**: `Material Snow Coverage`,
  `Material Wetness`, `Material Dust Coverage` — и документация специально оговаривает, что видимое
  состояние материалов не всегда выводится напрямую из состояния погоды.
- **Пресеты — ассеты данных.** *«The Weather Settings Presets are the preconfigured weather states
  like Thunderstorm, Partly Cloudy, and Blizzard»*, лежат в `Blueprints/Weather_Effects/Weather_Presets`,
  новые делаются дублированием.
- **Переход — функция с длительностью.** `Change Weather`: *«it will immediately start transitioning
  to the new weather preset over the transition length»*.
- **Случайная смена по вероятностям, зависящим от сезона.** `Random Weather Variation` с режимами
  `Random Interval` / `Daily` / `Hourly`, карта `Weather Type Probabilities` (значения
  пропорциональные), `Transition Length` как доля интервала. Есть климатические пресеты по реальным
  климатическим данным.
- **Сезон — отдельная ось, float 0…4** (0 = середина весны, 1 = середина лета), целые значения —
  середины сезонов, дробные — переход. Может выводиться из даты или задаваться извне.
- **Локальность** — `Weather Override Volumes` (регион произвольной формы) и `Radial Storms`
  (круговой регион, видимый снаружи).
- **Иерархия источников состояния** названа явно и упорядочена: пресет в Basic Controls → `Change
  Weather` → случайная вариация → ручное состояние и поштучные оверрайды → объёмы → радиальные штормы.
- **Сохранение**: *«Create UDS and UDW State for Saving»* пакует состояние времени и погоды в одну
  структуру для сейва.

Это, по сути, архитектура, к которой владелец нас и ведёт. Она хороша тем, что уже проверена рынком,
и плоха тем, что мы не можем ни посмотреть в её код, ни сослаться на неё как на эталон.

---

## 3. Контракт параметров, которыми погода обязана управлять

Ниже — то, ради чего задача и ставилась: **поимённые параметры каждой подсистемы UE.** Всё в этом
разделе — 🟩 EPIC/ДВИЖОК, если не помечено иначе.

Оговорка, которую надо сделать один раз: страницы документации Epic по компонентам **числовых
дефолтов почти нигде не дают**. Проверял по Sky Atmosphere, Exponential Height Fog, Sky Light — в
таблицах свойств только описания. Поэтому числа в этом документе не приводятся, кроме тех, что
дословно есть на странице. Наши текущие дефолты (частью взятые из UE в ходе программы Э) лежат в
`ECS/SkyAtmosphereComponent.hpp`, `ECS/ExponentialHeightFogComponent.hpp`,
`ECS/VolumetricCloudComponent.hpp`.

### 3.1 Sky Atmosphere

<https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-properties-in-unreal-engine>

| группа | параметры |
|---|---|
| Планета | `Ground Radius` (км от центра до поверхности), `Ground Albedo` (действует при `MultiScattering` > 0), `Transform Mode` |
| Атмосфера | `Atmosphere Height` (км над поверхностью), `MultiScattering`, `Trace Sample Count Scale` |
| Рэлей | `Rayleigh Scattering Scale`, `Rayleigh Scattering` (коэффициенты на уровне моря), `Rayleigh Exponential Distribution` — *«The altitude in kilometers at which Rayleigh scattering effect is reduced to 40%»* |
| Ми | `Mie Scattering Scale`, `Mie Scattering`, `Mie Scattering Absorption`, `Mie Absorption`, `Mie Anisotropy` (0 — равномерно, ~1 — вперёд с гало), `Mie Exponential Distribution` |
| Поглощение (озон) | `Absorption Scale`, `Absorption`, tent-функция: высота пика, значение плотности, ширина |
| Арт-дирекция | `Sky Luminance Factor`, `Aerial Perspective Distance Scale`, `Height Fog Contribution`, `Transmittance Min Light Elevation Angle`, `Aerial Perspective Start Depth` |

**Что из этого реально двигает погода.** Документация Epic на этот вопрос не отвечает — она описывает
свойства, а не сценарии. Twinmotion (🟦) отвечает: там из всей атмосферы наружу выведены ровно два
контрола, `Turbidity` и `Atmosphere density`. Наш `SkyAtmosphereData` держит полный набор UE.

### 3.2 Volumetric Cloud: компонент и материал

**Компонент** — <https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-component-in-unreal-engine>.
Со страницы дословно подтверждаются: `Ground Albedo` (цвет земли, подсвечивающей облако снизу),
`Trace Sample Count Scale`, секция `Cloud Tracing` со шкалами для View / Reflections / Shadows,
`Reflection Sample Count Scale`, `Shadow Reflection Sample Count Scale`,
`Shadow View Sample Count Scale`, а также узел материала `Volumetric Advanced Material Output`
(октавы multiple scattering, contribution, occlusion, eccentricity; флаги `Ray March Volume Shadow`,
`Ground Contribution`, `Gray Scale Material`).

Смежное на Directional Light: `Cast Cloud Shadows`, `Cloud Shadow Map Resolution Scale`,
`Cloud Shadow Extent`, `Cloud Shadow Ray Sample Count Scale`.
Смежное на Sky Light: `Cloud Ambient Occlusion` + `Strength` / `Extent` / `Map Resolution Scale` /
`Aperture Scale` (<https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-lights-in-unreal-engine>).

**Материал** — уже разобран нами в `EpicDoc_CloudMaterial.md` (источник:
<https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-material-in-unreal-engine>).
Не переоткрываю; дополняю тем, что важно именно погоде.

Погодные ручки материала, в порядке убывания важности для системы погоды:

| параметр | почему он погодный |
|---|---|
| `Layout_GlobalCoverage` | общая облачность. **Биас со знаком**, а не 0..1: положительное добавляет, отрицательное убавляет |
| `Cloud_GlobalDensity` | плотность: выше — плотнее, ниже — мутнее. Это разница между «ясно с кучёвкой» и «пасмурно» |
| `Layout_CloudType` | **видимость каждого из четырёх видов по отдельности (RGBA)** — см. §5, это ключ ко всему |
| `Layout_WindControls` | сила ветра по каждой оси, альфа масштабирует всё разом |
| `Layout_GlobalCloudMask` + `Layout_CloudTypeMask` | локальная погода: маска добавляет/убирает облака в области, второй параметр задаёт, насколько сильно она действует на каждый вид |
| `Layout_CloudPerTypeScale` | масштаб паттерна размещения свой у каждого вида |
| `Layout_CloudGlobalScale` | период повторения текстур раскладки, в километрах |
| `Cloud_AlbedoColor` | цвет; **альфа = сила окклюзии света**, то есть плотность теней |
| `Phase_Controls`, `Multiscatter_Controls` | фазовые функции (−1…1) и multiscatter (0…1) |

**Группа `Storm_*` — дополняю, как просил тимлид.** В `EpicDoc_CloudMaterial.md` §6 она названа и
отложена. Что она означает для погоды:

- `Storm_Clouds` — **бленд в грозовые облака**. Это не «включить дождь», а именно смешение формы в
  грозовую. То есть у Epic переход «ясно → гроза» на уровне облачного материала выражен **отдельным
  параметром смешения**, а не изменением `Layout_CloudType`. Два разных механизма на одну задачу, и
  документация не объясняет, зачем оба;
- `Storm_LightningTexScale`, `Storm_LightningAnim`, `Storm_LightningClouds`
  (Source Power, Fill Scatter, Fill Scatter Intensity, Second Mip Level), `Storm_LightningColor`,
  `Storm_LightningMask` — **молния как подсветка изнутри объёма**, со своей текстурой `VT_Lightning`;
- `Storm_AlbedoColor` — отдельный альбедо грозовых облаков (грозовое облако темнее, и это отдельный
  цвет, а не затемнение общего).

Существенное следствие: **у Epic молния — это свойство ОБЛАКА, а не эффект в воздухе.** Если владелец
захочет грозу, то самая дорогая её часть — свечение внутри объёма — ложится на облачный рендерер, а не
на партиклы.

### 3.3 Exponential Height Fog

<https://dev.epicgames.com/documentation/en-us/unreal-engine/exponential-height-fog-in-unreal-engine>

| группа | параметры |
|---|---|
| основной слой | `Fog Density` (*«global density factor, which can be thought of as the fog layer's thickness»*), `Fog Height Falloff` (как плотность растёт с падением высоты) |
| второй слой | `Second Fog Density`, `Second Fog Height Falloff`, `Fog Height Offset` (*«height offset relative to the Actor's Z height position»*) |
| цвет и непрозрачность | `Fog Inscattering Color`, `Fog Max Opacity` |
| дистанции | `Start Distance`, `Fog Cutoff Distance` |
| направленное рассеяние | `Directional Inscattering Exponent` (размер конуса), `Directional Inscattering Start Distance`, `Directional Inscattering Color` |
| объёмный туман | `Scattering Distribution`, `Albedo`, `Emissive`, `Extinction Scale`, `View Distance`, `Static Lighting Scattering Intensity` |

**Второй слой тумана — это и есть погодный слой.** Он существует ровно затем, чтобы поверх базовой
атмосферы положить туманность/дымку со своей высотой и своим спадом, не трогая основную. Мы это уже
имеем: `SecondFogDensity`, `SecondFogHeightFalloff`, `SecondFogHeightOffset` в
`ECS/ExponentialHeightFogComponent.hpp`.

⬜ **Не нашёл:** объёмный туман (`Volumetric Fog`) у нас в дереве **не реализован** — группа имён
зарезервирована комментарием в нашем заголовке, но полей нет. Для погоды это важно: «туман, в котором
видно лучи и который реагирует на источники света» — это именно объёмный туман, а не exponential
height fog.

### 3.4 Directional Light

Свойства световых шахт (**дословно**, по <https://docs.unrealengine.com/4.27/en-US/BuildingWorlds/LightingAndShadows/LightShafts/>;
актуальная страница <https://dev.epicgames.com/documentation/en-us/unreal-engine/using-light-shafts-in-unreal-engine>
тело в этой сессии не отдала, поэтому цитирую 4.27, где текст полный):

| свойство | описание |
|---|---|
| `Enable Light Shaft Occlusion` | *«Whether to occlude fog and atmosphere in-scattering with screen-space blurred occlusion from this light»* |
| `Occlusion Mask Darkness` | *«Controls how dark the occlusion masking is. A value of 1 results in no darkening term»* |
| `Occlusion Depth Range` | *«Everything closer to the camera than this distance will occlude light shafts»* |
| `Enable Light Shaft Bloom` | *«the color around the light direction will be blurred radially and then added back to the scene»* |
| `Bloom Scale` | *«This will scale the additive color of the bloom»* |
| `Bloom Threshold` | *«Scene color must be larger than this to create bloom in the light shafts»* |
| `Bloom Max Brightness` | *«After exposure is applied, scene color brightness larger than BloomMaxBrightness will be rescaled down»* (по 5.x-версии страницы) |
| `Bloom Tint` | *«Multiplies against scene color to create the bloom color»* |

Плюс погодно-значимое: `Atmosphere Sun Light` (пометка «этот свет — солнце атмосферы»),
`Cast Cloud Shadows`, `Cloud Shadow Extent`, `Cloud Shadow Map Resolution Scale`,
`Cloud Shadow Ray Sample Count Scale`.

Интенсивность и цвет — стандартные `Intensity` (в люксах для направленного света) и `Light Color` /
`Temperature`; угловой размер солнца — `Source Angle`. ⬜ Дословную страницу со свойствами
Directional Light в этой сессии получить не удалось (страница `directional-lights-in-unreal-engine`
отдаёт только оглавление); имена привожу как общеизвестные, но **как цитату их не засчитывать**.

### 3.5 Sky Light

<https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-lights-in-unreal-engine>

`Real Time Capture`, `Source Type` (`SLS Captured Scene` / `SLS Specified Cubemap`), `Cubemap`,
`Source Cubemap Angle`, `Cubemap Resolution`, `Sky Distance Threshold`, `Capture Emissive Only`,
`Lower Hemisphere is Solid Color`, `Lower Hemisphere Color`; в группе Light — `Intensity Scale`,
`Volumetric Scattering Intensity`, `Indirect Lighting Intensity`; в группе Atmosphere and Cloud —
`Cloud Ambient Occlusion` и четыре его параметра.

**Погодная роль Sky Light одна и она критическая:** при `Real Time Capture` он захватывает небо
каждый кадр, поэтому **ambient сцены меняется от облачности сам, без участия системы погоды.** Это
ответ на вопрос «кто затемняет мир, когда набегают тучи»: у Epic — не погода, а перезахват неба.

### 3.6 Ветер

**У UE ветер в движке есть, и он один:** `WindDirectionalSource` /
`UWindDirectionalSourceComponent`
(<https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/WindDirectionalSourceComponent>).

Свойства: `strength`, `speed`, `min_gust_amount`, `max_gust_amount`, `point_wind`, `radius`;
методы `set_minimum_gust_amount` (*«Set minimum deviation for wind gusts»*),
`set_maximum_gust_amount`, `set_radius` (*«Set the effect radius for point wind»*).

**И вот главное ограничение, дословно из документации:**
> *«Component that provides a directional wind source. Only affects SpeedTree assets.»*

То есть **первосторонний ветер UE не двигает ни облака, ни туман, ни осадки, ни ткань, ни волосы.**
Ветер облаков живёт отдельно, в материале, параметром `Layout_WindControls`. Это архитектурный факт,
а не мелочь: **у эталона нет единого ветра.** У нас, кстати, ровно та же расколотость —
`SceneSettings::WindDirection/WindStrength/WindTurbulence` (трава) против
`VolumetricCloudData::WindDirection/WindSpeed` (облака), см. §7.

### 3.7 Осадки

**Чем Epic их делает: Niagara — и на этом всё.**

- 🟨 туториал `nwb3` говорит *«Use Niagara or Cascade to design rain particles»*, без имён систем;
- 🟦 Fortnite: устройство `VFX Spawner` с готовыми эффектами `Rain`, `Snow`, `Small Tornado`;
- 🟦 Twinmotion: осадки встроены, управляются слайдером `Precipitation` 0–100 %, тип (дождь/снег)
  определяется сезоном, есть `Brightness boost` для яркости частиц;
- ⬜ **первосторонней Niagara-системы дождя в поставке UE найти не удалось.** Искал: страницы
  документации Niagara на предмет rain/snow/precipitation, содержимое Content Examples, сэмплы
  City Sample и Electric Dreams. Все найденные рецепты дождя — community-туториалы
  (`5nKZ` «Rain and Thunder», `GPjd` «Create Rain in Unreal Engine 5 with Niagara`) или маркетплейс.

**Как дождь связан с облаками.** ⬜ **Никак, у Epic такой связи в документации нет.** Искал связь
`Volumetric Cloud` → осадки, `Layout_GlobalCoverage` → интенсивность дождя, любые упоминания
«rain under clouds». Не нашёл ни одного. Единственная сторона, где связь описана, — 🟦 Twinmotion, и
там она обратная: релиз-ноуты 2025.1 хвалятся тем, что слайдеры **разделили**, чтобы можно было
получить *«rain without clouds»*.

**Мокрые поверхности и брызги.** ⬜ **Первосторонней системы мокрости в UE нет.** Искал `Wetness`,
`Puddle`, `Wet Surface`, `Rain Ripples` в документации движка — только community-туториалы и
маркетплейс. Есть только в 🟦 Twinmotion: `Surface effects`, `Wetness` (−1…1), `Puddle size` (−1…1),
`Offset X/Y`. И в 🟥 UDW отдельным «material state»: `Material Wetness`, `Material Snow Coverage`,
`Material Dust Coverage`.

**Вывод по осадкам: это единственная подсистема, где у нас нет вообще никакого эталона в движке.**
Здесь UE нам не помощник — придётся решать самим, а сверяться можно только с Twinmotion.

---

## 4. Время суток: связано с погодой или отдельная ось?

**У Epic — отдельная ось, и разведено это очень жёстко.**

🟩 **В движке.** Первосторонний механизм времени суток — `Day Sequence` (§2.3). Его словарь целиком
временной: `Day Length`, `Time Per Cycle`, `Initial Time of Day`, `Time of Day Preview`,
`Day Interp Curve`, `Run Day Cycle`, режимы `Day / Night Cycle` у modifier volume. Слова weather на
странице нет.

Пересечение осей у Epic существует, но не как связь, а как **общая точка приложения**: modifier volume
одновременно меняет время суток и `cloud coverage` — то есть обе оси пишут в одни и те же компоненты
окружения, но друг о друге не знают.

🟨 **В туториале `nwb3`** время суток не упоминается вообще. Сезоны есть — раздел «Seasonal Changes»,
*«Implement a seasonal cycle that affects weather patterns»*, — то есть у автора **сезон влияет на
погоду**, а время суток из рассмотрения выпало.

🟥 **В UDS/UDW** (не эталон, но показательно): время живёт на UDS, погода — на UDW, и связь между
ними односторонняя и через третью сущность — **сезон**. Сезон выводится из даты на UDS и влияет на
вероятностные карты выбора погоды на UDW. Плюс есть погодно-зависимые оверрайды по времени: экспозиция
задаётся *«by the Exposure Bias settings for different time and weather scenarios»* — то есть
двумерной таблицей «время × погода».

**Что из этого следует для нас.** Три оси, а не две: **время суток — погода — сезон.** У Epic первая
реализована (Day Sequence), вторая отсутствует, третья отсутствует. У нас реализована первая
(`TimeOfDayECSSystem` + поля `TimeOfDay`, `DayLengthSeconds`, `Latitude`, `NorthOffset` в
`SkyAtmosphereData`), остальных двух нет.

---

## 5. Смешение состояний — и совпадает ли эталон с нашим D-14

Это раздел, ради которого тимлид отдельно просил «особый интерес». Отвечаю прямо: **совпадает, и
подтверждается с двух независимых сторон.**

### 5.1 Что у Epic интерполируется покомпонентно

**Всё, что является числом на компоненте или скаляром/вектором в материале.** Механизм — Sequencer:
`Day Sequence` анимирует свойства компонентов треками, а `Day Sequence Modifier Volume` смешивает
целые последовательности весами. Веса вычисляются по `Mode` (`Volume`: плавно 0→1 по границе объёма;
`Global`: всегда 1.0) и комбинируются с пользовательским весом по `Blend Policy`
(`Ignored` / `Minimum` / `Maximum` / `Override`), а конфликты разрешаются иерархическим `Bias`.

То есть у Epic смешение — **не «лерп между двумя состояниями», а взвешенное наложение слоёв с
политикой разрешения конфликтов.** Это ближе к тому, как работают Post Process Volume, чем к
переходу между пресетами.

### 5.2 Что НЕ интерполируется

- **Условные записи коллекции.** `Day Sequence Condition Set` — булев предикат: активна запись или
  нет. Промежуточного состояния нет по построению.
- **`Day / Night Cycle` режимы** modifier volume (`Fixed Time`, `Random Fixed Time`, …) — это выбор
  из перечисления, не число.
- 🟥 у UDW явно оговорено, что **material state не выводится напрямую из weather state**: снег на
  поверхности не может «интерполироваться обратно» так же быстро, как перестают падать снежинки. Это
  не про плавность, а про то, что накопленные величины живут своей динамикой.

### 5.3 Виды облаков: эталон отвечает независимыми весами, как и мы

Проблема, ради которой раздел и написан: **набор видов облаков нельзя интерполировать.** Между
stratus и cumulonimbus нет промежуточного облака; среднее их параметров — это не «переходная погода»,
а несуществующая форма.

**Как это решено у Epic** (🟩, источник — <https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-material-in-unreal-engine>,
разобрано нами в `EpicDoc_CloudMaterial.md` §1–§3):

1. Четыре вида закреплены за каналами RGBA: R Stratocumulus, G Altostratus, B Cirrostratus,
   A Nimbostratus.
2. Параметр `Layout_CloudType` задаёт **видимость каждого вида по отдельности** — четыре независимых
   числа, а не одна позиция на шкале видов.
3. `Layout_CloudGlobalPattern` — *«defines the world location for each type of cloud where each
   channel describes a different cloud type»*, то есть **у каждого вида своё поле размещения**.
4. `Layout_CloudPerTypeScale` — масштаб этого поля тоже свой у каждого вида.

**Отсюда прямой ответ на вопрос тимлида.** Погода у Epic не интерполировала бы «вид», потому что
интерполировать нечего: вида как переменной не существует, есть **вектор из четырёх независимых
видимостей, и он интерполируется покомпонентно как обычный `float4`.** Переход «ясно → грозовое небо»
выражается как одновременное убывание R и возрастание A — и в любой момент перехода в небе стоят оба
вида сразу, каждый со своей формой, а не один усреднённый.

**Это ровно наше решение D-14** (`ANALYSIS_APPROACH.md` §7): *«Вес вида при смешении — независимое
поле на вид (своё поле, свой масштаб), а не разбиение одного поля. Объединение остаётся `max`»*.
И наша реализация T3 (`PLAN_CLOUD_TYPES.md`): `CloudType1..4` на компоненте, своё поле размещения на
вид с собственными `PlacementScale` / `PlacementAnisotropy`, объединение через `max`.

**Расхождение ровно одно, и оно в нашу пользу.** У Epic четыре вида — жёсткий структурный потолок
(каналы RGBA трёх текстур). У нас четыре — потолок ширины тексела таблицы профилей, а библиотека на
диске уже насчитывает девять видов и может расти. Плюс у нас `.decloudtype` — файл, который правит
художник, а у Epic вид зашит в раскладку каналов текстуры.

**Второе расхождение, которое надо назвать честно:** у Epic **есть ещё и `Storm_Clouds` — отдельный
параметр «бленд в грозовые облака»** (§3.2), существующий параллельно с `Layout_CloudType` A-каналом
(Nimbostratus). То есть эталон держит **два механизма** перехода в грозу: через независимую видимость
вида и через отдельный бленд-параметр. Зачем оба — документация не объясняет, и я не нашёл источника,
который бы объяснил. **Не выдумываю причину.** Для нас это значит: перед тем как копировать
`Storm_*`, надо понять, что именно он даёт сверх поднятия веса cumulonimbus, — а этого мы пока не
знаем.

### 5.4 Итог по §5

| вопрос | ответ эталона | наше состояние |
|---|---|---|
| чем смешиваются состояния | взвешенное наложение последовательностей Sequencer с политикой (`Blend Policy`, `Bias`) | механизма нет |
| что интерполируется | все числовые свойства компонентов и параметры материала | нечем |
| что не интерполируется | булевы условия (`Condition Set`), перечисления, накопленные величины поверхностей | — |
| как решается непрерывность видов облаков | **вектор независимых видимостей на вид, четыре канала** | **то же самое, D-14 + T3, реализовано** |
| потолок числа видов в одном небе | 4, структурно (RGBA) | 4, ширина тексела; библиотека 9 |

---

## 6. Грабли — что авторы называют неработающим, дорогим или требующим осторожности

Только то, что сказано в источниках дословно.

🟩 **Day Sequence:**
- **статус Experimental**, с прямым предупреждением *«use caution when shipping with it»*. То есть
  первосторонний механизм, к которому мы примеряемся, сам Epic шипить не рекомендует;
- `Smooth Blending` — *«This can be an expensive option. Use with caution»*; он временно понижает
  интервал обновления, что и делает его дорогим;
- `Sequence Update Interval` — компромисс назван численно: воспроизведение подешевле при больших
  значениях, но *«when this value is higher, like around 2, you would see shadows jumping around every
  two seconds»*; рекомендованный диапазон 0…0.5;
- при установке готового `Sun Moon Day Sequence Actor` существующие Directional Light, Sky Light,
  Sky Atmosphere и Volumetric Cloud из уровня **должны быть удалены** — два владельца окружения не
  уживаются. (Нам это знакомо: см. заметку «One SceneRenderer per frame».)
- слайдер `Time` отключается молча в трёх случаях: нет актора, у актора нет коллекции, коллекция
  пуста;
- `Procedural Day Sequence` создаётся **только из C++**.

🟩 **Volumetric Cloud:**
- multiple scattering: *«For games projects, it is recommended to only use a single octave of light
  multiple scattering for performance considerations»* — с оговоркой, что похожего результата можно
  добиться высоким Contribution и низким Occlusion **без затрат**;
- `Reflection Sample Count Scale` и `Shadow Reflection Sample Count Scale` **заклампены**, снимается
  только консольными переменными;
- качество теней облаков на мире упирается сразу в две шкалы — `Trace Sample Count Scale` на облаке и
  `Cloud Shadow Ray Sample Count Scale` на свете.

🟦 **Twinmotion:**
- *«using the Virtual Shadow Maps feature lowers the framerate when snow and rain weather particle
  systems are used»* — осадки конфликтуют с виртуальными теневыми картами. Прямое предупреждение о
  цене осадков от первой стороны;
- `Precipitation` и `Stars intensity` **недоступны в режиме Path tracer**.

🟥 **UDW** (не эталон, но грабли реальны):
- `Cloud Coverage` и `Fog` перестают быть авторскими на UDS, как только в уровне появляется UDW —
  *«these values are controlled as part of the weather state»*. **Классический конфликт двух
  владельцев одного числа**, ровно тот класс дефектов, о котором говорит §2.3.1 нашего контракта;
- анимация в Sequencer: кейфреймить `Cloud Coverage` на UDS **можно только если UDW в сцене нет** —
  иначе надо кейфреймить на UDW;
- порядок применения конфигураций задан жёстко: *«If you need to apply both sky and weather configs at
  once, I recommend applying the weather config first»*.

🟨 **Туториал `nwb3`** называет граблями только общие вещи: профилировать через Unreal Insights,
оптимизировать партиклы и текстуры, применять LOD, следить за коллизиями снежинок. Конкретики нет.

---

## 7. Что это значит для нас

### 7.1 К чему погода подключается в НАШЕМ дереве

Карта точек подключения, все пути от корня репозитория.

**Целевые компоненты (то, чем погода будет управлять):**

| подсистема | файл | что двигать |
|---|---|---|
| Небо | `Desert/Desert/Source/Engine/ECS/SkyAtmosphereComponent.hpp` | `SkyAtmosphereData`: палитра (`ZenithColor`, `HorizonColor`, `GroundColor`), `SkyBrightness`, `SunIntensity`, `SunColor`, и вся физика (`RayleighScattering*`, `Mie*`, `OtherAbsorption*`) при `Model = PhysicalAtmosphere` |
| Туман | `Desert/Desert/Source/Engine/ECS/ExponentialHeightFogComponent.hpp` | `FogDensity`, `FogHeightFalloff`, `FogInscatteringLuminance`, `FogMaxOpacity` и **весь второй слой** (`SecondFogDensity`, `SecondFogHeightFalloff`, `SecondFogHeightOffset`) |
| Облака | `Desert/Desert/Source/Engine/ECS/VolumetricCloudComponent.hpp` | **у нас уже есть категория `Weather`**: `Coverage`, `CoverageContrast`, `WeatherTileSize`; плюс `DensityScale`, `ExtinctionScale`, `CloudType1..4`, `WindDirection`, `WindSpeed` |
| Солнце | `Desert/Desert/Source/Engine/ECS/Components.hpp` (`DirectionalLightData`, стр. 397–479) | `Color`, `Intensity`, `LightShaftBloom`, `BloomScale`, `BloomThreshold`, `BloomMaxBrightness`, `BloomTint` |
| Ветер (трава) | `Desert/Desert/Source/Engine/Core/SceneSettings.hpp` (стр. 295–304) | `WindDirection` (компас, градусы), `WindStrength`, `WindTurbulence` |
| Партиклы | `Desert/Desert/Source/Engine/ECS/Components.hpp` (`ParticleEmitterData`, стр. 556–650) | `Enabled`, `SpawnRate`, `MaxParticles`, `StartSpeed`, `Gravity` |

**Механика, которую можно переиспользовать:**

- `Desert/Desert/Source/Engine/Graphic/WindEnv.hpp` + `SceneRenderer::GetWind()` — **точный шаблон
  канала «данные сцены → рантайм-структура → рендер»**; парный ему `AtmosphereEnv.hpp` +
  `GetAtmosphere()`. `WeatherEnv` + `GetWeather()` ложится сюда один в один;
- `Desert/Desert/Source/Engine/ECS/System/TimeOfDayECSSystem.hpp` — работающий пример системы,
  которая читает данные одного компонента и **пишет в другой** (в трансформ солнца). Система погоды
  будет делать ровно это, только шире; регистрация — `Editor/Source/EditorLayer.cpp`, стр. 765–788;
- `Desert/Desert/Source/Engine/Graphic/SkyPresets.hpp` — X-макрос `DESERT_SKY_PRESET_FIELDS(X)` →
  `struct SkyPresetValues` → таблица `kSkyPresets[]` → `ApplySkyPreset`. **Это буквально готовый
  скелет для «погодного пресета» с покомпонентным лерпом**, и он уже порождается из одного макроса;
- слой ассетов: `Desert/Desert/Source/Engine/Assets/` (`AssetBase`, `AssetManager`,
  `Common.hpp::AssetTypeID`) и живой образец `CloudTypeAsset` / `CloudTypeData` — как делать
  `.deweather`, если решим, что пресет это ассет;
- регистрация компонента и миграция сцен: `Core/Serialize/ComponentRegistry.cpp` (стр. 1078–1091 —
  небо/туман/облака) и `Core/Serialize/SceneMigration.hpp` (сейчас `kSceneVersion =
  kSceneVersionCloudSet = 6`).

### 7.2 Что у нас уже готово

- **Виды облаков как ассеты и независимые веса — сделано и совпадает с эталоном.** Программа T
  закрыта; `CloudType1..4`, своё поле размещения на вид, объединение `max`, девять видов в библиотеке
  `.decloudtype`. Это самая трудная часть погоды, и она позади;
- покрытие, контраст и тайл погоды **уже названы `Weather`** в компоненте облаков — категория
  существует, наполнять её не с нуля;
- второй слой тумана есть;
- шахты света и параметры UE-паритета на направленном свете есть;
- время суток есть, с азимутом по широте и `DayLengthSeconds`;
- ветер для травы есть, с готовым каналом `SceneSettings → WindEnv → SceneRenderer → Grass.shader`;
- GPU-партиклы есть: compute-симуляция + билборды
  (`Graphic/Systems/Scene/Particles/ParticleRenderer.*`).

### 7.3 Чего нет вовсе

- **Погодного состояния** — ни компонента, ни ассета, ни системы. Слова `Weather` как сущности в
  дереве нет: все совпадения — это категория/поля облаков и иконки шрифта;
- **Осадков.** `Rain`, `Precipitation` — **ноль совпадений** по всему дереву. `Snow` есть только как
  слой террейна (`TerrainData::SnowMode`, канал B сплат-карты);
- **Мокрости.** `Wetness` — **ноль совпадений**, ни в материалах, ни в шейдерах;
- **Объёмного спавна у партиклов.** Эмиттер только точечный конусный (`Direction` + `ConeAngle`), нет
  box/volume-спавна, нет коллизий частиц, нет связи партиклов с `WindEnv`. Дождь на этом не сделать
  без расширения;
- **Объёмного тумана.** Группа `Volumetric Fog` зарезервирована комментарием, но не реализована;
- **Общего механизма кривых и интерполяции.** Есть `Common::Math::Interpolate` (только трансформы),
  UI-твины с easing (`UITweenData`, `UIEasing`, `UIAnimTrack` — но это UI), и FSM аниматора с
  cross-fade (но он про позы скелета). Ничего из этого не является рантайм-кривой общего назначения;
- **Сезона** — нет ни в каком виде;
- **Единого ветра.** Их два и они не связаны: `SceneSettings::Wind*` (трава) и
  `VolumetricCloudData::WindDirection/WindSpeed` (облака). Утешение слабое, но эталон расколот так же
  (§3.6): UE-шный `WindDirectionalSource` *«Only affects SpeedTree assets»*, а облака ветрятся
  параметром материала.

### 7.4 Решения, которые тимлиду придётся принять

Не проектирую систему — называю развилки и то, что известно по каждой.

**Решение 1. Чем представлено состояние погоды: плоский набор чисел или таймлайн.**

Развилка настоящая, потому что два источника отвечают по-разному. 🟩 Epic в Day Sequence выбрал
**таймлайн Sequencer** — состояние есть анимация, а смешение есть взвешенное наложение
последовательностей. 🟥 UDW выбрал **плоскую структуру значений** (`Cloud Coverage`, `Fog`,
`Wind Intensity`, `Rain`, `Snow`, `Dust`, `Thunder`) плюс пресеты-ассеты. 🟦 Twinmotion — тоже плоский
набор слайдеров.

Вход в решение с нашей стороны: **у нас нет ни Sequencer-рантайма, ни системы кривых** (§7.3), а
плоская структура с покомпонентным лерпом строится поверх готового `SkyPresets.hpp` за один заход.
Цена выбора таймлайна — построить сначала инфраструктуру, которой нет. Цена выбора плоской
структуры — потерять способность выразить «погода, которая внутри себя развивается по времени»
(нарастающий шторм) иначе, чем внешним драйвером.

**Решение 2. Кто владеет числами — погода или компоненты, и что происходит при конфликте.**

Это тот самый класс дефектов из §2.3.1 контракта, и у обоих источников он проявился. 🟥 У UDW: как
только в сцене появляется UDW, поля `Cloud Coverage` и `Fog` на UDS перестают быть авторскими, и
кейфреймить их в секвенсоре уже нельзя. 🟩 У Epic мягче — modifier volume накладывается слоем с
`Blend Policy` и `Bias`, то есть авторское значение не отбирается, а перекрывается с явным приоритетом.

Вход в решение с нашей стороны: у нас уже есть **работающий прецедент обоих подходов**. Мягкий —
`TimeOfDayECSSystem`, который пишет трансформ солнца **только при `DriveSunFromTimeOfDay == true`**,
то есть владение передаётся явным флагом. Жёсткий — вычисляемая оболочка облачного слоя из T0
(«оболочка вычисляется, а не авторится»). Выбирать надо один и на всю погоду: если поле может быть
переписано погодой, панель свойств обязана это показывать, иначе художник будет крутить ползунок,
который тут же откатывается.

**Решение 3. Осадки — расширять партиклы или строить отдельный проход.**

Здесь эталона нет вовсе (§3.7): у UE нет первосторонних осадков, у Twinmotion они встроены в продукт
и наружу отдаются одним слайдером. Решать полностью самим.

Вход в решение: нашему `ParticleEmitterData` для дождя не хватает объёмного спавна вокруг камеры,
коллизий и связи с ветром — то есть это не «настроить эмиттер», а «добавить в систему частиц три
механизма». Альтернатива — отдельный рендерер осадков по образцу `VolumetricCloudRenderer`, где дождь
не частицы, а экранный эффект. И отдельно от обоих стоит **мокрость поверхностей**: 🟦 Twinmotion и
🟥 UDW оба выносят её в самостоятельную ветку (`Surface effects` / `Material Wetness`), потому что она
живёт по своей динамике — накопилась медленно, высыхает медленно — и покомпонентным лерпом состояния
не выражается.

**Развилка, которую стоит держать в поле зрения, хоть она и не третья по важности: три оси или две.**
Если сезон когда-нибудь понадобится (а 🟨 туториал, 🟦 Twinmotion и 🟥 UDW все три его имеют, причём у
UDW он управляет вероятностями погоды), то вводить его лучше сразу третьей независимой осью, а не
полем внутри погоды. Задним числом расщепить ось дороже — это ровно то, что 🟦 Twinmotion и делал в
2025.1, разделяя слитые слайдеры.

---

## Приложение: полный список источников

**🟩 EPIC / ДВИЖОК**

1. Day Sequence Time of Day Plugin — <https://dev.epicgames.com/documentation/en-us/unreal-engine/day-sequence-time-of-day-plugin-for-unreal-engine>
2. Sky Atmosphere Component Properties — <https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-properties-in-unreal-engine>
3. Exponential Height Fog — <https://dev.epicgames.com/documentation/en-us/unreal-engine/exponential-height-fog-in-unreal-engine>
4. Volumetric Cloud Component — <https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-component-in-unreal-engine>
5. Volumetric Cloud Material — <https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-material-in-unreal-engine> (разобрано в `EpicDoc_CloudMaterial.md`)
6. Sky Lights — <https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-lights-in-unreal-engine>
7. Light Shafts (4.27, полный текст) — <https://docs.unrealengine.com/4.27/en-US/BuildingWorlds/LightingAndShadows/LightShafts/>; актуальная — <https://dev.epicgames.com/documentation/en-us/unreal-engine/using-light-shafts-in-unreal-engine>
8. WindDirectionalSourceComponent (Python API) — <https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/WindDirectionalSourceComponent>
9. Environment Light Mixer — <https://dev.epicgames.com/documentation/unreal-engine/environment-light-mixer-in-unreal-engine>
10. City Sample Project — <https://dev.epicgames.com/documentation/unreal-engine/city-sample-project-unreal-engine-demonstration>

**🟦 EPIC / ДРУГИЕ ПРОДУКТЫ**

11. Twinmotion — Ambience Settings — <https://dev.epicgames.com/documentation/en-us/twinmotion/ambience-settings>
12. Twinmotion 2025.1 Release Notes — <https://dev.epicgames.com/documentation/twinmotion/twinmotion-2025-1-release-notes>
13. Fortnite — Using VFX Spawner Devices — <https://dev.epicgames.com/documentation/fortnite/using-vfx-spawner-devices-in-fortnite-creative>

**🟨 СООБЩЕСТВО**

14. How to Build a Dynamic Weather System, SilkroadLabs, 21.06.2024, UE 5.4 — <https://dev.epicgames.com/community/learning/tutorials/nwb3/unreal-engine-how-to-build-a-dynamic-weather-system> (тело добыто через `https://r.jina.ai/`)
15. Ветка форума с автором и датой — <https://forums.unrealengine.com/t/community-tutorial-how-to-build-a-dynamic-weather-system/1911764>
16. Вопрос «How to implement weather using the new Day Sequence Plugin?» — <https://forums.unrealengine.com/t/how-to-implement-weather-using-the-new-day-sequence-plugin/2278984>

**🟥 МАРКЕТПЛЕЙС (не эталон)**

17. Ultra Dynamic Sky / Ultra Dynamic Weather 9.5 Documentation — <https://www.ultradynamicsky.com/Documentation/V9/9-5>

**⬜ ЧТО ИСКАЛ И НЕ НАШЁЛ**

- первосторонний класс/плагин/подсистема погоды в UE — искал в C++ API Reference (`UWeather*`,
  `AWeather*`, `WeatherComponent`, `WeatherSubsystem`), в release notes 5.5/5.6/5.7, в разделе
  Environmental Light;
- первосторонняя Niagara-система дождя или снега в поставке UE;
- связь «облака → осадки» в документации Epic;
- первосторонняя система мокрости поверхностей (`Wetness`, `Puddle`, `Wet Surface`, `Rain Ripples`);
- реализация погоды в City Sample / Matrix Awakens / Electric Dreams — **проверял только
  документацию; содержимое проектов не скачивал**, поэтому это «не проверено», а не «нет»;
- числовые дефолты свойств Sky Atmosphere, Exponential Height Fog, Sky Light — на страницах Epic их
  нет;
- дословная страница со свойствами Directional Light (`directional-lights-in-unreal-engine`) — отдаёт
  только оглавление.
