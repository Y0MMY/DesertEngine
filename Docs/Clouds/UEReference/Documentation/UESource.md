# Сверка находок Ц1, Ц2, Ц3, Ц6 с исходником Unreal Engine

Задача **R2** программы «Облака». Отвечает на четыре находки ревью `622a01a6`
(`Docs/Clouds/REVIEW_622a01a6.md`), которые невозможно закрыть изнутри нашего дерева.

Основание доступа к коду Epic — тикет **#UE-2026-0842-WAIVER**, `Docs/Clouds/LICENCE_RECORD.md`.
Фрагменты приведены минимально достаточными: только там, где спор идёт о порядке операций.

## Версия, к которой относятся все номера строк

| | |
|---|---|
| Репозиторий | `EpicGames/UnrealEngine`, ветка `release` |
| Коммит | `71fe36aac5a8df5ccd66c763ffc902b29b6a9c43` |
| Дата коммита | `2026-07-28T12:02:51Z` |

Исходники лежат в `Docs/Clouds/UEReference/Source/` с сохранённой раскладкой UE и **в git не
попадают** (`.gitignore:118`). Полный состав и контрольные размеры — в
`Docs/Clouds/UEReference/Source/README.md`. Ниже пути пишутся коротко: `VolumetricCloud.usf` — это
`Source/Engine/Shaders/Private/VolumetricCloud.usf`, `VolumetricRenderTarget.cpp` — это
`Source/Engine/Source/Runtime/Renderer/Private/VolumetricRenderTarget.cpp`.

## Сводка вердиктов

| Находка | Вердикт |
|---|---|
| **Ц2** октавы multiple scattering | **Наш код верен.** Ревью ошиблось в разборе; документ §2.1 формулирует верно, но двусмысленно. **Отдельно найден настоящий дефект в той же строке кода: множитель фазы.** |
| **Ц1** что валидирует реконструкция | **Наш код верен в выборе канала** — UE тоже не сравнивает фронт облака между историей и кадром. **Неверен комментарий**, обещающий полноту: у UE шесть проверок, у нас три. |
| **Ц3** как читается depth guide | **Наш код неверен.** UE читает свой guide точечно и никогда не пишет билинейку по четвертьразрешению в штатном пути. |
| **Ц6** таблица обхода субпикселей | **Таблица верна дословно.** Обоснования в UE нет вообще; наш комментарий неверен. |

---

# Ц2. Октавы multiple scattering

## Что делает UE

`VolumetricCloud.usf:339-343` — заголовок механизма:

```hlsl
// Multi scattering approximation based on http://magnuswrenninge.com/.../Wrenninge-OzTheGreatAndVolumetric.pdf
// 1 is for the default single scattering look. Then [2,N] is for extra "octaves"
#ifndef MSCOUNT
#define MSCOUNT (1 + MATERIAL_VOLUMETRIC_ADVANCED_MULTISCATTERING_OCTAVE_COUNT)
#endif
```

Фактический цикл — `VolumetricCloud.usf:387-399`, внутри
`SetupParticipatingMediaContext` (объявлена на `VolumetricCloud.usf:374`):

```hlsl
	UNROLL
	for (int ms = 1; ms < MSCOUNT; ++ms)
	{
		PMC.ScatteringCoefficients[ms] = PMC.ScatteringCoefficients[ms - 1] * MsSFactor;
		PMC.ExtinctionCoefficients[ms] = PMC.ExtinctionCoefficients[ms - 1] * MsEFactor;
		MsSFactor *= MsSFactor;
		MsEFactor *= MsEFactor;
```

Несущее здесь — **два разных действия в одном шаге**: коэффициент домножается на текущий множитель
(`[ms-1] * MsSFactor`), и **только потом** сам множитель возводится в квадрат.

Разворачиваем при `MsSFactor = c`:

```
ms = 0:  S[0] = S                               (множитель ещё не применялся)
ms = 1:  S[1] = S[0] * c   = S·c                ; c -> c²
ms = 2:  S[2] = S[1] * c²  = S·c³               ; c² -> c⁴
ms = 3:  S[3] = S[2] * c⁴  = S·c⁷
```

**Последовательность UE: `1, c, c³, c⁷`**, то есть `c^(2^ms − 1)`. Не `1, c, c²`.

Экстинкция (`VolumetricCloud.usf:391`) идёт по той же схеме и потребляется теневым маршем на
`VolumetricCloud.usf:1140` (`ExtinctionAcc[ms] += ShadowPMC.ExtinctionCoefficients[ms] * ExtinctionFactor;`)
и `VolumetricCloud.usf:1147`.

