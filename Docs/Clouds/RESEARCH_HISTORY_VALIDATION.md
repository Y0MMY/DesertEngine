# Исследование: четыре проверки истории UE, которых нет в `CloudTemporalResolve`

Задача HV, этап «исследование». Автор: разработчик программы «Небо и облака».
Дата: 2026-08-25.

Повод — долг, записанный в `REVIEW_622a01a6.md` (пункт Ц1) и продублированный в шапке
`Editor/Resources/Shaders/Programs/Clouds/CloudTemporalResolve.shader:60-65`:

> WHAT UNREAL VALIDATES AND THIS PASS DOES NOT […]: a minimum reprojection distance (`:299`), the whole
> min/max-depth family (`:402-427`, `:480`), the eight-neighbour DILATION toward the closest scene depth
> (`:543-558`) and the optional neighbourhood colour box (`:570+`).

Документ отвечает на четыре вопроса тимлида. **Каждое утверждение помечено уровнем доверия:**

| метка | что значит |
|---|---|
| **[ИСХ]** | прочитано в исходнике UE, файл и строка названы |
| **[ВЫВ]** | выведено из двух и более мест исходника; цепочка вывода приведена целиком |
| **[НАШ]** | прочитано в нашем исходнике, файл и строка названы |
| **[НЕТ]** | не установлено; сказано, чего именно не видно |

---

## 0. Версия, по которой считаны строки

`Docs/Clouds/UEReference/Source/README.md` фиксирует срез: `EpicGames/UnrealEngine`, ветка `release`,
коммит **`71fe36aac5a8df5ccd66c763ffc902b29b6a9c43`** (2026-07-28). Каталог `Source/` в `.gitignore`
(строка 118) по условиям тикета #UE-2026-0842-WAIVER — то есть **он есть на диске и его нет в истории**,
ровно как `m_SimpleVolumetricCloud.graph.txt` (`.gitignore:113`), на котором уже ошибся разработчик PT.
Проверено `ls`, а не `git log`: оба файла на месте.

Чтобы номера строк не зависели от того, у кого какая выгрузка, файлы забраны заново через
`gh api repos/EpicGames/UnrealEngine/contents/…?ref=release` и **сверены побайтово** с зафиксированным
срезом:

| файл | результат `diff` |
|---|---|
| `Engine/Shaders/Private/VolumetricRenderTarget.usf` | идентичен |
| `Engine/Source/Runtime/Renderer/Private/VolumetricRenderTarget.cpp` | идентичен |

`VolumetricCloud.usf` и `VolumetricCloudRendering.cpp` забраны тем же способом и используются ниже; их
строки относятся к тому же срезу.

**Граф материала (`m_SimpleVolumetricCloud.graph.txt`) к этой задаче отношения не имеет и не читался:**
временная реконструкция целиком движковая, материал в неё не входит ни одним узлом. Это утверждение
проверяемо: в `VolumetricRenderTarget.usf` нет ни одного обращения к материалу — файл не включает
`MaterialTemplate.ush` и не объявляет `FMaterialPixelParameters`. **[ИСХ]**

---

## 1. Первое, что меняет постановку: четыре проверки НИКОГДА не работают вместе

Проход реконструкции — одна функция `ReconstructVolumetricRenderTargetPS`
(`VolumetricRenderTarget.usf:249`). Внутри неё ровно один `#if`, который делит тело надвое:

```
:398  #if PERMUTATION_CLOUD_MIN_AND_MAX_DEPTH      ← проверки №2 (:402-427, :480)
:515  #else // PERMUTATION_CLOUD_MIN_AND_MAX_DEPTH ← проверки №3 (:543-558) и №4 (:570+)
:621  #endif
```
**[ИСХ]**

То есть **№2 и {№3, №4} исключают друг друга на уровне препроцессора.** Ни в одной сборке UE все четыре
не выполняются. №1 (`:299`) — общая для обеих ветвей, она вычисляется до развилки.

Какая ветвь берётся, решает C++:

```
VolumetricRenderTarget.cpp:841   const bool bMinMaxDepth = ShouldVolumetricCloudTraceWithMinMaxDepth(ViewInfo);
VolumetricRenderTarget.cpp:846   PermutationVector.Set<…FCloudMinAndMaxDepth>(bMinMaxDepth);

VolumetricCloudRendering.cpp:348-356
    return ShouldViewRenderVolumetricCloudRenderTarget(ViewInfo)
        && ShouldUseComputeForCloudTracing(ViewInfo.FeatureLevel)
        && VRT.GetMode() == 0;
```
**[ИСХ]**

