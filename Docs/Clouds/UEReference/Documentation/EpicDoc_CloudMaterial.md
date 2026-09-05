# Официальная документация Epic по материалу объёмных облаков — выжимка

Источник: <https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-material-in-unreal-engine>
Забрано 2026-08-19. Публичная документация, цитируется по `UEReference/README.md` §5.

Ценность документа в том, что он описывает **авторскую поверхность глазами художника** — то есть ровно
предмет программы T. Архитектуры шейдера (пины материала, `CloudSampleAttribute`,
`VolumetricAdvancedOutput`, `ConservativeDensity`, пропуск пустоты, стоимость) на странице **нет**;
проверено прямым запросом.

---

## 1. Четыре вида облаков, закреплённые за каналами

Это главное, и это снимает вопрос «сколько видов и как они различаются» у эталона:

| канал | вид |
|---|---|
| R | Stratocumulus |
| G | Altostratus |
| B | Cirrostratus |
| A | Nimbostratus (грозовой) |

Одна и та же раскладка каналов повторяется в **трёх** текстурах — профиль, размещение, маска.

## 2. Три текстуры раскладки, все — на вид по каналам

**`Layout_CloudHeightProfile`** — дословно:

> «This texture describes the shape of the clouds as the altitude changes. Each channel of this texture
> describes the profile shape **and relative altitude** of a different cloud type».

То есть профиль **и высота** вида лежат в канале текстуры, а не в числах компонента. Подтверждает
разбор R1 и решение **D-13** (профиль — таблица, параметрическая кривая — генератор).

**`Layout_CloudGlobalPattern`** — дословно:

> «This texture defines the world location for each type of cloud where each channel describes a
> different cloud type».

**Отдельное поле размещения на каждый вид**, а не разбиение одного поля. Подтверждает решение **D-14**
и, соответственно, опровергает мою первоначальную формулу смешения через `smoothstep`-разбиение.

**`Layout_GlobalCloudMask`** — добавляет или убирает облака в замаскированной области, тоже по каналу
на вид.

## 3. Параметры раскладки

| параметр | смысл |
|---|---|
| `Layout_CloudType` | видимость каждого вида по отдельности (RGBA = четыре вида) |
| `Layout_CloudPerTypeScale` | масштаб паттерна размещения **свой у каждого вида** |
| `Layout_CloudGlobalScale` | период повторения текстур раскладки, **в километрах** |
| `Layout_GlobalTexturePlacement` | смещение и поворот раскладки вокруг мировой оси Z |
| `Layout_WindControls` | сила ветра по каждой оси; альфа масштабирует всё разом |
| `Layout_CloudTypeMask` | насколько сильно маска действует на каждый вид |
| `Layout_GlobalCoverage` | общее покрытие: **положительное добавляет, отрицательное убавляет** — то есть биас, а не ползунок 0..1 |

## 4. Форма облака

| параметр | дословно / смысл |
|---|---|
| `Noise_Texture3D` | «Each color channel describes a different noise pattern»; **альфа не используется** |
| `Noise_Bias` | **вычитается** из выборки: «determines how the noise texture dilates or erodes the Cloud Layout **without increasing the overall contrast**» |
| `Noise_Strength` | **умножается**: «increases or decreases the contrast of the noise» |
| `Noise1_Coordinates` | масштаб и влияние ветра для основного разбиения формы |
| `Noise2_Coordinates` | **«controls distortion amount and scale for Noise1»** |
| `Noise3_Coordinates` | третья октава, включается флагом `UseNoise3` |
| `Cloud_AlbedoColor` | цвет; **альфа = сила окклюзии света**, то есть плотность теней |
| `Cloud_GlobalDensity` | общая плотность: выше — плотнее, ниже — мутнее |

**`Noise2` искажает `Noise1`.** Это независимое подтверждение домен-варпа, который R1 вывел
трассировкой узлов и который был единственным пунктом ревизии плана, не подтверждённым мной лично.
Epic называет тот же механизм своими словами в документации для художника.

Различие `Noise_Bias` / `Noise_Strength` описано Epic ровно так, как я проверил по графу
(`Noise_Bias = (0.5, 0.8, 0.5, 0)` L5482, `Noise_Strength = (0.8, 0.08, 0.03, 2.5)` L5519) — то есть
поправка к `MaterialGraph.md` подтверждена дважды и независимо.

## 5. Multiscatter

- `Phase_Controls`: две фазовые функции, Phase A и Phase B, **диапазон −1…1**, плюс Phase Blend
  (0 = целиком Phase A, 1 = целиком Phase B).
- `Multiscatter_Controls`, **диапазон 0…1**: Intensity (сила эффекта), Occlusion (сила ослабления
  света), Eccentricity (равномерность рассеяния).

## 6. Storm — чего у нас нет и что в объём не берётся

Отдельная группа: `Storm_Clouds` (бленд в грозовые облака), `Storm_LightningTexScale`,
`Storm_LightningAnim`, `Storm_LightningClouds` (Source Power, Fill Scatter, Fill Scatter Intensity,
Second Mip Level), `Storm_LightningColor`, `Storm_LightningMask`, `Storm_AlbedoColor`.

Молния внутри облака — самостоятельная подсистема с собственной текстурой (`VT_Lightning`, она же
единственная НЕзаглушечная текстура в графе). В программу T не входит и здесь записана, чтобы не
всплыть позже как «а UE это умеет».

---

## Что из этого следует для программы T

1. **D-13 и D-14 подтверждены первоисточником**, а не только разбором графа.
2. **Четыре вида одновременно — это потолок эталона, и он структурный**: виды живут в каналах RGBA
   трёх текстур. Наш T3 говорит «2–4 вида» — совпадает, и теперь у этого числа есть причина, а не
   вкус. Библиотека на диске при этом может быть любого размера; ограничение на то, сколько видов
   участвует в ОДНОМ небе.
3. **Масштаб размещения свой у каждого вида** (`Layout_CloudPerTypeScale`) — у нас один тайл погоды на
   весь слой. Это входит в T3 вместе с полем на вид.
4. **Покрытие у Epic — биас со знаком.** Наш порог 0..1 остаётся по D-15 (у него свой несущий вход —
   измеренный дефект вуали), но расхождение теперь названо.