## Что делает наш код

`Editor/Resources/Shaders/Programs/Clouds/CloudRaymarch.shader:439-450`:

```glsl
for (int octave = 0; octave < octaveCount; ++octave)
{
    if (octave > 0)
    {
        scatterFactor *= scatterStep;
        ...
        scatterStep *= scatterStep;
```

octave 0 → `1`; octave 1 → `c`, шаг становится `c²`; octave 2 → `c · c² = c³`.
**Наша последовательность: `1, c, c³`. Совпадает с UE тексель в тексель.**

## Вердикт по Ц2

**Наш код верен. Неверен разбор в ревью.**

Арифметика ревью (`REVIEW_622a01a6.md:40-43`) как трассировка нашего кода правильная — `1, c, c³`
там выведено верно. Неверен следующий шаг: утверждение, что §2.1 и «простая геометрическая
прогрессия» предписывают `1, c, c²`. Формулировка §2.1 — «коэффициенты возводятся **в квадрат** на
каждой октаве, а не умножаются на константу» — описывает ровно `MsSFactor *= MsSFactor`
(`VolumetricCloud.usf:392`), и это даёт `1, c, c³`, а не `1, c, c²`. Геометрическая прогрессия
`1, c, c²` — это как раз то, что Epic **не** делает: она получилась бы, если бы множитель оставался
константой.

Расчёт при `MultiScatterContribution = 0.667`: третий порядок вносит `c³ = 0.2966`. Ревью ожидало
`c² = 0.4449` и увидело в разнице «потерянную треть». Потери нет: `0.2966` — это и есть число UE.

**Документ формально верен, но двусмыслен** и уже один раз был прочитан наоборот — этого достаточно,
чтобы его уточнить (правка ниже).

## Что при этом действительно сломано: множитель фазы

Тот же цикл у UE, но для фазы — `VolumetricCloud.usf:421-429`:

```hlsl
	UNROLL
	for (int ms = 1; ms < MSCOUNT; ++ms)
	{
		PMPC.Phase0[ms] = lerp(IsotropicPhase(), PMPC.Phase0[0], MsPhaseFactor);
		...
		MsPhaseFactor *= MsPhaseFactor;
	}
```

Здесь `lerp` идёт от **`PMPC.Phase0[0]`** — от БАЗОВОЙ фазы, а не от `Phase0[ms-1]`. Накопления нет.
Последовательность коэффициентов смешивания: `ms=1 → p`, `ms=2 → p²`, `ms=3 → p⁴`, то есть
`p^(2^(ms−1))`.

Наш `CloudRaymarch.shader:445,449` накапливает фазу так же, как рассеяние
(`phaseFactor *= phaseStep;` … `phaseStep *= phaseStep;`), и даёт `p, p³`.
Потребитель — `CloudRaymarch.shader:452`: `mix(CLOUD_ISOTROPIC_PHASE, phase, phaseFactor)`, то есть
ровно UE'шный `lerp(IsotropicPhase(), Phase0[0], MsPhaseFactor)`.

**На третьей октаве мы подставляем `p³` там, где UE подставляет `p²`.** При `p = 0.5` это `0.125`
против `0.25`: третий порядок у нас вдвое ближе к изотропному, чем у Epic. Дефект настоящий, просто
не тот, который был найден: спутаны две соседние цепочки, у которых у Epic разная семантика —
рассеяние и экстинкция накапливаются, фаза каждый раз берётся от базовой.

## Расхождение внутри самого UE (к сведению, не дефект у нас)

`VolumetricCloud.usf:1076` — ветка, где тень берётся из теневой карты, а не марша:

```hlsl
		PMC.TransmittanceToLight0[ms] *= exp(-OutOpticalDepth * (ms == 0 ? 1.0f : pow(MsExtinFactor, ms)));
```

`pow(MsExtinFactor, ms)` даёт `1, e, e², e³` — **другую** цепочку, чем `VolumetricCloud.usf:391`
(`1, e, e³, e⁷`), которую использует маршевая ветка на `VolumetricCloud.usf:1140`. Epic сам себе
здесь противоречит. У нас теневая ветка одна и она соответствует маршевой — то есть строке 1140,
а не 1076. Менять на `pow` **не надо**; отмечено, чтобы следующая сверка не «починила» это обратно.

---

# Ц1. Что валидирует реконструкция у UE