`r.VolumetricRenderTarget.Mode` по умолчанию **0** (`VolumetricRenderTarget.cpp:32-35`), а
`ShouldUseComputeForCloudTracing` (`VolumetricCloudRendering.cpp:287-299`) истинна везде, где не выключен
`r.VolumetricCloud.DisableCompute`, формат `PF_FloatRGBA`/`PF_G16R16F` поддерживает typed UAV load и не
включён MSAA. **[ИСХ]**

> ### Вывод, который меняет объём задачи
>
> Наш проход — **compute, режим 0**, это записано в его собственной шапке
> (`CloudTemporalResolve.shader:5`) и подтверждается кодом: `DispatchComputeInFrame`,
> `VolumetricCloudRenderer.cpp:1236`. **[НАШ]**
>
> Значит наш архитектурный аналог — ветвь `PERMUTATION_CLOUD_MIN_AND_MAX_DEPTH`. Проверки **№3
> (дилатация) и №4 (box-constraint) живут в ветви, которую конфигурация, которую мы копируем, не
> компилирует вообще.** Это не довод «не переносить» сам по себе — но это довод не называть их
> «недостающими у нас относительно эталона»: у эталона в нашем режиме их тоже нет. **[ВЫВ]**

---

## 2. Проверка №1 — минимальная дистанция репроекции

### Что это и что отвергает

```hlsl
// VolumetricRenderTarget.usf:286
float TracingVolumetricSampleDepthKm = SafeLoadTracingVolumetricDepthTexture(...).CloudFrontDepthFromViewKm;
...
// :298-301
const bool bValidPreviousUVs = all(PrevScreenUVs > 0.0) && all(PrevScreenUVs < 1.0f)
    && (TracingVolumetricSampleDepthKm >= MinimumDistanceKmToEnableReprojection);
```
**[ИСХ]**

Отвергается **не история, а сама репроекция**: если облачная поверхность ближе порога, `bValidPreviousUVs`
ложно, и пиксель уходит в ветвь `else // !bValidPreviousUVs` (`:500-507` / `:613-619`) — билинейная
выборка трассировки ЭТОГО кадра. То есть это **третий случай нашей шапки**, а не «отвергнутая история».

### Что происходит с отвергнутым пикселем

История **не читается вовсе** (одно `&&` в том же выражении, что и off-screen), и пиксель получает
билинейную реконструкцию четвертьразрешённой трассировки. Вес истории не падает — истории просто нет.
**[ИСХ]**

### Чем оправдано у Epic

Дословно, `:299-301` и `VolumetricRenderTarget.cpp:64`:

> This helps hide reprojection issues due to imperfect approximation of cloud depth as a single front
> surface, especially visible **when flying through the cloud layer**. It is not perfect but will help in
> lots of cases. The problem when using this method: clouds will look **noisier** when closer to that
> distance.

Артефакт: облако репроецируется как ОДНА фронтальная поверхность; вблизи угловой размер ошибки этого
приближения растёт как 1/расстояние, и история приезжает не туда. Плата названа самим Epic — шум.

### ⚠️ Значение по умолчанию — НОЛЬ, то есть у Epic проверка выключена

```
VolumetricRenderTarget.cpp:62-65
    TEXT("r.VolumetricRenderTarget.MinimumDistanceKmToEnableReprojection"), 0.0f,
    TEXT("… One could start with a value of 4km. …")
```
**[ИСХ]**

Значение 0 делает условие `TracingVolumetricSampleDepthKm >= 0.0f` тождественно истинным для любой
неотрицательной дистанции. **В стоковом UE эта проверка не срабатывает никогда**; Epic предлагает 4 км
как то, с чего можно НАЧАТЬ, если проблема наблюдается. **[ВЫВ]**

---

## 3. Проверка №2 — семейство min/max-глубины

### ⚠️ Первое: это НЕ глубина облака. Это глубина СЦЕНЫ

`REVIEW_622a01a6.md` называет их «min/max глубины облака», и то же слово стоит в шапке нашего шейдера
(`:63-64`, «a min/max cloud depth»). **Это неверно.** Пишет их марш:

```hlsl
// VolumetricCloud.usf:594-606, ветвь CLOUD_MIN_AND_MAX_DEPTH
float2 MinAndMaxDepth = …SceneDepthMinAndMaxTexture.Load(…).rg;
MinAndMaxDepth = max(1e-12, MinAndMaxDepth.yx);            // x — ближняя, y — дальняя
const float TClosestDepthBufferKm  = length(SvPositionToTranslatedWorld(…MinAndMaxDepth.x…) - Origin);
const float TFurthestDepthBufferKm = length(SvPositionToTranslatedWorld(…MinAndMaxDepth.y…) - Origin);
OutDepth.xyzw = float4(TFurthestDepthBufferKm, TFurthestDepthBufferKm,
                       TClosestDepthBufferKm,  TFurthestDepthBufferKm);
```
**[ИСХ]**

`SceneDepthMinAndMaxTexture` — это `HalfResolutionDepthCheckerboardMinMaxTexture`
(`VolumetricRenderTarget.cpp:882`), то есть **минимум и максимум Z-буфера непрозрачной геометрии** в
блоке, накрытом одним текселем четвертьразрешённой трассировки. Ни одна из четырёх компонент не
описывает облако, кроме `.x`, которая позже перезаписывается:

```hlsl
// VolumetricCloud.usf:1728-1732
OutDepth.x = MaxHalfFloat;
OutDepth.x = ((GrayScaleTransmittance > 0.99) ? NoCloudDepth : tAP) * CENTIMETER_TO_KILOMETER;
```
**[ИСХ]** — и это **средневзвешенная по пропусканию** глубина `tAP` (`:1493`), а не первое попадание.
Наш `.x` — именно первое попадание (`CloudRaymarch.shader:56-59` **[НАШ]**). Расхождение зафиксировано
здесь как факт; на выводы этой задачи оно не влияет.

Итог по семантике: `FDepthData` = { `.x` глубина облака (средневзвешенная), `.y` = `TFurthest`,
`.zw` = (`TClosest`, `TFurthest`) }, все три «глубины» кроме `.x` — глубины **непрозрачной сцены**.
**[ВЫВ]**

### Что это и что отвергает

Ворота (`:402-403`):

```hlsl
const bool bApplyDisoclusion =
       any(NewDepthData.MinMaxViewDepthKm     < MinimumDistanceKmToDisableDisoclusion)
    || any(HistoryDepthData.MinMaxViewDepthKm < MinimumDistanceKmToDisableDisoclusion)
    || NewDepthData.SceneDepthFromViewKm      < MinimumDistanceKmToDisableDisoclusion
    || HistoryDepthData.SceneDepthFromViewKm  < MinimumDistanceKmToDisableDisoclusion;
```
Порог `MinimumDistanceKmToDisableDisoclusion` — **5.0 км по умолчанию**
(`VolumetricRenderTarget.cpp:67-70`). Смысл ворот в комментарии `:400-401`: если ВСЯ геометрия дальше 5 км,
облако — слой поверх фона, краёв нет, дизокклюзия не нужна. **[ИСХ]**

Внутри ворот — три условия, каждое ставит `bUseNewSample = true`:

| строка | условие | комментарий Epic |
|---|---|---|
| `:408-414` | `abs(MinMax.x − MinMax.y) > 2 км` И `abs(MinMax.x − Scene) < abs(MinMax.y − Scene)` | «If there is a huge delta of depth for a pixel, use the new sample but only if we are on the closest depth part. **This helps a lot removing cloud over trees and small details**» |
| `:416-421` | `HistoryScene < NewScene − 2 км` | «History is closer than the near cloud tracing this frame. This means an object had move and an new disocluded area is discovered» |
| `:423-427` | `HistoryScene > NewScene + 2 км` | «An area that just go covered (history is invalid because occluded)» |

Порог `ThresholdToNewSampleKm = 2.0` — литерал с пометкой Epic `// Arbitrary` (`:406`). **[ИСХ]**

Четвёртое, отдельное, вне ворот и уже ПОСЛЕ чтения истории (`:476-485`):

```hlsl
// Ignore reprojected samples with too large of a min depth difference that have been missed by
// dissoclusion. … This is an attempt to fix missing cloud over sky background around close moving meshes.
if (abs(NewDepthData.MinMaxViewDepthKm.x - HistoryDepthData.MinMaxViewDepthKm.x) > 1.0f)
{
    ReprojRGBA = NewRGBA; ReprojRGBA2 = NewRGBA2; ReprojDepthData = NewDepthData;
}
```
**[ИСХ]**

### ⚠️ ВТОРОЕ: условие `:408-414` НЕДОСТИЖИМО. Это, судя по всему, ошибка Epic

Цепочка вывода, целиком:

1. `NewDepthData` читается из `TracingVolumetricDepthTexture` точечно (`:389-391`), то есть это
   ровно тот `OutDepth`, который записал марш (`VolumetricCloud.usf:1841`). **[ИСХ]**
2. В ветви `CLOUD_MIN_AND_MAX_DEPTH` марш пишет `.y = TFurthestDepthBufferKm` и
   `.w = TFurthestDepthBufferKm` — **одно и то же выражение, один и тот же float**
   (`VolumetricCloud.usf:606`). **[ИСХ]**
3. Дальше перезаписывается только `.x` (`:1728-1731`); `.y`, `.z`, `.w` не трогаются. **[ИСХ]**
4. На путях ранних выходов `OutDepth = MaxHalfFloat` для всех четырёх (`:481`) — равенство `.y == .w`
   сохраняется. **[ИСХ]**
5. `FixupDepthDataSafe` (`VolumetricRenderTarget.usf:237-247`) применяет к `.y` и к `.w` **одно и то же**
   правило `> 60000 ? MaxHalfFloat : x` покомпонентно — равенство сохраняется. **[ИСХ]**
6. Следовательно `NewDepthData.MinMaxViewDepthKm.y == NewDepthData.SceneDepthFromViewKm` **побитово**,
   всегда, в этой ветви. **[ВЫВ]**
7. Тогда второй сомножитель `:409` есть `abs(MinMax.x − Scene) < abs(MinMax.y − Scene)` =
   `abs(TClosest − TFurthest) < 0.0`, а `x < 0` для `x = abs(...)` ложно всегда. **[ВЫВ]**

**Значит ветвь `:408-414` — «убрать облако с деревьев и мелких деталей» — не выполняется ни на одном
пикселе.** Живыми в семействе остаются `:416`, `:423` и `:480`.

Уровень доверия: **[ВЫВ]**, цепочка из шести прочитанных мест. Запуском UE не проверялось и проверено
быть не может — у нас нет собранного UE. Если у Epic `.y` когда-то нёс что-то другое, это ошибка
рефакторинга, а не наша ошибка чтения: шесть строк выше говорят сами за себя.

### Что происходит с отвергнутым пикселем

- `:408-427`: `bUseNewSample = true` → `:454-460` берёт **точечно загруженный** сэмпл этого кадра.
  История не читается. Вес не падает — она заменяется целиком.
- `:480`: история УЖЕ прочитана и **выбрасывается**, на её место кладётся сэмпл этого кадра
  (и цвет, и вторичный цвет, и вся `FDepthData`). **[ИСХ]**

### Чем оправдано

`:412` «removing cloud over trees and small details», `:418` движущийся объект открыл новый участок,
`:425` участок закрылся, `:477-478` «missing cloud over sky background around close moving meshes».
**Все четыре — про непрозрачную геометрию, движущуюся близко к камере.** **[ИСХ]**

---

## 4. Проверка №3 — дилатация по восьми соседям

Ветвь `#else`, то есть **не наш режим** (см. §1).

```hlsl
// VolumetricRenderTarget.usf:536-538
const float ReconstructDepthZ = HalfResDepthTexture.Load(int3(SVPos.xy + ViewViewRectMin, 0)).r;
const float3 TranslatedWorldPosition = SvPositionToTranslatedWorld(float4(CenterSample, ReconstructDepthZ, 1.0));
const float PixelDistanceFromViewKm = length(TranslatedWorldPosition - …CameraOrigin) * CENTIMETER_TO_KILOMETER;
// :543
if (abs(PixelDistanceFromViewKm - DepthData.SceneDepthFromViewKm) > PixelDistanceFromViewKm * 0.1f)
```
**[ИСХ]**

Отвергает историю, чья глубина сцены разошлась с **полуразрешённой глубиной сцены этого кадра** больше
чем на **10 % от дистанции** — единственный ОТНОСИТЕЛЬНЫЙ порог во всём проходе.

### Что происходит с отвергнутым пикселем — здесь единственный раз НЕ «взять новый сэмпл»

`:546-558`: цикл по восьми соседям в четвертьразрешённой трассировке; выбирается тот, чья
`SceneDepthFromViewKm` ближе всего к `PixelDistanceFromViewKm`, и берутся **его** цвет и глубина. То есть
пиксель **подменяется соседом**. Центральный сэмпл в конкурс не входит: закомментированный блок
`:559-564` («After more testing, the code below looked unecessary») — это именно попытка добавить его.
**[ИСХ]**

### Чем оправдано