Проход — `ReconstructVolumetricRenderTargetPS`, `VolumetricRenderTarget.usf:249`.
Ниже разбирается штатная ветка `PERMUTATION_CLOUD_MIN_AND_MAX_DEPTH` (compute, mode 0) — та, что
соответствует нашей схеме.

## Прямой ответ на вопрос: сравнивает ли UE дистанцию до фронта облака

**Нет.** Ни одна проверка приёмки истории не сравнивает `CloudFrontDepthFromViewKm` истории с
`CloudFrontDepthFromViewKm` этого кадра. Фронт облака у UE участвует ровно в двух местах:

1. **как база репроекции** — `VolumetricRenderTarget.usf:286-291`:

```hlsl
		float TracingVolumetricSampleDepthKm = SafeLoadTracingVolumetricDepthTexture(int2(SVPos.xy) / DownSampleFactor).CloudFrontDepthFromViewKm;
		float TracingVolumetricSampleDepth = TracingVolumetricSampleDepthKm * KILOMETER_TO_CENTIMETER;
		float DeviceZ = ConvertToDeviceZ(TracingVolumetricSampleDepth); // Approximation. Should try real DeviceZ
		float4 CurrClip = float4(ScreenPosition, DeviceZ, 1);
		float4 PrevClip = mul(CurrClip, View.ClipToPrevClip);
```

2. **как абсолютный порог включения репроекции** — `VolumetricRenderTarget.usf:298-301`:

```hlsl
		const bool bValidPreviousUVs = all(PrevScreenUVs > 0.0) && all(PrevScreenUVs < 1.0f) 
			&& (TracingVolumetricSampleDepthKm >= MinimumDistanceKmToEnableReprojection);
```

Это **не** сравнение истории с кадром — это «ближе такого-то километра репроекцию вообще не
включаем». Комментарий рядом (`:299-301`) прямо называет причину: «This helps hide reprojection
issues due to imperfect approximation of cloud depth as a single front surface, especially visible
when flying through the cloud layer. It is not perfect but will help in lots of cases.»

Дефолт cvar — **0.0**, `VolumetricRenderTarget.cpp:62-65`, то есть у Epic этот гейт по умолчанию
выключен; текст справки там же советует «One could start with a value of 4km».

Все прочие сравнения истории с кадром идут по `SceneDepthFromViewKm` и `MinMaxViewDepthKm`.

## Полный список проверок UE перед принятием сэмпла истории

Порядок — как в коде.

1. **Репроецированный UV строго внутри экрана.** `VolumetricRenderTarget.usf:298`
   (`> 0.0` и `< 1.0`, границы исключены).
2. **Порог по дистанции фронта облака.** `VolumetricRenderTarget.usf:299`, разобран выше.
3. **`FixupDepthDataSafe` на обеих сторонах.** `VolumetricRenderTarget.usf:395-396`, тело —
   `VolumetricRenderTarget.usf:237-247`: всё дальше `MaxDepthKm = 60000.0f` схлопывается в
   `MaxHalfFloat`. Комментарий (`:239-241`) объясняет, зачем: протёкшая в небесный пиксель глубина
   ландшафта иначе читается реконструкцией как разрыв глубины, и облако мерцает.
4. **Гейт «применять ли разокклюзию вообще».** `VolumetricRenderTarget.usf:402-403`:

```hlsl
			const bool bApplyDisoclusion = any(NewDepthData.MinMaxViewDepthKm < MinimumDistanceKmToDisableDisoclusion) || any(HistoryDepthData.MinMaxViewDepthKm < MinimumDistanceKmToDisableDisoclusion)
										|| NewDepthData.SceneDepthFromViewKm < MinimumDistanceKmToDisableDisoclusion || HistoryDepthData.SceneDepthFromViewKm < MinimumDistanceKmToDisableDisoclusion;
```

   Дефолт `MinimumDistanceKmToDisableDisoclusion = 5.0` км, `VolumetricRenderTarget.cpp:67-70`.
   То есть **для далёких облаков UE тесты разокклюзии не выполняет вовсе** и берёт историю
   сознательно; комментарий `:400-401` формулирует это как «cloud information will be like a layer
   blended on top without upsampling».
5. **Три теста внутри гейта**, порог `ThresholdToNewSampleKm = 2.0` (`VolumetricRenderTarget.usf:406`):
   - `:408-409` — большой разброс min/max глубины в текселе И ближняя сторона ближе к глубине сцены
     → форсировать новый сэмпл («This helps a lot removing cloud over trees and small details»);
   - `:416` — история ближе нового более чем на 2 км → новый сэмпл (объект уехал, открылась область);
   - `:423` — история дальше нового более чем на 2 км → новый сэмпл (область только что закрылась).
6. **Тест края по минимальной глубине.** `VolumetricRenderTarget.usf:480-485`:

```hlsl
				if (abs(NewDepthData.MinMaxViewDepthKm.x - HistoryDepthData.MinMaxViewDepthKm.x) > 1.0f)
				{
					ReprojRGBA = NewRGBA;
					ReprojRGBA2 = NewRGBA2;
					ReprojDepthData = NewDepthData;
				}
```

   Комментарий `:476-479` называет назначение прямо: «Ignore reprojected samples with too large of a
   min depth difference that have been missed by dissoclusion. Those samples are considered as an
   edge so we skip because it might reproject bad bilinearly filtered data over the background».
7. **Проверка на конечность.** `VolumetricRenderTarget.usf:491-498`: `IsFinite` по обоим цветам и по
   обеим дистанциям, с падением в определённое значение (`1000.0f` км), а не в ноль.

## Что делает вторая ветка UE — и почему это важно для нашего комментария

Ветка без min/max (`VolumetricRenderTarget.usf:517-619`, используется, когда compute недоступен)
устроена иначе в двух отношениях:

- порог **относительный**, а не абсолютный, и опорная дистанция берётся из настоящего буфера
  глубины, а не из guide — `VolumetricRenderTarget.usf:536-538` и `:543`:

```hlsl
				if (/*ReconstructDepthZ > 0.0001f &&*/ abs(PixelDistanceFromViewKm - DepthData.SceneDepthFromViewKm) > PixelDistanceFromViewKm * 0.1f)
```

- при провале теста UE **не отбрасывает историю, а ищет по восьми соседям** текущей трассировки
  тексель с ближайшей глубиной сцены — `VolumetricRenderTarget.usf:534` (таблица смещений) и
  `:546-558`.

Наш `CloudTemporalResolve.shader:213-215` утверждает обратное: «That is Unreal's rule and the reason
there is no dilation or search here». Дилатация у Unreal есть — в той ветке, которой мы не следуем.
Утверждение надо либо снять, либо ограничить веткой mode 0.

## Вердикт по Ц1

**Наш код верен в выборе канала: сравнивать `.y` (дистанцию сцены) — это ровно то, что делает UE.
Порог `kDisocclusionKm = 2.0f` (`CloudTemporalResolve.shader:101`) совпадает с
`ThresholdToNewSampleKm = 2.0` (`VolumetricRenderTarget.usf:406`) точно.**

**Неверен комментарий** `CloudTemporalResolve.shader:36-43`: он называет три правила «правилами
Unreal» и этим обещает полноту, которой нет. У UE проверок шесть-семь, и в списке нет ни одной из
трёх, которые у нас отсутствуют.

Механизм госта, описанный тимлидом (кромка, уехавшая по чистому небу, где дистанция сцены у истории
и у кадра одна и та же — дальняя плоскость), **у UE тоже присутствует**, и Epic закрывает его не
сравнением фронта, а двумя другими средствами, которых у нас нет:

- гейтом по абсолютной дистанции фронта (`:299`, cvar с рекомендацией 4 км);
- тестом по `MinMaxViewDepthKm.x` (`:480`) — минимальной глубине сцены в текселе, — комментарий к
  которому описывает ровно наш сценарий: «reproject bad bilinearly filtered data over the background».

Второе у нас невозможно без изменения формата guide: наш guide несёт две дистанции, `.zw` записаны
нулями и объявлены неиспользуемыми (`CloudRaymarch.shader:67-71`), тогда как у UE это четыре канала
`(CloudFront, Scene, MinScene, MaxScene)` — `VolumetricRenderTarget.usf:107-119` и `:630`.

---

# Ц3. Как UE читает свой depth guide в реконструкции

## Точечно или фильтровано

Обёртки объявлены рядом и различаются ровно этим:

- `VolumetricRenderTarget.usf:142` — `SafeLoadTracingVolumetricDepthTexture`, внутри
  `TracingVolumetricDepthTexture.Load(...)` — **точечно**;
- `VolumetricRenderTarget.usf:147` — `SafeSampleTracingVolumetricDepthTexture`, внутри
  `.SampleLevel(LinearTextureSampler, ...)` — **фильтровано**;
- `VolumetricRenderTarget.usf:188` / `:193` — та же пара для текстуры предыдущего кадра.