`:545` «History has a too large depth difference at depth discontinuities, use the data with closest depth
within the neightborhood». Артефакт — разрыв глубины: силуэт близкой геометрии, вдоль которого история
принадлежит другой поверхности. **[ИСХ]**

---

## 5. Проверка №4 — box-constraint по окрестности

Ветвь `#else` (не наш режим) И `#if PERMUTATION_REPROJECTION_BOX_CONSTRAINT` (`:570`), И — только в
`else` от дилатации, то есть **лишь для пикселей, которые проверку №3 прошли**. **[ИСХ]**

```
VolumetricRenderTarget.cpp:57-60
    TEXT("r.VolumetricRenderTarget.ReprojectionBoxConstraint"), 0,
```
**Выключено по умолчанию.** **[ИСХ]**

### Что делает

`:572-592` строит AABB цвета (`float4`, то есть RGB и пропускание) и AABB пары глубин по восьми соседям
ПЛЮС центральному новому сэмплу; `:595-600` зажимает историю в этот бокс.

### Что происходит с отвергнутым пикселем

Единственная из четырёх, которая **не отвергает, а ЧИНИТ**: история остаётся, но покомпонентно
зажимается. Ни `bUseNewSample`, ни подмены соседом. **[ИСХ]**

### Два признака незаконченности, названные прямо

1. `:576, :586, :592, :593` — флаг `bApply` вычисляется (все восемь соседей и новый сэмпл должны иметь
   глубину сцены > 1000 км), но `if (bApply)` **закомментирован**, и зажим применяется всегда. **[ИСХ]**
2. `:569` — `// TODO: To use this, we need to make sure we prioritise pixe lwith under represented depth.`
   **[ИСХ]**

Ещё один box-constraint, полностью закомментированный, лежит в НАШЕЙ ветви на `:467-474` с объяснением
Epic: «this is too much constraint to use only the new sample as it **causes jittering**». **[ИСХ]**

### Чем оправдано

`:571` «Make sure that history stay in the neightborhood color/transmittance/depth box after reprojection» —
классический TAA neighbourhood clamp против гостинга. Именно его наш шейдер называет нереализованным в
`CloudTemporalResolve.shader:164-165`. **[НАШ]**

---

## 6. Порядок и взаимодействие — сводная таблица

Порядок выполнения в UE, ветвь `CLOUD_MIN_AND_MAX_DEPTH` (наш аналог):

```
:298  bValidPreviousUVs = onScreen && (frontDepth >= MinimumDistanceKmToEnableReprojection)   ← №1
:395  FixupDepthDataSafe(New), FixupDepthDataSafe(History)
:402  ворота bApplyDisoclusion (порог 5 км)
:408    ├ недостижимо                                          → bUseNewSample = true   ← №2a
:416    ├ история ближе на 2 км                                → bUseNewSample = true   ← №2b
:423    └ история дальше на 2 км                               → bUseNewSample = true   ← №2c
:454  if (bUseNewSample)          → точечный сэмпл этого кадра
:461  else if (bValidPreviousUVs) → читаем историю
:480      └ |ΔMinMax.x| > 1 км    → выбрасываем прочитанную историю, кладём новый сэмпл  ← №2d
:491      └ не-конечное           → чёрное, непрозрачное, 1000 км
:500  else                        → билинейная выборка этого кадра
```

**Ответ на вопрос «все четыре или первая сработавшая»: НИ ТО, НИ ДРУГОЕ.** Устройство трёхуровневое:

1. **№1 — вентиль**, вычисляется раньше всех и решает, будет ли история читаться вообще. Он в одном
   выражении с off-screen, то есть первая сработавшая из этих двух выигрывает и обе дают один исход.
2. **№2a–2c — три условия одного `if/else if/else if`**, то есть **первая сработавшая выигрывает**, и
   исход у всех трёх один и тот же (`bUseNewSample`). Порядок между ними ненаблюдаем.
3. **№2d и NaN-проверка — последовательные, применяются обе**, уже после чтения истории, и обе могут
   сработать на одном пикселе (сначала подмена на новый сэмпл, потом обнуление, если новый сэмпл сам
   не-конечен).

В ветви `#else`: **№3 и №4 взаимоисключающие** (`if`/`else` на `:543`/`:567`), причём №4 достаётся только
тем, кого №3 не тронула. **[ИСХ]**

---

## 7. Что каждая проверка ловила бы на НАШЕМ материале