В реконструкции:

| Что | Как читается | Строка |
|---|---|---|
| guide этого кадра для репроекции — **для любого пикселя, трассированного или нет** | `Load` (точечно) | `VolumetricRenderTarget.usf:286` |
| guide этого кадра как кандидат «новый сэмпл» | `Load` (точечно), `CenterSample = SVPos / DownSampleFactor` | `VolumetricRenderTarget.usf:388, 391` |
| guide истории | `SampleLevel` (билинейно) по репроецированному UV | `VolumetricRenderTarget.usf:393` |
| guide четвертьразрешения как аварийный фолбэк | `SampleLevel` (билинейно) | `VolumetricRenderTarget.usf:504-506` |

Ключевое: даже для пикселя, который в этом кадре **не** трассировался, UE берёт свой
четвертьразрешённый тексель **точечным `Load`** — тот, что содержит этот пиксель
(`int2(SVPos.xy) / DownSampleFactor`, `VolumetricRenderTarget.usf:286`).

## Что UE записывает в guide

Запись — `VolumetricRenderTarget.usf:627-636`:

```hlsl
	OutputRt0 = RGBA;
#if PERMUTATION_CLOUD_MIN_AND_MAX_DEPTH
	OutputRt1 = RGBA2;
	OutputRt2 = float4(DepthData.CloudFrontDepthFromViewKm, DepthData.SceneDepthFromViewKm, DepthData.MinMaxViewDepthKm);
```

`DepthData` к этому моменту — одно из четырёх:

1. `NewDepthData` — **точечный `Load` своего текселя** (`:391`, ветка `:454-459`);
2. `HistoryDepthData` — билинейная выборка **предыдущего РЕКОНСТРУИРОВАННОГО** guide, то есть
   ПОЛУразрешённого, где силуэт уже разрешён (`:393`, ветка `:461-489`);
3. снова `NewDepthData`, если сработал тест края `:480`;
4. билинейная выборка четвертьразрешённого guide — **только** когда истории нет вовсе
   (`:504-506`, ветка `else // !bValidPreviousUVs`).

**То есть билинейно усреднённую по четвертьразрешению дистанцию UE пишет ровно в одном случае — на
разрыве истории.** В штатном пути нетрассированный пиксель получает либо свой точечный тексель,
либо историю своего же разрешения.

## Как guide читают потребители UE

Апсемплинг/композит: четыре соседа читаются точечно —
`VolumetricRenderTarget.usf:1091-1094` через `SafeLoadVolumetricDepthTexture`
(`.Load`, объявление на `VolumetricRenderTarget.usf:749`). Билатеральные веса —
`VolumetricRenderTarget.usf:1181` при `WeightMultiplier = 1000.0f` (`:1125`):

```hlsl
			float4 weights = 1.0f / (Depth4Diff * WeightMultiplier + 1.0f);
```

Это в точности наш `GuideWeight` (`CloudComposite.shader:84-87`), и наш композит читает guide
`texelFetch`'ем (`CloudComposite.shader:126`) — здесь мы совпадаем с UE.

Отдельное подтверждение того, что фильтрованный guide у Epic считается вредом, —
`VolumetricCloudCommon.ush:89-92`:

```hlsl
	// When depth is reprojected using bilinear filtering, it can leave some trails that are visible when doing binary depth testing.
	// Fixing the reprojection is costly and also not perfect so instead we workaround that issue but using the min of the gathered cloud front depth (being MaxHalf when no clouds have been traced)
	float4 FrontDepth4 = VolumetricCloudDepth.Gather(VolumetricCloudDepthSampler, ScreenUv, int2(0, 0));
	const float CloudFrontDepthKm = min(min(FrontDepth4.x, FrontDepth4.y), min(FrontDepth4.z, FrontDepth4.w));
```

Epic знает о «хвостах» от билинейно отфильтрованной глубины и лечит их у потребителя — **min по
Gather**, а не среднее.

## Вердикт по Ц3

**Наш код неверен.**

`CloudTemporalResolve.shader:194-195` берёт `traceGuide = texture(u_CloudTraceGuide, traceUv)` —
билинейку по четвертьразрешению — и строка `:209` записывает её как guide кадра для трёх пикселей из
четырёх. Это ветка, которую UE держит **аварийной** (`VolumetricRenderTarget.usf:504-506`,
`!bValidPreviousUVs`), поставленная в штатный путь.