Это вторая половина задачи, и она отделена от §§2-5 намеренно: выше — что написано у Epic, ниже — что из
этого вообще применимо к нам. Ни одна строка ниже не является основанием писать код; основанием будет
измерение в `CALIBRATION.md §HV`.

### Различие архитектур, из которого всё следует

| | UE | мы |
|---|---|---|
| поле | материал ЕСТЬ поле, вычисляется на каждом шаге марша | поле **испечено** заранее (`CloudProceduralVolume`) |
| гид | `.x` средневзвешенная по пропусканию, `.y` дальняя глубина сцены, `.zw` (ближняя, дальняя) глубина сцены | `.x` первое попадание, `.y` глубина сцены, `.zw` = 0 **[НАШ]** `CloudRaymarch.shader:53-69` |
| глубина сцены в резолве | есть полуразрешённая (`HalfResDepthTexture`) | **нет**, резолв её не получает **[НАШ]** |
| смешивание | **его нет** — UE ВЫБИРАЕТ (новый / история / билинейка) | `mix(history, trace, w)`, `w` = 0.75 / 0.06 **[НАШ]** `CloudTemporalResolve.shader:170-171` |

Последняя строка важнее остальных: у UE «отвергнуть историю» и «взять новый сэмпл» — одно и то же
действие, потому что третьего значения нет. У нас отвергнутая история означает `resolved = traceScatter`
без смешивания, то есть **потерю накопления**, а не переключение между двумя равноправными оценками.
Цена ложного срабатывания у нас поэтому ВЫШЕ, чем у Epic. **[ВЫВ]**

### Гипотезы, подлежащие проверке (не выводы)

| проверка | что ловила бы у нас | чем это меряется |
|---|---|---|
| №1 мин. дистанция | пролёт камеры сквозь слой: `.x` мал, приближение «одна фронтальная поверхность» разваливается | кадры с камерой ВНУТРИ слоя и на подлёте; `ImageStat`/`LineJump` на серии |
| №2 min/max сцены | требует канала, которого у нас нет. Но у семейства есть подмножество, которое НЕ требует: `:416`/`:423` сравнивают `SceneDepth` истории и этого кадра — а `.y` у нас есть. **Это ровно то, что у нас уже реализовано** (`CloudTemporalResolve.shader:283`, симметричная форма пары). Недостаёт только min/max внутри тексела | сцена с движущейся близкой геометрией перед облаками |
| №3 дилатация | требует полуразрешённой глубины сцены в резолве. Ловит разрывы глубины на силуэтах непрозрачной геометрии | та же сцена |
| №4 box-constraint | гостинг и «липкость» истории на движении. **Канала не требует** — только соседей по трассировке этого кадра | движущаяся камера + движущиеся облака, разница кадров |

### ⚠️ Предварительный отбор: две из четырёх упираются в отсутствующий канал, а не в отсутствующий код

№2 (в полном виде) и №3 требуют, чтобы марш писал то, чего он не пишет. Это меняет цену: не «одно
сравнение в резолве», а новый канал гида плюс новая запись в марше плюс сопровождающая стоимость в
`Clouds: March`. Такой перенос обязан быть оправдан артефактом на НАШЕМ материале, а не наличием у
эталона.

№1 и №4 реализуемы в резолве без нового канала.

**Ни одна из четырёх не переносится, пока §HV `CALIBRATION.md` не покажет артефакт кадром или числом.**
Контракт запрещает мёртвые настройки (§1.3), и мёртвая проверка — та же мёртвая настройка.

---

## 8. Чего не видно, и это сказано прямо

- **[НЕТ]** Не установлено, на каком материале Epic наблюдал каждый артефакт: комментарии называют
  симптом («cloud over trees», «close moving meshes»), но не сцену, не настройки и не числа.
- **[НЕТ]** Не установлено, почему `:408` в текущем срезе недостижим: истории коммитов этого файла у нас
  нет (`gh api …/commits` по приватному репозиторию в объём выгрузки не входил), поэтому нельзя сказать,
  была ли это работающая проверка, сломанная рефакторингом, или она никогда не работала.
- **[НЕТ]** Не установлено, каков практический эффект `HistoryPreExposureCorrection`
  (`VolumetricRenderTarget.usf:160`) — множитель истории при смене экспозиции. У нас автоэкспозиции в
  облачных сценах нет, но это НЕ проверено измерением, только чтением `Clouds_Demo.desce`
  (`"AutoExposure":false`).
- **[НЕТ]** Не проверено запуском UE ни одно утверждение этого документа. Все они — чтение исходника.