Хуже того, эта же величина уходит и в репроекцию (`CloudTemporalResolve.shader:235`,
`traceGuide.x`), тогда как UE для репроекции берёт точечный `Load` даже у нетрассированного пикселя
(`VolumetricRenderTarget.usf:286`). То есть у нас усреднённая дистанция портит три вещи сразу:
билатеральный вес композита, тест разокклюзии следующего кадра и точку репроекции.

Замечание тимлида про `CloudRaymarch.shader:74-75` («a filtered depth across a silhouette averages
foreground and background into a distance where nothing is») подтверждается независимо —
`VolumetricCloudCommon.ush:89-90` говорит то же самое своими словами.

---

# Ц6. Таблица обхода субпикселей

## Фактическая таблица UE

`VolumetricRenderTarget.cpp:300-322`:

```cpp
			NoiseFrameIndex += FrameId == 0 ? 1 : 0;
			NoiseFrameIndexModPattern = NoiseFrameIndex % (VolumetricTracingRTDownsampleFactor * VolumetricTracingRTDownsampleFactor);

			FrameId++;
			FrameId = FrameId % (VolumetricTracingRTDownsampleFactor * VolumetricTracingRTDownsampleFactor);

			if (VolumetricTracingRTDownsampleFactor == 2)
			{
				static int32 OrderDithering2x2[4] = { 0, 2, 3, 1 };
				int32 LocalFrameId = OrderDithering2x2[FrameId];
				CurrentPixelOffset = FIntPoint(LocalFrameId % VolumetricTracingRTDownsampleFactor, LocalFrameId / VolumetricTracingRTDownsampleFactor);
			}
			else if (VolumetricTracingRTDownsampleFactor == 4)
			{
				static int32 OrderDithering4x4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
```

- Таблица 2×2 — **`{ 0, 2, 3, 1 }`**, дословно наша (`CloudPayload.hpp:149`).
- Формула развёртки — **`(index % factor, index / factor)`** (`VolumetricRenderTarget.cpp:310`),
  дословно наша (`CloudPayload.hpp:152`).
- `NoiseFrameIndex` инкрементируется при `FrameId == 0`, то есть раз в полный цикл
  (`VolumetricRenderTarget.cpp:300`) — как и утверждает `ANALYSIS_APPROACH.md` §2.1.

## Есть ли у UE комментарий, объясняющий порядок

**Не нашёл.** Искал: комментарии в окне `VolumetricRenderTarget.cpp:294-330`; вхождения
`OrderDithering`, `CurrentPixelOffset`, `NoiseFrameIndex` по всему `VolumetricRenderTarget.cpp`;
`Dither`, `checker`, `diagonal`, `Bayer` по `VolumetricRenderTarget.cpp`,
`VolumetricRenderTarget.usf` и `VolumetricCloudRendering.cpp`.

Результат честно: `diagonal` и `Bayer` не встречаются нигде ни разу. `checker` встречается, но во
всех случаях — про `HalfResolutionDepthCheckerboardMinMaxTexture`, то есть про шахматный min/max
буфер глубины (`VolumetricRenderTarget.cpp:792, 822, 882`), а не про порядок обхода субпикселей.
Единственный комментарий в самом блоке — `// Default linear parse`
(`VolumetricRenderTarget.cpp:320`), и он относится к ветке-фолбэку, а не к таблице.

Единственное, что Epic сообщает о намерении, — **имя массива**: `OrderDithering2x2` /
`OrderDithering4x4`. Это «порядок дизеринга», а не «диагональное чередование».

Косвенное, но однозначное подтверждение природы таблицы даёт версия 4×4
(`VolumetricRenderTarget.cpp:314`): `{0,8,2,10, 12,4,14,6, 3,11,1,9, 15,7,13,5}` — это стандартная
**матрица Байера 4×4**, выписанная в растровом порядке. Тогда `{0,2,3,1}` — это стандартная матрица
Байера 2×2 `[[0,2],[3,1]]`, тоже в растровом порядке. Обоснование таблицы — «это Байер», и оно про
равномерность распределения по порядку, а не про диагональ.

## Фазовое различие с нашим кодом

UE инкрементирует `FrameId` **до** индексации (`VolumetricRenderTarget.cpp:303-304`), поэтому
фактическая последовательность смещений у UE — `(0,1) → (1,1) → (1,0) → (0,0)`, а у нас
(`CloudPayload.hpp:155-158`) — `(0,0) → (0,1) → (1,1) → (1,0)`. Это **один и тот же цикл с другой
начальной фазой**; на картинку это не влияет, менять не нужно.

## Вердикт по Ц6

**Таблица верна — совпадает с UE дословно, вместе с формулой развёртки. Неверен комментарий.**

Обход `{0,2,3,1}` действительно круговой: каждая пара соседних кадров — сторона квадрата, ни одна не
диагональ. Разбор тимлида (`REVIEW_622a01a6.md:125-132`) правильный, и его замечание, что
диагонального 4-цикла на 2×2 не существует, тоже верно.

Дополнительно: **у Epic нет комментария, который мы могли бы процитировать вместо своего.**
Правка — заменить ложное обоснование на проверяемое (это матрица Байера 2×2) и на ссылку с версией,
а не изобретать новое объяснение.

---

# Что чинить и как

Порядок — по цене ошибки, а не по номеру находки.

## 1. Ц3 — guide перестаёт быть фильтрованным. **Меняет картинку, нужна пересъёмка.**

`Editor/Resources/Shaders/Programs/Clouds/CloudTemporalResolve.shader:194-202`.

Guide читать **всегда** точечно, из текселя, который содержит этот пиксель — то есть перенести
`texelFetch` из ветки `owned` наружу и оставить в ветке только радиантность:

```glsl
vec4 traceScatter = texture(u_CloudTrace, traceUv);
vec4 traceGuide   = texelFetch(u_CloudTraceGuide, traceCoord, 0);   // ВСЕГДА точечно

if (owned)
    traceScatter = texelFetch(u_CloudTrace, traceCoord, 0);
```

`traceCoord` уже равен `coord >> 1` (`:178`) — это ровно UE'шный
`int2(SVPos.xy) / DownSampleFactor` (`VolumetricRenderTarget.usf:286`), так что менять больше нечего.

Побочно чинится и точка репроекции (`:235` читает `traceGuide.x`) — она тоже станет точечной, как у
UE.

Комментарий на `:204-208` при этом остаётся верным по существу («guide всегда описывает ЭТОТ кадр»)
и должен получить ссылку на `VolumetricRenderTarget.usf:286, 391` с указанием версии.

Ожидаемый эффект на картинке: пропадает срабатывание билатерального фильтра вдоль всей кромки —
тот самый дефект, который `CloudRaymarch.shader:250-254` описывает как «квантование кадра на сетку
прохода». **Кадры пересъёмочные: зенит + средний угол + горизонт**, по правилу «три угла, не один».

## 2. Ц2 — множитель фазы. **Меняет картинку, нужна пересъёмка.**

`Editor/Resources/Shaders/Programs/Clouds/CloudRaymarch.shader:439-450`.

Фазу вынуть из накапливающей цепочки: у UE она каждый раз берётся от базовой
(`VolumetricCloud.usf:424`).

```glsl
if (octave > 0)
{
    scatterFactor *= scatterStep;
    extinctFactor *= extinctStep;
    phaseFactor    = phaseStep;   // = , не *= : UE смешивает от БАЗОВОЙ фазы каждый раз

    scatterStep *= scatterStep;
    extinctStep *= extinctStep;
    phaseStep   *= phaseStep;
}
```

Проверка: octave 0 → `1`; octave 1 → `p`; octave 2 → `p²`. Совпадает с `Phase0[0..2]` UE.

`scatterFactor` и `extinctFactor` **не трогать** — они уже дают `1, c, c³`, то есть UE.

Эффект: третья октава становится вдвое направленнее (при `p = 0.5` коэффициент `0.25` вместо
`0.125`) — заметно на подсвеченных краях против солнца. **Пересъёмка нужна, три угла.**

## 3. Ц2 — тест на цепочки октав. Картинку не меняет.

Три цепочки живут в `.shader`, который как C++ не компилируется, поэтому расхождение фазы и
рассеяния никакой тест поймать не мог. Вынести вычисление трёх множителей чистой функцией в
`Editor/Resources/Shaders/Common/CloudLighting.glslh` (там уже есть сюита —
`Desert/Tests/Engine/CloudLighting/`) и запинить тестом обе последовательности:

- `scatter/extinct`: `1, c, c³` — против `VolumetricCloud.usf:390-393`;
- `phase`: `1, p, p²` — против `VolumetricCloud.usf:424-428`.

Это ровно случай `DEV_CONTRACT.md` §2.3.1: две величины обязаны согласовываться с внешним эталоном —
значит, тест, а не комментарий.

## 4. Ц2 — уточнить `ANALYSIS_APPROACH.md` §2.1. Картинку не меняет.

Строка про multi-scattering сейчас: «коэффициенты возводятся **в квадрат** на каждой октаве, а не
умножаются на константу». Формально верно, но уже прочитано как `1, c, c²`. Заменить на
последовательность дословно, с двумя разными цепочками:

> коэффициенты рассеяния и экстинкции идут как `1, c, c³, c⁷` (множитель возводится в квадрат
> **после** применения, `VolumetricCloud.usf:390-393`); коэффициент смешивания фазы — `1, p, p², p⁴`,
> потому что фаза каждой октавы смешивается от БАЗОВОЙ, а не от предыдущей
> (`VolumetricCloud.usf:424-428`)

и поставить рядом ссылку на версию (`release @ 71fe36aa`, 2026-07-28).

## 5. Ц6 — заменить обоснование таблицы. Картинку не меняет.

`Desert/Desert/Source/Engine/Graphic/Clouds/CloudPayload.hpp:127-138`.

Из комментария убрать утверждение про «DIAGONALLY opposite» и про «horizontal crawl»: первое ложно
(обход круговой, а диагонального 4-цикла на 2×2 не существует), второе — недоказанное объяснение
поверх ложного. Написать проверяемое:

> Таблица `{0, 2, 3, 1}` — матрица Байера 2×2 `[[0,2],[3,1]]` в растровом порядке, взята дословно из
> `VolumetricRenderTarget.cpp:308` (`OrderDithering2x2`), вместе с развёрткой
> `(index % 2, index / 2)` (там же, `:310`). Обоснования порядка Epic не даёт — только имя
> `OrderDithering`; версию 4×4 (`:314`) видно как каноническую матрицу Байера, что и объясняет
> природу таблицы. Обход круговой: соседние кадры — стороны квадрата, не диагонали.
> Проверено против `release @ 71fe36aa` (2026-07-28).

Пять `static_assert` ниже (`:155-159`) верны и остаются как есть.

## 6. Ц1 — привести комментарий в соответствие, затем решить про недостающие проверки.

**Часть A, обязательная, картинку не меняет.** `CloudTemporalResolve.shader:36-43`: снять обещание
полноты. Формулировка «WHAT IS VALIDATED, and each rule is Unreal's» → «...это подмножество проверок
Unreal (mode 0); полный список — `Docs/Clouds/UEReference/Documentation/UESource.md`, раздел Ц1».
Там же (`:213-215`) убрать утверждение «no dilation or search here … That is Unreal's rule»:
в ветке без min/max UE как раз дилатирует по восьми соседям
(`VolumetricRenderTarget.usf:534, 546-558`). Правильная формулировка — «в mode 0 Unreal дилатации не
делает; она есть в его ветке без min/max, которой мы не следуем».

Порог `kDisocclusionKm = 2.0f` подтверждён (`VolumetricRenderTarget.usf:406`) — оставить и сослаться.

**Часть B, дешёвая, меняет картинку в движении.** Добавить гейт по абсолютной дистанции фронта —
UE'шный `MinimumDistanceKmToEnableReprojection` (`VolumetricRenderTarget.usf:299`,
`VolumetricRenderTarget.cpp:62-64`). Одно условие в `if (onScreen)` на
`CloudTemporalResolve.shader:250`: репроекцию не включать, если `traceGuide.x` меньше порога.
Дефолт Epic — `0.0` (гейт выключен), справка советует `4 км`; брать **0.0**, чтобы не менять
картинку молча, и вывести параметр наружу вместе с остальными в `CloudResolveParams`.
Если ставить ненулевое значение — **пересъёмка**, и по справке Epic ждать шума ближе порога.

**Часть C, дорогая, отдельной задачей.** Тест по минимальной глубине сцены в текселе
(`VolumetricRenderTarget.usf:480`) — то самое средство Epic против «bad bilinearly filtered data over
the background», то есть против госта на кромке из находки Ц1. Требует расширения guide до четырёх
каналов `(CloudFront, Scene, MinScene, MaxScene)` по образцу
`VolumetricRenderTarget.usf:107-119`. У нас `.zw` сейчас записаны нулями и объявлены
неиспользуемыми (`CloudRaymarch.shader:67-71`), формат `rgba16f` уже четырёхканальный — то есть
место есть, платить надо только за вычисление min/max глубины сцены в марше.

Порядок разумный: сначала 1 и 2 (они дешёвые и снимают самый заметный дефект), затем 6A, и только
после пересъёмки — 6C, потому что часть госта может уйти уже от точечного guide.
