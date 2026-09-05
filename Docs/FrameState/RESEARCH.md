# Покадровое состояние принадлежит рендереру, а не материалу — исследование

Дата проверки: 2026-08-10. Ветка `dev`, HEAD **`e34783f`**.

Всё ниже прочитано в коде и перепроверено заново. Там, где `Docs/RENDERER_FRAME_STATE.md` расходится с
кодом, это сказано явно — раздел 9. Пути даны от корня репозитория.

Работа только на чтение: ничего не менялось, не собиралось и не запускалось.

> **О предыдущем заходе.** Прошлый исследователь дошёл до документа и записал его, но файл остался
> **неотслеживаемым** в основном рабочем каталоге (`Docs/FrameState/RESEARCH.md`, проверено на
> `3771019`), а работа шла в worktree — поэтому он выглядел утраченным. Настоящий документ построен на
> независимой перепроверке каждого несущего утверждения того текста; результат сошёлся, расхождения
> перечислены в конце раздела 9 (пункты «поправки к заходу на `3771019`»).
>
> `dev` с тех пор ушёл с `3771019` на `e34783f`. `git diff --stat 3771019 e34783f` — 12 файлов, всё это
> миграция настроек неба в сценах (`SceneMigration.*`, `SceneSerializer.cpp`, шесть `.desce`, новый тест
> `Desert/Tests/Engine/SceneSkyMigration`). **Нашей территории эти коммиты не касаются вообще**; единственное
> следствие — тестовых проектов стало 30, а не 29.

---

## 0. Краткий ответ

* Затронуто **18 из 54** файлов `.shader` + **6 из 13** общих заголовков `.glslh` (плюс один транзитивный).
* Рефлексия **уже полностью понимает номера наборов** и всегда понимала — недавняя переписка её не касалась.
  Раскладки создаются **по одной на набор**, есть `GetDescriptorSetLayout(set)`.
* Сломано ровно одно звено в коде: **запись дескрипторов жёстко прибита к набору 0** — четыре строки
  `const uint32_t setIndex = 0; // Simplified`. Половина, отвечающая за выделение, наборы уже понимает.
* Аренда слотов множит память **в 6 раз** на каждый uniform-буфер, каждый непостоянный storage-буфер и каждый
  дескрипторный набор каждого материала. Создателей `SceneRenderer` — ровно шесть на шесть слотов: запас ноль.
* **Главное, чего не знали предыдущие тексты (4.0):** план «покадровый набор привязывается один раз в начале
  прохода» в Vulkan неисполним при текущем коде — раскладки пайплайнов несовместимы, потому что
  push-константные диапазоны у шейдеров разные. Отсюда два следствия: покадровое должно ехать в **набор 0**
  (материал — в набор 1), а не наоборот; и привязку надо оставить **на каждую отрисовку**. Владение — то, что
  лечит баг; редкость привязки — отдельная оптимизация на потом.
* Рядом идёт программа «Небо и облака» (задача T6), которая трогает те же шейдеры и **прямо сейчас лежит
  недоделанной в этом самом worktree**. Читать после стабилизации.

---

## 1. Текущее устройство, точно

### 1.1 Кто пишет покадровое состояние

Точка сборки одна — `MeshRenderer::FrameState`, объявлена в
`Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.hpp:120-144`. Это снимок всего, что сцена
(а не объект) вносит в освещённую отрисовку: камера, три вида света, четыре каскада теней с их матрицами и
картами, IBL (irradiance / prefiltered / BRDF LUT).

* Собирается в `MeshRenderer::CaptureFrameState`
  (`Desert/Desert/Source/Engine/Graphic/Systems/Scene/Mesh/MeshRenderer.cpp:1196`).
* Применяется в `MeshRenderer::FrameState::ApplyTo` (`.../MeshRenderer.cpp:1232`) — ровно четыре вызова:
  `StaticMaterialPBR::UpdateCamera / UpdateLights / UpdateShadow / UpdateEnvironment`.
* Снимок берётся в трёх местах: `.../MeshRenderer.cpp:487` (стекло/GI), `:556` (RSM из позиции света),
  `:598` (основной проход). Применяется в **пяти**: `:487`, `:556` (снимок и применение в одном выражении),
  и `:750`, `:825` — снимком, взятым на `:598`.

`StaticMaterialPBR::Update*` — тонкие делегаты на базу
(`Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.cpp:26-52`).

### 1.2 Куда именно это пишется

`MaterialPBRBase` (`Desert/Desert/Source/Engine/Graphic/Materials/Mesh/PBR/MaterialPBRBase.cpp`) пишет
**через родительский материал**, а не через экземпляр:

| Функция | Строка | Что пишет | Через что |
|---|---|---|---|
| `UpdateCamera` | `:19` | `CameraUB` | `instance->GetParentMaterial()->Get<UniformBufferProperty>(...)->SetRawData` (`:27`) |
| `UpdatePointLights` | `:41` | `PointLightsUB` | `StorageBufferProperty::SetRawData` (`:49`) |
| `UpdateSpotLights` | `:55` | `SpotLightsUB` | `StorageBufferProperty::SetRawData` (`:61`) |
| `UpdateDirectionLights` | `:67` | `DirectionLightsUB` | `UniformBufferProperty::SetRawData` (`:74`) |
| `UpdateShadow` | `:79` | `ShadowUB` + `u_ShadowMap0..3` | UB + `Texture2DProperty::SetImage` (`:102`) |
| `UpdateEnvironment` | `:118` | `u_EnvIrradianceTex`, `u_EnvSpecularTex`, `u_BRDFLUTTexture` | `TextureCube/Texture2DProperty` (`:121`) |
| `UpdateLightsMetadata` | `:133` | `LightsMetadata` | `UniformBufferProperty::SetRawData` (`:146`) |

`GetParentMaterial()` — это **один объект на шейдер**. Именно поэтому запись выглядит дешёвой и делается раз на
группу, и именно поэтому второй рендерер в том же кадре её затирал.

Помимо PBR камеру в общий `CameraUB` пишут ещё семь мест, каждое своим кодом:
`Materials/Mesh/MaterialShadow.cpp:22`, `Materials/Mesh/MaterialSilhouette.cpp:16` и `:35`,
`Materials/Skybox/MaterialSkybox.cpp:31`, `Materials/Skybox/MaterialProceduralSky.hpp:31`,
`Materials/Particles/MaterialParticleBillboard.hpp:29`, `Materials/Debug/MaterialDebugLine.hpp:34`,
`Materials/Debug/MaterialOverdraw.hpp:25`, и — по имени, из рефлексии — `MeshRenderer::DrawGenericMeshes`
(`MeshRenderer.cpp:235-297`, там же `TimeUB`). Отдельная копия каскадов живёт у отложенного освещения:
`Materials/Deferred/MaterialDeferredLighting.hpp:17` и `:116` («mirrors `MaterialPBRBase::UpdateShadow`»).

### 1.3 Как это доезжает до GPU

`UniformBufferProperty::SetRawData` помечает свойство грязным; при флаше материала
`VulkanMaterialBackend::ApplyUniformBuffer`
(`Desert/Desert/Source/Engine/Graphic/API/Vulkan/VulkanMaterialBackend.cpp:186`) строит
`VkWriteDescriptorSet` и вызывает `vkUpdateDescriptorSets` (`:180`). Дескриптор **ссылается** на буфер, а не
копирует его, — отсюда и весь класс багов.

Привязка: `VulkanMaterialBackend::BindDescriptorSets` (`:299-327`) — `vkCmdBindDescriptorSets` с
**`firstSet = 0`** и всем массивом наборов сразу (`:325`). Вызывается из шести мест `VulkanRenderer.cpp`
(`:242`, `:341`, `:381`, `:426`, `:460`, `:502`).

### 1.4 Слоты рендерера — как устроена аренда

* Константа: `kMaxRendererSlots = 6` — `Desert/Desert/Source/Engine/Core/FrameManager.hpp:17`,
  реэкспорт `Desert/Desert/Source/Engine/Core/EngineContext.hpp:61`.
* Хранение «кто сейчас пишет»: `EngineContext::m_ActiveRendererSlot` (`EngineContext.hpp:63-71`, поле `:109`).
  Значение вне диапазона молча сворачивается в 0 (`:70`).
* Захват/возврат: свободные слоты — битовая маска `s_SlotsInUse` в анонимном namespace
  `Desert/Desert/Source/Engine/Graphic/SceneRenderer.cpp:212`; `ClaimRendererSlot()` `:213-231` берёт
  **младший свободный**, `ReleaseRendererSlot()` `:233-238` возвращает. Конструктор
  `SceneRenderer::SceneRenderer()` `:240-242`, деструктор `:244-247`.
* Публикация: `SceneRenderer::BeginScene` `:253` — `SetActiveRendererSlot( m_RendererSlot )`, первой строкой.
* Переполнение: `LOG_WARN` `:227-230` и падение на слот 0.

Кто создаёт `SceneRenderer` (то есть кто конкурирует за 6 слотов) — **шесть мест**:

| Место | Файл:строка |
|---|---|
| Главный вьюпорт редактора | `Editor/Source/EditorLayer.cpp:246` |
| Дополнительное окно сцены (Scenes → New Scene View) | `Editor/Source/EditorLayer.cpp:677` |
| Превью в панели свойств | `Editor/Source/Editor/Widgets/PreviewViewport.cpp:108` |
| Рендерер миниатюр ассетов | `Editor/Source/Editor/Widgets/AssetThumbnailRenderer.cpp:25` |
| Превью фотограмметрии | `Editor/Source/Editor/Panels/Photogrammetry/PhotogrammetryPanel.cpp:601` |
| Runtime | `Runtime/Source/RuntimeLayer.cpp:63` |

Шесть создателей на шесть слотов. Запас — ноль: одновременно живые главный вьюпорт, второе окно сцены,
превью свойств, миниатюры и фотограмметрия занимают пять, и любое **седьмое** окно уже получает
`LOG_WARN` и слот 0. Потолок не теоретический.

### 1.5 Где слот читается

Слот разрешается ровно в четырёх местах, и это сделано намеренно:

* `VulkanUniformBuffer::CopyIndex` — `Desert/Desert/Source/Engine/ShaderResources/API/Vulkan/VulkanUniformBuffer.cpp:21-26`,
  `index = frameIndex * slots + slot`. Используется в `SetData` `:111` и `MapMemory` `:123`.
* `VulkanStorageBuffer::CopyIndex` —
  `Desert/Desert/Source/Engine/ShaderResources/API/Vulkan/VulkanStorageBuffer.cpp:134-139`;
  `SetData` `:123`, `MapMemory` `:130`, `GetDescriptorBufferInfo` — `VulkanStorageBuffer.hpp:36-42`.
* `VulkanMaterialBackend::GetDescriptorSet` — `VulkanMaterialBackend.cpp:159-170`. Единая точка: и запись, и
  привязка ходят сюда (`BindDescriptorSets` берёт слот сам, `:302`).
* Учёт грязности: `Materials/Properties/MaterialProperty.hpp:49-67` и
  `Materials/Properties/FieldProperty.hpp:26, 131-132` — счётчики
  `std::array<..., kMaxRendererSlots>`; `Materials/Properties/PropertyDirty.hpp:37` — `frames * kMaxRendererSlots`.

Плюс два места, где слот временно перематывается:
`VulkanMaterialBackend::InitializeWithFallbacks` `:359-364` и `:494` — прогоняет фолбэки по **всем** слотам,
чтобы ни один набор не был привязан незаписанным; и `FlushUpdates` `:344-348` — помечает свежими наборы
**только своего** слота.

*Замечание:* обе перемотки — работа через **ambient**-состояние (глобальный «текущий слот» в синглтоне), а не
через аргумент. Это ровно тот приём, который вариант A устраняет: слот перестанет существовать, и перематывать
будет нечего.

### 1.6 Память

Все числа ниже — из кода, не из замеров.

`framesInFlight` по умолчанию 2 (`FrameManager.hpp:47`, доступ `EngineContext.hpp:44-47`),
`kMaxRendererSlots` = 6.

| Ресурс | Копий сейчас | Копий без слотов | Множитель |
|---|---|---|---|
| Uniform-буфер (`VulkanUniformBuffer.cpp:69`) | `2 × 6 = 12` | 2 | **×6** |
| Storage-буфер, непостоянный (`VulkanStorageBuffer.cpp:68`) | `2 × 6 = 12` | 2 | **×6** |
| Storage-буфер, постоянный (`VulkanStorageBuffer.cpp:68`) | 1 | 1 | ×1 |
| Дескрипторные наборы материала (`VulkanMaterialBackend.cpp:129-156`) | `2 × 6 × setCount` | `2 × setCount` | **×6** |
| Размер пула (`VulkanMaterialBackend.cpp:115`) | `framesInFlight × slots × setCount` | — | **×6** |

Размеры покадровых uniform-блоков (по объявлениям, не по замеру):
`CameraUB` = `mat4 + mat4 + vec3` → 140 Б объявленного размера (значение 140 названо в коде:
`MeshRenderer.cpp:291`); `ShadowUB` = `4×mat4 + 3×vec4` = 304 Б (`MaterialPBRBase.cpp:86-92`);
`LightsMetadata` = три `uint` = 12 Б (`MaterialPBRBase.cpp:144-147`); `DirectionLightsUB` = `2×vec4` = 32 Б
(`Editor/Resources/Shaders/Common/DirectionLightsUB.glslh:4-8`). Итого ≈ 488 Б покадровых UB на освещённый
материал: 12 копий ≈ 5,9 КБ против 2 копий ≈ 1,0 КБ. Лишнего ≈ 4,9 КБ на материал — это мелочь.

Дорогое — storage-буферы, и их размер **динамический**: `VulkanStorageBuffer::SetData` растит буфер и
переаллоцирует все копии (`VulkanStorageBuffer.cpp:110-114`), стартовый размер из рефлексии — жёстко 36 байт
(`Desert/Desert/Source/Engine/ShaderResources/ShaderResourcesManager.cpp:117`). Статического числа для массива
материалов на объект или для поз костей в источнике нет — оценки в `Docs/RENDERER_FRAME_STATE.md:87-91`
(«~6 КБ на позу», «~640 КБ на 10k объектов», «~5 МБ») ничем в коде не подкреплены; это оценки автора, не
измерения. **В источнике не указано.**

**Единственный постоянный storage-буфер во всём движке** — `ParticleState`
(`Desert/Desert/Source/Engine/Graphic/Systems/Scene/Particles/ParticleRenderer.cpp:119-120`,
`/*persistent=*/true`). Все остальные создаются с `persistent = false` по умолчанию
(`Desert/Desert/Source/Engine/ShaderResources/StorageBuffer.hpp:30`): `ParticleSpawn`
(`ParticleRenderer.cpp:121`), `GrassIndirect` (`Systems/Scene/Terrain/TerrainRenderer.cpp:428`), `GrassVisible`
(`TerrainRenderer.cpp:435`), `AEHistogram` (`Systems/Scene/PostProcessing/AutoExposureRenderer.cpp:63`) и все
буферы из рефлексии (`ShaderResourcesManager.cpp:117`).

---

## 2. Полный список затрагиваемого

### 2.1 Шейдеры — счёт

Все шейдеры движка лежат в одном дереве: `Editor/Resources/Shaders/`. Под `Runtime/`, `Desert/`, `Tools/`
шейдеров нет (проверено `find`). `Editor/Cooked/ShaderCache/` — вывод компиляции, не исходники.

* `.shader`: **54**
* `.glslh`: **13**
* Всего: **67**

Из них объявляют или включают хотя бы один покадровый блок:

* `.shader`: **17 напрямую + 1 транзитивно = 18 из 54**
* `.glslh`: **6 из 13** (`CameraUB`, `LightsMetadata`, `PointLight`, `Spotlight`, `DirectionLightsUB`, `TimeUB`)
  + транзитивно `Common/GraphVertex.glslh` (включает `CameraUB.glslh` на `:14`)

### 2.2 Где блоки объявлены

Хорошая новость: шесть покадровых блоков объявлены **один раз** в общих заголовках, и все шесть номеров
проверены построчно.

| Блок | Binding | Объявлен | Потребителей |
|---|---|---|---|
| `CameraUB` | 0 | `Editor/Resources/Shaders/Common/CameraUB.glslh:1` | 17 шейдеров через `#include` |
| `LightsMetadata` | 4 | `Editor/Resources/Shaders/Mesh/LightsMetadata.glslh:4` | 6 |
| `PointLightsUB` (SSBO) | 6 | `Editor/Resources/Shaders/Mesh/PointLight.glslh:18` | 6 |
| `SpotLightsUB` (SSBO) | 16 | `Editor/Resources/Shaders/Mesh/Spotlight.glslh:20` | 6 |
| `DirectionLightsUB` | **14** | `Editor/Resources/Shaders/Common/DirectionLightsUB.glslh:10` | только генерируемые шейдер-графы |
| `TimeUB` | **15** | `Editor/Resources/Shaders/Common/TimeUB.glslh:3` | только генерируемые шейдер-графы |

Плохая новость: три оставшихся семейства продублированы построчно по файлам.

| Блок | Binding | Копий |
|---|---|---|
| `DirectionLightsUB` **встроенный**, binding **3** (не 14) | 3 | 5 |
| `ShadowUB` | 7 | 6 |
| `u_ShadowMap0..3` | 5, 13, 14, 15 | 6 файлов × 4 = 24 объявления |
| `u_EnvSpecularTex` / `u_EnvIrradianceTex` / `u_BRDFLUTTexture` | 8, 9, 10 | 5 файлов × 3 = 15 объявлений |

### 2.3 Восемнадцать `.shader` с покадровыми блоками

Пути от `Editor/Resources/Shaders/`. Номера строк перепроверены.

| Файл | Покадровое | Bindings |
|---|---|---|
| `Programs/PBR/StaticMeshPBR.shader` | CameraUB (incl.), PointLight/Spotlight/LightsMetadata (incl.), DirectionLightsUB `:140`, u_ShadowMap0-3 `:145-148`, ShadowUB `:149`, Env/BRDF `:265,266,269` | 0,3,4,5,6,7,8,9,10,13,14,15,16 |
| `Programs/PBR/StaticMeshPBR_Instanced.shader` | те же: `:16 / :74-76 / :124 / :129-132 / :133 / :228,229,232` | те же |
| `Programs/PBR/StaticMeshGBuffer.shader` | `:14 / :69-71 / :104 / :106-109 / :110 / :117-119` | те же |
| `Programs/PBR/StaticMeshGlass.shader` | `:15 / :68-70 / :96 / :98-101 / :102 / :109-111` | те же |
| `Programs/PBR/SkinnedMeshPBR.shader` | `:15 / :83-85 / :133 / :138-141 / :142 / :237,238,241` | те же |
| `Programs/Deferred/DeferredLighting.shader` | PointLight `:27`, Spotlight `:28`, LightsMetadata `:29`, u_ShadowMap0-3 `:125-128`, ShadowUB `:129`; **без CameraUB и без Env** — камера и солнце внутри `DeferredUB` binding 0 `:70` | 4,5,6,7,13,14,15,16 |
| `Programs/Shadow/Shadow.shader` | CameraUB `:24` | 0 |
| `Programs/Shadow/Shadow_Instanced.shader` | CameraUB `:28` | 0 |
| `Programs/Silhouette/Silhouette.shader` | CameraUB `:11` | 0 |
| `Programs/Silhouette/Silhouette_Skinned.shader` | CameraUB `:16` | 0 |
| `Programs/Skybox/Skybox.shader` | CameraUB `:27` | 0 |
| `Programs/Text/TextSDF.shader` | CameraUB `:31` | 0 |
| `Programs/Unlit/Unlit.shader` | CameraUB `:30` **и** `:75` (проход "Depth") | 0 |
| `Programs/Debug/DebugLine.shader` | CameraUB `:9` | 0 |
| `Programs/Debug/Overdraw.shader` | CameraUB `:16` | 0 |
| `Programs/Particles/ParticleBillboard.shader` | CameraUB `:10` | 0 |
| `Programs/ProceduralSky/ProceduralSky.shader` | CameraUB `:75` | 0 |
| `Programs/Graph/NewShaderGraph.shader` | CameraUB транзитивно через `Common/GraphVertex.glslh:14`, подключён на `:20` и `:46` | 0 |

**Ни один compute-шейдер не объявляет покадровый блок.** Ближайший случай — `Programs/Grass/GrassCull.shader:30`,
которому нужна камера, но он берёт её push-константой `mat4 u_MVP`, а не привязанным блоком.

### 2.4 Занятые номера привязок

* Покадровые: **13 штук** — `0, 3, 4, 5, 6, 7, 8, 9, 10, 13, 14, 15, 16`
* Материальные / на отрисовку: `2` (Materials SSBO), `11` (albedo), `12` (normal), `17` (`InstanceTransforms`,
  `StaticMeshPBR_Instanced.shader:28` и `Shadow_Instanced.shader:34`), `18` (opacity), `19` (scene colour,
  только Glass)

**Внутри одного файла пересечения нет** — переезд в другой набор будет чистой перенумерацией, без разбора
коллизий. Но **между файлами один и тот же номер значит разное**: в PBR binding 3 — это `DirectionLightsUB`, а в
`DeferredLighting.shader:35` — `u_GBufferB`; bindings 8/9/10 в PBR — это Env/BRDF, а в `DeferredLighting`
`:36,37,40` — SSAO, emissive, GI. Это существенно: покадровый набор, чтобы быть **одним** набором на всех, должен
иметь одинаковую раскладку у всех, кто его объявляет, — а `DeferredLighting` объявляет подмножество (нет
`CameraUB`, нет Env). См. 4.0.

### 2.5 Материалы

`Desert/Desert/Source/Engine/Graphic/Materials/` — **43 `.hpp`**. Дескрипторными наборами не владеет ни один из
них напрямую: все получают `VulkanMaterialBackend` через `MaterialExecutor`
(`Materials/MaterialExecutor.cpp:114` и `:132`).

Покадровое состояние трогают: `Mesh/PBR/MaterialPBRBase`, `Mesh/PBR/StaticMaterialPBR`,
`Mesh/PBR/SkinnedMaterialPBR` (`SkinnedMaterialPBR.cpp:10-11`), `Mesh/PBR/MaterialGlass`, `Mesh/PBR/MaterialRSM`,
`Mesh/MaterialShadow`, `Mesh/MaterialSilhouette`, `Skybox/MaterialSkybox`, `Skybox/MaterialProceduralSky`,
`Particles/MaterialParticleBillboard`, `Debug/MaterialDebugLine`, `Debug/MaterialOverdraw`,
`Deferred/MaterialDeferredLighting`, `DataDrivenMaterial` (через `DrawGenericMeshes`).

### 2.6 Все места создания и привязки дескрипторных наборов

В коде движка (без ThirdParty) ровно **11** живых обращений к сырому Vulkan API дескрипторов — перепроверено
одним `grep` по шести именам функций:

| Вызов | Файл:строка | Владелец |
|---|---|---|
| `vkCreateDescriptorSetLayout` | `Graphic/API/Vulkan/VulkanShader.cpp:152` | `VulkanShader::CreateDescriptorsLayout` |
| `vkCreatePipelineLayout` | `Graphic/API/Vulkan/VulkanPipeline.cpp:186` | `VulkanPipeline::CreatePipelineLayout` |
| `vkCreateDescriptorPool` | `Graphic/API/Vulkan/VulkanMaterialBackend.cpp:118` | `CreateDescriptorPool` |
| `vkAllocateDescriptorSets` | `VulkanMaterialBackend.cpp:151` | `AllocateDescriptorSets` |
| `vkUpdateDescriptorSets` | `VulkanMaterialBackend.cpp:180` | `UpdateDescriptorSets` |
| `vkCmdBindDescriptorSets` | `VulkanMaterialBackend.cpp:325` | `BindDescriptorSets`, `firstSet = 0`, весь массив |
| `vkCmdBindDescriptorSets` | `VulkanPipelineCompute.cpp:172` | `firstSet = 0`, ровно 1 набор |
| `vkCreateDescriptorPool` | `VulkanPipelineCompute.cpp:255` | кольцо на 64 набора |
| `vkAllocateDescriptorSets` | `VulkanPipelineCompute.cpp:265` | 64 копии **раскладки набора 0** (`:257`) |
| `vkCreatePipelineLayout` | `VulkanPipelineCompute.cpp:311` | compute |
| `vkUpdateDescriptorSets` | `VulkanPipelineCompute.cpp:375` | `UpdateDescriptorSet` |

Плюс пул ImGui: `Graphic/API/Vulkan/imgui/VulkanImGuiLayer.cpp:74`, отдаётся `ImGui_ImplVulkan_Init`. Наборы
ImGui создаются внутри вендорного бэкенда и с материальным путём не пересекаются.

Закомментированный код `Graphic/API/Vulkan/VulkanUtils/lightweightvk/VulkanClasses.cpp` (строки 5121, 5277,
5486, 7539, 7560, 7567, 7708, 7910-7914) — мёртвый, весь под `//`.

---

## 3. Что умеет рефлексия и привязка сегодня

### 3.1 Множественные наборы: рефлексия умеет всё

`spv::DecorationDescriptorSet` читается для **каждого** класса ресурсов —
`Graphic/API/Vulkan/VulkanShaderReflection.cpp:104` (UB), `:134` (sampled images), `:175` (SSBO),
`:192` (storage images):

```cpp
uint32_t set     = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
uint32_t binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
auto&    ub      = data.ShaderDescriptorSets[set].UniformBuffers[binding];
```

Контейнер — настоящая карта по набору: `Graphic/API/Vulkan/VulkanShaderResource.hpp:110`,
`std::unordered_map<SetPoint, ShaderDescriptorSet> ShaderDescriptorSets`, где `SetPoint` = `unsigned int`
(`Graphic/RendererTypes.hpp:7`). Внутри — семь корзин по типу ресурса (`VulkanShaderResource.hpp:44-50`).

### 3.2 Раскладка по номеру набора: есть

`Graphic/API/Vulkan/VulkanShader.hpp:64-84`:

```cpp
const auto& GetDescriptorSetLayout( uint32_t set ) const;   // индекс вектора == номер набора
const auto  GetDescriptorSetLayoutCount() const;
const auto& GetAllDescriptorSetLayouts() const;
```

Хранилище — `std::vector<VkDescriptorSetLayout> m_DescriptorSetLayouts;` (`VulkanShader.hpp:130`).
Раскладки строятся **по одной на набор**, не сливаются: `VulkanShader.cpp:131-155`, с
`if ( setIndex >= m_DescriptorSetLayouts.size() ) m_DescriptorSetLayouts.resize( setIndex + 1 );` на `:151`.

### 3.3 С какого набора начинается привязка

С **нулевого**, всегда, и весь массив разом:

* графика — `VulkanMaterialBackend.cpp:325`, `firstSet = 0`, `descriptorSetCount = setsToBind.size()`;
* compute — `VulkanPipelineCompute.cpp:172`, `firstSet = 0`, `count = 1`.

Привязка происходит **на каждую отрисовку**: шесть вызовов `BindDescriptorSets` из `VulkanRenderer.cpp`
(`:242`, `:341`, `:381`, `:426`, `:460`, `:502`), каждый непосредственно перед своим `vkCmdDraw*`. Это
важно для 4.0: сегодня совместимость раскладок пайплайна не имеет значения, потому что ничего не переживает
смену пайплайна.

### 3.4 Что изменила недавняя переписка рефлексии

`git log -- '*VulkanShaderReflection*'` даёт ровно один содержательный коммит: `07c786d` («render: classify
shader image resources by type, not by name»); `b23a7f7` — его merge, содержимое то же.

Что изменилось: классификация картинок по `type.image.dim` (`VulkanShaderReflection.cpp:21-44`, `ClassifyImage`)
вместо поиска подстрок `"Env"`/`"Cube"` в имени; появились корзины `Image3DSamplers` и `StorageImage3DSamplers`;
неподдерживаемые ресурсы теперь дают диагностику и **шейдер не загружается** — `Reflect` сменил тип с `void` на
`Common::BoolResultStr` (`VulkanShader.cpp:104-129`).

**Работа с наборами переписка не тронула вообще.** Чтение `DecorationDescriptorSet` и индексация
`ShaderDescriptorSets[set]` существовали до неё в том же виде. Единственное новое взаимодействие с номером
набора — косметическое: номер подставляется в текст диагностики.

Таким образом утверждение старого документа «рефлексия уже умеет множественные наборы» — **по-прежнему верно**,
и от переписки оно не пострадало. Более того, переписка нам **помогла**: `CollectImageBindings`
(`VulkanShaderResource.hpp:84-104`) — чистая функция над одним набором, уже принимающая набор как аргумент.

### 3.5 Что рефлексия НЕ умеет

| Возможность | Статус |
|---|---|
| Массивы дескрипторов (`sampler2D u_T[4]`) | **Явно отвергаются** с диагностикой — `VulkanShaderReflection.cpp:140-147` и `:196-203`; все раскладки хардкодят `descriptorCount = 1` (`VulkanShader.cpp:138`) |
| Массивные картинки (`sampler2DArray`), multisample | Отвергаются в `ClassifyImage` (`:28-31`) |
| Раздельные sampler / sampled image (`texture2D` + `sampler`) | **Не обрабатываются никак.** `resources.separate_images` и `resources.separate_samplers` не читаются — ни привязки, ни диагностики. Единственный оставшийся путь «тихой потери» |
| Storage images | Да; 2D и cube в одной корзине, 3D отдельно (`:190-223`) |
| 3D-образы | Да, и sampled (`:156`), и storage (`:213`) |
| Донести номер набора до **моделей ресурсов** | **Нет.** См. 4.10 — номер живёт только ключом карты |

---

## 4. Что именно сломается при переезде

### 4.0 Главное, чего не знают ни старый документ, ни прошлый заход: совместимость раскладок пайплайна

Оба текста исходят из того, что покадровый набор привязывается **один раз в начале прохода** и держится
через все отрисовки. В Vulkan это верно только при выполнении условия, которого здесь нет.

Правило Vulkan: две раскладки пайплайна совместимы «для набора N», только если они созданы с **идентичными
раскладками наборов 0..N** и с **идентичными диапазонами push-констант**. Привязка набора N переживает смену
пайплайна лишь при такой совместимости; иначе она **сбрасывается**.

Что в коде:

* Раскладка пайплайна собирается из **всех** наборов шейдера плюс его собственный push-константный диапазон:
  `VulkanPipeline::CreatePipelineLayout` (`Graphic/API/Vulkan/VulkanPipeline.cpp:170-187`), диапазон —
  `SetUpPushConstantRange` (`:405-423`), максимум один, прямо из рефлексии.
* Диапазоны **разные**: `Programs/Shadow/Shadow.shader:27-30` — `mat4 Transform` (64 Б);
  `Programs/PBR/StaticMeshPBR.shader:53-57` — `mat4 Transform` + `uint MaterialIndex` (68 Б);
  `Programs/Deferred/DeferredLighting.shader` и `Programs/Skybox/Skybox.shader` — push-констант нет вовсе
  (`SetUpPushConstantRange` вернёт `{0, {}}`).

Следствия, оба обязательные к учёту:

1. **Вариант «покадровые в набор 1, материал в набор 0» — заведомо худший из двух.** Совместимость «для
   набора 1» требует **совпадения раскладки набора 0**, а набор 0 — это как раз материал, и он у каждого свой.
   То есть привязка набора 1 сбрасывалась бы при каждой смене материала. Именно этот вариант рекомендовал
   прошлый заход (как «более дешёвый: 36 шейдеров без покадровых блоков не трогаются»). **Он неверен.**
2. **Правильная нумерация — та, что в старом документе: покадровое в набор 0, материал в набор 1**
   (`Docs/RENDERER_FRAME_STATE.md:99`). Совместимость «для набора 0» не зависит от раскладки материала. Но и
   этого мало: **push-константные диапазоны всё равно различаются**, а они входят в определение совместимости.
   Значит даже при покадровом в наборе 0 привязка «раз на проход» не переживёт смену пайплайна, пока
   диапазоны не приведены к одному виду.

Практический вывод: **«владеет рендерер» и «привязывается один раз за проход» — два независимых решения, и
брать надо только первое.** Достаточно, чтобы набор *принадлежал* рендереру (он его выделяет, он в него
пишет, материал в него не лезет) — а привязывать его можно по-прежнему на каждую отрисовку, тем же
`vkCmdBindDescriptorSets`, одним вызовом вместе с материальным набором (`firstSet = 0`, два набора в массиве).
Класс багов, ради которого всё затевается, лечится владением, а не редкостью привязки. Экономия на числе
привязок — отдельная, более поздняя оптимизация, которая потребует унификации push-констант.

Проверить это здесь нельзя (нет GPU) — это чтение спецификации, приложенное к прочитанному коду. Но ошибка
такого рода проявляется как «набор внезапно содержит мусор после смены материала», то есть ровно как баг, от
которого мы уходим, — и потому названа первой.

### 4.1 Настоящий блокер в коде: запись дескрипторов прибита к набору 0

Четыре раза подряд, дословно:

```cpp
const uint32_t setIndex = 0; // Simplified
```

`VulkanMaterialBackend.cpp:194` (`ApplyUniformBuffer`), `:223` (`ApplyStorageBuffer`), `:251` (`ApplyTexture2D`),
`:279` (`ApplyTextureCube`). И соответствующие вызовы построителя записи передают литеральный `0` как набор:
`:206`, `:234`, `:262`, `:290`. Ресурс, объявленный в наборе 1, был бы записан в дескриптор набора 0.

Плюс охранник дедупликации читает `m_DescriptorSetsUpdateFrame[frame][slot][0]` (`:197`, `:225`, `:253`, `:281`),
а `FlushUpdates` помечает свежими **все** наборы слота разом (`:344-348`) — с двумя наборами обновление одного
объявило бы свежим и второй.

Обратите внимание: **половина, отвечающая за выделение, уже понимает наборы.**
`CreateDescriptorPool` (`:70-120`), `AllocateDescriptorSets` (`:122-157`) и `InitializeWithFallbacks`
(`:351-496`, цикл `for ( const auto& [setIndex, descriptorSet] : descriptorSets )` на `:366` с прокидыванием
`setIndex` в `GetUniformWDS`/`GetStorageWDS`/`GetImageWDS` на `:390`, `:400`, `:422`, `:434`, `:448`, `:463`,
`:476`) — все set-aware. Не понимает наборы только половина, отвечающая за запись.

### 4.2 Дыра с `VK_NULL_HANDLE` при разрежённых наборах

`VulkanShader.cpp:151-152` растит вектор через `resize( setIndex + 1 )`. Если шейдер объявит ресурсы **только**
в наборе 1, элемент 0 останется `VK_NULL_HANDLE` — и уедет прямо в `vkCreatePipelineLayout`
(`VulkanPipeline.cpp:180-181`, `VulkanPipelineCompute.cpp:293-294`) как невалидный хэндл, а не как «пустой
набор». Нужен явный заполнитель пустой раскладкой. Это ударит по любому шейдеру, у которого **нет** ресурсов
одного из наборов, но есть другого, — а после переезда таких будет много.

### 4.3 Материалы без покадровых блоков

36 из 54 `.shader` не объявляют ни одного покадрового блока (пост-обработка, JFA, compute, UI). При нумерации
«покадровое = набор 0» (см. 4.0) у них набор 0 окажется **пустым или отсутствующим**, а материальные ресурсы
переедут в набор 1 — то есть их придётся трогать тоже, вопреки надежде прошлого захода сэкономить. Либо
объявлять им пустой набор 0 явно (одна строка в `CreatePipelineLayout` плюс заполнитель из 4.2), либо
оставлять их ресурсы в наборе 0 и мириться с тем, что номер набора значит разное у разных шейдеров — второе
дёшево сегодня и дорого потом.

Это — главное подорожание относительно оценки прошлого захода: правильная нумерация делает работу шире,
а не уже.

### 4.4 Порядок привязки

`BindDescriptorSets` (`:299-327`) сейчас привязывает **весь** массив наборов материала с `firstSet = 0`. После
переезда материал обязан не трогать чужой набор — иначе он затрёт покадровый. Правка одного цикла, но она
касается всех шести вызовов из `VulkanRenderer.cpp`. Если принять вывод 4.0 (привязывать оба набора одним
вызовом на отрисовку), то правка ещё проще: массив собирается из «покадровый набор рендерера + наборы
материала», и `firstSet` остаётся нулём.

### 4.5 Push-константы

Сами по себе не ломаются: `SetUpPushConstantRange` (`VulkanPipeline.cpp:405-423`) возвращает максимум **один**
диапазон, собранный рефлексией (`VulkanShaderReflection.cpp:226-250`) и никак не связанный с номерами наборов.
Пер-объектные значения ездят именно так (`Materials/Mesh/PBR/PBRPush.hpp`).

**Но** они входят в определение совместимости раскладок (4.0). Если когда-нибудь захочется привязывать
покадровый набор один раз на проход, диапазоны придётся унифицировать — общий «максимальный» диапазон на все
освещённые шейдеры. Сегодня этого делать не нужно; знать нужно.

### 4.6 Compute-проходы

`VulkanPipelineCompute` целиком живёт в наборе 0: `GetDescriptorSetLayout( 0 )` (`:257`),
`backend->GetDescriptorSet( 0, 0 )` (`:200`), `firstSet = 0, count = 1` (`:172`), параметр `setIndex` в
`UpdateDescriptorSet` принимается и **не используется** (`:367`). Но, как показано в 2.3, **ни один
compute-шейдер покадровых блоков не объявляет** — переделка их не трогает. Достаточно не сломать существующее:
если compute-шейдер вдруг окажется с ресурсами не в наборе 0, `layout0` будет `VK_NULL_HANDLE` (4.2).

### 4.7 Внешние проходы редактора

ImGui владеет собственным пулом и собственной раскладкой внутри вендорного бэкенда
(`Graphic/API/Vulkan/imgui/VulkanImGuiLayer.cpp:54-96`) — независим полностью. Проходы `EditorGridPass`,
`EditorColliderPass`, Render2D, пост-обработка, JumpFlood — все ходят через `VulkanMaterialBackend` и
наследуют общий путь; отдельного кода привязки у них нет. Отдельного риска они не создают, но попадают под
4.3: их шейдеры покадровых блоков не имеют.

### 4.8 Постоянные storage-буферы: правило НЕ ломается, но описано неверно

Правило «постоянные storage-буферы сознательно общие для всех рендереров» реализовано в
`Desert/Desert/Source/Engine/ShaderResources/API/Vulkan/VulkanStorageBuffer.cpp:62-68`:
`copies = m_Persistent ? 1u : framesInFlight * slots`, и все `(frame × slot)` дескрипторы указывают на буфер 0
(`:98-104`); `SetData`/`MapMemory` берут индекс 0 (`:123`, `:130`).

Переезд покадровых блоков в отдельный набор этому не мешает: постоянные буферы — это **не** покадровое
состояние сцены, и в покадровый набор они не поедут. Более того, вариант A делает правило **менее** хрупким:
исчезает измерение «слот», и «постоянный» перестаёт означать «исключение из размножения по слотам» —
останется только исключение из размножения по кадрам, что и есть настоящий смысл флага.

Важная поправка к старому документу: **симуляция травы постоянной не является.** Единственный постоянный
буфер во всём движке — `ParticleState`
(`Graphic/Systems/Scene/Particles/ParticleRenderer.cpp:119-120`, `/*persistent=*/true`). `GrassIndirect`
(`Graphic/Systems/Scene/Terrain/TerrainRenderer.cpp:428`) и `GrassVisible` (`:435`) создаются с
`persistent = false` по умолчанию (`ShaderResources/StorageBuffer.hpp:30`), то есть уже размножены по
`(кадр × слот)`. Если травяная симуляция действительно должна переживать смену вида, сегодня она этого не
делает — см. находку F10.

### 4.9 DSL шейдеров: `set` умеет только `Uniform`

Сахар `Uniform(s, n)` → `layout(set = s, binding = n) uniform` **уже существует**:
`Desert/Desert/Source/Engine/Core/ShaderCompiler/DShader/DShaderParser.cpp:810` (документация) и `:835-836`
(правило). Порядок правил верный — двухаргументное идёт раньше одноаргументного (`:837`).

Но:

* **`Buffer(n)` / `ReadBuffer(n)` / `WriteBuffer(n)` формы с набором не имеют** (`DShaderParser.cpp:838-843`).
  А `PointLightsUB` и `SpotLightsUB` — именно `ReadBuffer`. Значит либо расширять DSL, либо писать сырое
  `layout(std430, set = 1, binding = n) readonly buffer` (сырой `layout()` проходит насквозь, `:806`).
* **Автораспределитель привязок не разделяет наборы.** `DShaderParser.cpp:860` собирает занятые номера одним
  регэкспом `binding\s*=\s*(\d+)` в единое множество `usedBind` — без понятия о наборе. То есть `Uniform(1, 0)`
  займёт binding 0 для **всей стадии**, и следующий безскобочный `Uniform` в наборе 0 получит 1, а не 0.
  Комментарий на `:818-823` прямо описывает «три независимых пространства» — наборов среди них нет.
* `DShaderTool` (`Tools/DShaderTool/Source/Main.cpp`, 108 строк) — только линтер парсинга: собирает `*.shader`,
  проверяет `IsDShader`, зовёт `DShaderParser::Parse`. Правил про наборы у него нет — препятствием он не будет.

Сегодня `set` в дереве встречается только как **ноль**: сырой `layout(set=0,...)` в трёх compute-шейдерах
(`Programs/Compute/PrefilterEnvMap.shader:14`, `Programs/Compute/PanoramaToCubemap.shader:9`,
`Programs/Compute/DiffuseIrradiance_4x3.shader:8`) и `Uniform(0, n)` в восьми местах (четыре JFA-шейдера и те же
три compute). `set = 1` не использует никто.

### 4.10 Модели ресурсов теряют номер набора

`VulkanShader::GetUniformBufferModels / GetStorageBufferModels / GetUniformImageCubeModels /
GetUniformImage2DModels` (`VulkanShader.cpp:157-187`) **схлопывают все наборы в один плоский список** — цикл
`for ( const auto& [set, dSet] : ... )` с `push_back` и выброшенным `set`. А `ShaderResourcesManager`
(`Desert/Desert/Source/Engine/ShaderResources/ShaderResourcesManager.cpp:102-142`) по этим спискам создаёт
буферы и картинки для материала — значит материал продолжит создавать объекты и для блоков покадрового набора,
если их не отфильтровать.

Хуже: поле `DescriptorSet` есть только у `Image2DSampler` / `Image3DSampler` / `ImageCubeSampler`
(`Desert/Desert/Source/Engine/ShaderResources/ShaderReflectionTypes.hpp:44, 58, 66`), **у `UniformBuffer` и
`StorageBuffer` его нет вовсе** (`:22-40`). И даже там, где поле есть, оно **никогда не заполняется** — во всём
`VulkanShaderReflection.cpp` нет ни одного присваивания `DescriptorSet` (проверено `grep`). Номер набора живёт
исключительно ключом карты.

Отдельно: getter для 3D-образов отсутствует вовсе — есть `Image3DSamplers` в рефлексии, но нет
`GetUniformImage3DModels`; фолбэки для них выдаёт `InitializeWithFallbacks`, а `ShaderResourcesManager` о них
не знает. Не наш дефект, но соседний.

Это — вторая обязательная предварительная работа наряду с 4.1: провести номер набора до моделей ресурсов.

---

## 5. Что движется прямо сейчас в этой области

Параллельно выполняется программа «Небо и облака» — документы в `Docs/Clouds/`
(`DEV_CONTRACT.md`, `REQUIREMENTS_SKY.md`, `REQUIREMENTS_CLOUDS.md`, `RESEARCH_ENGINE.md`,
`RESEARCH_REFERENCE.md`, `WORK_BREAKDOWN.md`).

### 5.1 Что уже в `dev`

По `git log --first-parent dev`, снизу вверх: `b23a7f7` (T4 — классификация ресурсов-картинок по типу SPIR-V),
`2be4637` (небо и облака становятся ECS-компонентами), `80b2abb`/`e9fab81` (радиус планеты у атмосферы),
`4b399c2` + `a81793e` + `815e7a8` + `47659e6` (T3 — объёмные образы, `RGBA16F`, таблица форматов, фолбэк для
3D-привязки), `3771019` (clang-format), `4a8693c`/`e34783f` (T2 — миграция настроек неба в сценах).

Из этого нашей территории касаются двое, и оба — в плюс:

* **T4 `07c786d`/`b23a7f7`** — рефлексия. Разобрано в 3.4: наборов не тронула, но дала чистую, тестируемую
  без устройства функцию `CollectImageBindings( set )`.
* **T3 `47659e6`** — фолбэки для 3D-привязок; расширил set-aware половину `InitializeWithFallbacks`.

### 5.2 Что делается прямо сейчас — T6, и оно рядом

`Docs/Clouds/WORK_BREAKDOWN.md:73-77`, задача T6 («Небо: проход, солнце, IBL»):

> Процедурное небо уезжает из `Skybox` (старый путь удаляется целиком); блок параметров неба как
> std430 SSBO; `AtmosphereEnv` с непрозрачной ручкой буфера; правило выбора атмосферного солнца
> (`SystemRules`, шесть случаев); драйвер времени суток; автоперепекание IBL.

**T6 не «может начаться» — она идёт, незакоммиченной в `dev`, в ветке `worktree-agent-af898f0b361833a95`**
(коммиты `160bc67` «wip(T6): checkpoint — … NOT for dev», `4d6ef4a` merge dev, `5627329` «wip(T6): sun
ownership, time of day, and the editor's sun inversion removed»). Состояние по её собственному описанию:
движок компилируется, редактор — нет.

Что уже сделано в T6 и прямо задевает наши файлы:

* `Editor/Resources/Shaders/Programs/ProceduralSky/ProceduralSky.shader` переписан: `Uniform(1) SkyUB` заменён
  на `ReadBuffer(1) SkyBuffer` (std430 SSBO) с массивом `u_SkyPacked[]`; `#include <Common/Clouds.glslh>` убран.
* `Editor/Resources/Shaders/Common/Clouds.glslh` **удалён** (в `dev` он ещё на месте — 13 `.glslh` из 2.1
  посчитаны по `dev`; после T6 их станет 12).
* `Editor/Resources/Shaders/Programs/Compute/BakeProceduralSky.shader` читает тот же буфер.
* `Graphic/CloudSettings.hpp` удалён; появились `SkyPayload.hpp`, `AtmosphereEnv.hpp`, `SkyRules.hpp`.
* Тронуты **все пять** редакторских создателей `SceneRenderer` (`PreviewViewport.cpp`,
  `AssetThumbnailRenderer.cpp`, `EditorLayer.cpp`, `LightGizmoRenderer.cpp`, `RuntimeLayer.cpp`) — но только по
  сигнатуре `SetProceduralSky`; ни слотов, ни владения набором T6 не касается.

Отдельно стоит отметить комментарий, который T6 оставила в `ProceduralSky.shader` над новым SSBO: номер
привязки прописан явно и «должен оставаться равным `Graphic::kSkyPayloadBinding`, потому что графическая запись
берёт **собственный** номер буфера, а compute-диспатч получает номер аргументом, и при расхождении буфер тихо
садится на чужой слот». Это ровно наш класс дефекта, только по оси binding, а не по оси set — и подтверждение,
что `ReadBuffer` без формы с набором (4.9) уже мешает соседям.

### 5.3 Что читать после стабилизации

* `Editor/Resources/Shaders/Programs/ProceduralSky/ProceduralSky.shader` и
  `Editor/Resources/Shaders/Programs/Compute/BakeProceduralSky.shader` — **не трогать до приземления T6.**
  Механическое разрешение конфликта в этих двух файлах закончится потерей чужой работы.
* `Editor/Resources/Shaders/Common/Atmosphere.glslh`, `Common/Clouds.glslh` — счёт `.glslh` пересчитать после T6.
* `Desert/Desert/Source/Engine/Graphic/SkyPayload.hpp` и `AtmosphereEnv.hpp` — там появляется **второй** в
  движке «непрозрачный буфер, общий для проходов». Стоит проверить, не завёл ли он собственное покадровое
  состояние в материале — то есть новый экземпляр той же болезни.

Остальные 16 шейдеров из 2.3 к небу отношения не имеют и безопасны.

Волна 2 (`WORK_BREAKDOWN.md:85-90`, T8 — weather map и реймарч) придёт в те же файлы позже. Окно для нашего
шага 5 (см. 7.3) — между приземлением T6 и стартом T8.

---

## 6. Что можно проверить без GPU

В этой среде Vulkan нет (`glfwVulkanSupported` не проходит), редактор не запускается. Честное разделение.

### 6.1 Проверяется здесь, полностью

Тестовых проектов **30** (`find Desert/Tests -name premake5.lua` → 31 файл, минус корневой
`Desert/Tests/premake5.lua`). Три из них прямо накрывают нашу территорию, и все три работают **без
устройства**: компилируют GLSL через `shaderc` и скармливают SPIR-V рефлексии, не создавая ни `VkDevice`, ни
единого хэндла.

| Тест | Строк | Что делает |
|---|---|---|
| `Desert/Tests/Engine/ShaderReflection/shader_reflection_test.cpp` | 314 | компиляция через `shaderc::Compiler` (`:24-39`), проверка классификации по типу |
| `Desert/Tests/Engine/DShaderParser/dshader_parser_test.cpp` | 603 | трансляция сахара DSL в GLSL, включая сохранение явных привязок |
| `Desert/Tests/Engine/DescriptorFallbacks/descriptor_fallbacks_test.cpp` | 164 | что каждая объявленная привязка получает фолбэк нужного вида |

Это не случайность: `VulkanShaderResource.hpp:105-107` прямо документирует, что `ReflectionData` не держит
Vulkan-хэндлов — «which is what makes the classification testable off-GPU», а комментарий
`descriptor_fallbacks_test.cpp:10-12` формулирует приём: «устройство — это то, чего на этой машине быть не
может, поэтому тестируется ПЕРЕЧИСЛЕНИЕ, на котором проход фолбэков себя и строит».

Тот же приём годится нам целиком. Что из переделки принимается здесь:

1. **Раскладки.** Скомпилировать каждый из 18 шейдеров 2.3 и утверждать, что покадровые ресурсы попали в
   покадровый набор, а материальные — в материальный. Чистая проверка `ReflectionData`.
2. **Одинаковость покадрового набора.** Утверждение, которое 4.0 делает обязательным: у **всех** шейдеров,
   объявляющих покадровый набор, его раскладка совпадает побитово (тот же список `(binding, тип, стадии)`).
   Это чистая функция над `ReflectionData` — и именно она поймала бы подмножество в `DeferredLighting` (2.4).
3. **Сахар DSL.** Что `Uniform(s, n)`, а после расширения и `ReadBuffer(s, n)`, дают правильный GLSL; что
   автораспределитель привязок разделяет наборы (сейчас не разделяет — 4.9).
4. **Отсутствие коллизий привязок внутри набора** — чистая функция над `ReflectionData`.
5. **Дыра с `VK_NULL_HANDLE`** (4.2) — логика «какие номера наборов объявил шейдер и какие надо заполнить
   пустыми» выделяется в чистую функцию и тестируется без устройства.
6. **Единственность номера набора у моделей ресурсов** (4.10) — после шага 0 проверяется тем же тестом.
7. **`DShaderTool`** прогоняется по всему дереву шейдеров без GPU — гарантия, что перенумерация ничего не
   сломала синтаксически.
8. **Сборка и `clang-format` v18** — обычные CI-ворота.

### 6.2 Проверяется только запуском с валидационными слоями

* Что `vkCmdBindDescriptorSets` совместим с раскладкой пайплайна
  (VUID-vkCmdBindDescriptorSets-pDescriptorSets-00358 и родственные).
* **Что покадровый набор действительно переживает смену пайплайна** — если авторы всё же попробуют привязку
  «раз на проход» вопреки 4.0. Валидационные слои это ловят
  (VUID-vkCmdDraw-None-08600 и родственные «descriptor set … not bound / disturbed»).
* Что ни один дескриптор не остаётся незаписанным перед использованием (сегодня это гарантирует
  `InitializeWithFallbacks`, `VulkanMaterialBackend.cpp:351-496`).
* Что тип дескриптора совпадает с объявленным (историческая ошибка такого рода задокументирована в
  `Graphic/Systems/Scene/Particles/ParticleRenderer.cpp:113-117`: VUID-VkWriteDescriptorSet-descriptorType-00319).
* Собственно результат: два вида, две картинки, ни один не портит другой.

### 6.3 Как это определяет приёмку

Принимать в двое ворот.

**Ворота 1 (здесь).** Новые тесты в `Desert/Tests/Engine/ShaderReflection` или новый набор
`Desert/Tests/Engine/DescriptorSets`, пункты 1-6 выше; прогон `DShaderTool` по всему дереву; зелёная сборка;
`clang-format` v18. Пункт 2 (одинаковость раскладки покадрового набора) сделать **обязательным** — он
единственный ловит расхождение, которое иначе всплывёт как мусор в буфере на чужой машине.

**Ворота 2 (у пользователя, с валидационными слоями).** Скриншот вьюпорта с открытым превью в панели свойств
и вторым окном сцены, плюс пустой лог `VulkanDebugCallback`. Без вторых ворот работа не принимается — это не
формальность, а единственный способ увидеть класс ошибок из 6.2.

Доля, честно: **раскладки и рефлексия — почти целиком здесь; владение и привязка — почти целиком там.**
Шаги 0-4 из 7.3 принимаются воротами 1 полностью; шаги 5-6 — только воротами 2.

---

## 7. Оценка, риски, рекомендация

### 7.1 Вариант A против оставления B

| | A: покадровое — в набор рендерера | B: оставить как есть |
|---|---|---|
| Цена в шейдерах | 18 `.shader` + 6 `.glslh`; при правильной нумерации (4.0, 4.3) — плюс пустой/переномерованный набор у остальных 36 | 0 |
| Цена в C++ | 4 строки `setIndex = 0` + номер набора в моделях ресурсов (4.10) + фильтр в `ShaderResourcesManager` + заполнитель пустой раскладки (4.2, 4.3) + `Buffer(s, n)` в DSL (4.9) + владение набором у `SceneRenderer` | 0 |
| Память | Слоты не нужны: `×6` исчезает у UB, непостоянных SSBO и наборов | `×6` навсегда |
| Аренда слотов | Удаляется целиком | Остаётся |
| Потолок числа видов | Пропадает (сейчас 6 при шести создателях — запас ноль, 1.4) | 6 |
| Обязанность помнить правило | Пропадает: новый покадровый ресурс объявляется в покадровом наборе, и это всё | Остаётся: забыл — баг вернулся |
| Ambient-состояние | Пропадает: «текущий слот» в синглтоне и две его перемотки (1.5) исчезают | Остаётся |
| Риск регрессии | Высокий и **широкий**: раскладка каждого освещённого шейдера | Нулевой |

Что перестаёт быть нужным при A, поимённо: `Engine::kMaxRendererSlots` (`Core/FrameManager.hpp:17`),
`EngineContext::{Get,Set}ActiveRendererSlot` (`Core/EngineContext.hpp:61-71`, поле `:109`), `s_SlotsInUse` +
`ClaimRendererSlot` + `ReleaseRendererSlot` + `SceneRenderer::m_RendererSlot`
(`Graphic/SceneRenderer.cpp:212-247`), `VulkanUniformBuffer::CopyIndex`
(`ShaderResources/API/Vulkan/VulkanUniformBuffer.cpp:21-26`), слотовое измерение в
`VulkanStorageBuffer::CopyIndex` (`.../VulkanStorageBuffer.cpp:134-139`), массивы по слотам в
`Materials/Properties/MaterialProperty.hpp:53-54` и `Materials/Properties/FieldProperty.hpp:131-132`,
множитель в `Materials/Properties/PropertyDirty.hpp:37`, слотовое измерение `m_DescriptorSets`
(`VulkanMaterialBackend.hpp:53`) и цикл перемотки слота в `InitializeWithFallbacks`
(`VulkanMaterialBackend.cpp:359-364`, `:494`).

### 7.2 Риски, по убыванию

1. **Совместимость раскладок пайплайна (4.0).** Не учтена ни старым документом, ни прошлым заходом. Если
   реализовать «набор рендерера привязывается раз на проход», он будет сбрасываться сменой пайплайна, и
   вернётся ровно исходный симптом — в новой одежде и без предупреждения. Смягчение: не экономить на
   привязках, привязывать оба набора вместе на каждую отрисовку.
2. **Выбор номера набора.** Из 4.0 следует: покадровое — **набор 0**, материал — набор 1. Обратный вариант
   (тот, что рекомендовал прошлый заход как «дешёвый») делает задачу нерешаемой в её же терминах.
3. **Дыра с `VK_NULL_HANDLE` (4.2).** Самый вероятный источник «всё падает при старте». Заполнитель пустой
   раскладкой обязан приехать **до** первого шейдера с непустым вторым набором.
4. **Столкновение с T6 (5.2).** Два шейдера неба переписываются прямо сейчас, в незавершённом состоянии, в
   отдельной ветке. Слияние вслепую = потеря чужой работы.
5. **Модели ресурсов без номера набора (4.10).** Тихая ошибка: материал создаст буфер для покадрового блока,
   никто не заметит, всё будет «почти работать».
6. **Автораспределитель привязок DSL, слепой к наборам (4.9).** Тоже тихая: привязки поедут не туда, ошибок
   компиляции не будет.
7. **Расхождение раскладки покадрового набора между шейдерами (2.4).** `DeferredLighting` объявляет
   подмножество. Лечится тестом 6.1(2) — но только если этот тест написан.
8. **Отсутствие проверки на GPU здесь (6.2).** Половина класса ошибок не видна до запуска.
9. **`ShaderDescriptorSet::operator bool()`** (`VulkanShaderResource.hpp:52-55`) возвращает
   `!UniformBuffers.empty()` — набор только из сэмплеров считается ложным. Сегодня не используется, но это
   заряженная мина ровно для мира с двумя наборами.

### 7.3 Рекомендация по порядку работ

**Вариант A стоит делать.** Причина не в памяти — лишние ~4,9 КБ на материал (1.6) не проблема, — а в том, что
B оставляет **правило, которое нужно помнить**: каждый, кто заводит покадровый ресурс, обязан знать про слоты,
и компилятор ему об этом не скажет. A убирает правило вместе с механизмом, вместе с потолком в шесть видов и
вместе с ambient-состоянием «текущий слот».

Но браться за A **не сегодня**: T6 живёт в тех же файлах и не закончена. Порядок — снизу вверх, каждый шаг
самостоятельно осмыслен и обратим.

**Шаг 0 (можно начинать сейчас, ни с чем не конфликтует).** Провести номер набора до моделей ресурсов:
добавить `DescriptorSet` в `ShaderLayout::UniformBuffer` и `StorageBuffer`
(`ShaderResources/ShaderReflectionTypes.hpp:22-40`), заполнять его в `VulkanShaderReflection.cpp` для всех пяти
классов ресурсов, перестать схлопывать наборы в `VulkanShader::Get*Models` (`VulkanShader.cpp:157-187`).
Проверяется тестом рефлексии, поведения не меняет.

**Шаг 1.** Снять хардкод набора в записи: `VulkanMaterialBackend.cpp:194/223/251/279` берут набор из ресурса,
а не из литерала; охранник дедупликации и `FlushUpdates` начинают различать наборы. Всё ещё все ресурсы в
наборе 0 — поведение не меняется, но код перестаёт врать.

**Шаг 2.** Заполнитель пустой раскладкой для пропущенных номеров наборов (4.2). Логика выделяется чистой
функцией и тестируется здесь.

**Шаг 3.** Расширить DSL: `Buffer(s, n)` / `ReadBuffer(s, n)` / `WriteBuffer(s, n)`, и научить
автораспределитель привязок разделять пространства по наборам (4.9). Проверяется `dshader_parser_test`.

**Шаг 4.** Извлечь дублированные блоки в заголовки: `ShadowUB` + `u_ShadowMap0..3` в один `.glslh`,
`u_Env*` + `u_BRDFLUTTexture` во второй, встроенный `DirectionLightsUB` (binding 3) — в третий. Это ~44
объявления в 6 файлах превращается в 3 файла. Поведения не меняет, набор всё ещё 0. **Это самый ценный шаг:**
после него шаг 6 становится правкой нескольких строк вместо полусотни, и он же делает возможным тест
«раскладка покадрового набора одинакова у всех» (6.1(2)).

**Шаг 5 (решение, не код).** Зафиксировать нумерацию: покадровое = набор 0, материал = набор 1 (4.0), и
зафиксировать, что привязка остаётся **на каждую отрисовку**, одним `vkCmdBindDescriptorSets` с `firstSet = 0`
и двумя наборами. Записать это в `Docs/RENDERER_FRAME_STATE.md` до того, как кто-то начнёт править шейдеры.

**Шаг 6 — собственно переезд.** Требует, чтобы **T6 уже приземлилась**. Заголовки объявляют покадровые блоки в
наборе 0, материальные ресурсы всех 54 шейдеров переезжают в набор 1; `SceneRenderer` выделяет покадровый набор
(`framesInFlight` копий, без слотов) и пишет в него; материал перестаёт создавать объекты для покадровых блоков
и перестаёт их привязывать. Один коммит, ревертируемый целиком.

**Шаг 7.** Снос аренды слотов — список в 7.1.

**Шаг 8.** Свести к одному пути семь мест, которые сегодня пишут `CameraUB` своим кодом (1.2), и восемь
шейдеров, которые держат камеру в собственных блоках (находка F8).

Шаги 0-5 идут **параллельно с T6**: они не трогают ни `ProceduralSky.shader`, ни `BakeProceduralSky.shader`.
Шаг 6 ставится в очередь за T6 и **перед** T8 (5.3).

Если на всё это нет ресурса — оставить B, но тогда обязательны две вещи: (1) починить устаревший комментарий
F1, чтобы следующий читатель не поверил ему; (2) написать в `SceneRenderer.hpp` правило «новый покадровый
ресурс обязан быть непостоянным storage- или uniform-буфером, иначе он общий на все виды» — сейчас это правило
нигде не записано, а помнить его должен каждый.

---

## 8. Находки — дефекты, замеченные по дороге (ничего не чинилось)

**F1. Устаревший комментарий про слоты.** `Graphic/SceneRenderer.hpp:73-75` утверждает: «Slots are claimed in
creation order and never reused; past kMaxRendererSlots they fold back to 0…». Код
(`SceneRenderer.cpp:212-247`) делает ровно обратное: слоты арендуются (младший свободный) и возвращаются в
деструкторе. Это описание версии **до** исправления `b02730b`.

**F2. Мёртвая константа `MAX_SETS = 1`.** `Graphic/API/Vulkan/VulkanShaderResource.hpp:17`. Ни одной ссылки во
всём дереве (проверено `grep` по `Desert`, `Editor`, `Runtime`). Не ограничивает ничего, но читается как
утверждение о невозможности множественных наборов — прямо противоположное правде.

**F3. `DescriptorSet` никогда не заполняется.** Поле есть у `Image2DSampler`, `Image3DSampler`,
`ImageCubeSampler` (`ShaderResources/ShaderReflectionTypes.hpp:44, 58, 66`), но во всём
`VulkanShaderReflection.cpp` нет ни одного присваивания — всегда 0. У `UniformBuffer` и `StorageBuffer` поля
нет вовсе.

**F4. `ShaderDescriptorSet::operator bool()` смотрит только на UB.** `VulkanShaderResource.hpp:52-55`. Набор из
одних сэмплеров даёт `false`. Сегодня не используется.

**F5. `GetDescriptorSetLayout` возвращает ссылку на функциональный `static`.** `VulkanShader.hpp:64-78`. При
выходе за диапазон отдаётся ссылка на общий изменяемый объект. Сейчас в него никто не пишет, но контракт
опасный — и после переезда обращений «по номеру набора» станет заметно больше.

**F6. `GetDescriptorBufferInfo( frameIndex )` игнорирует свой аргумент.**
`ShaderResources/API/Vulkan/VulkanStorageBuffer.hpp:36-42` — параметр принимается «because every caller has
it», а индекс берётся из `CopyIndex()`. Задокументировано, но подпись вводит в заблуждение.

**F7. Столкновение привязок в двух общих заголовках.** `Common/DirectionLightsUB.glslh:10` объявляет блок на
binding **14**, `Common/TimeUB.glslh:3` — на binding **15**. В пяти PBR-шейдерах те же 14 и 15 заняты под
`u_ShadowMap2` и `u_ShadowMap3`, а `DirectionLightsUB` объявлен там же встроенно на binding **3**
(`StaticMeshPBR.shader:140`, `StaticMeshGBuffer.shader:104`, `SkinnedMeshPBR.shader:133`,
`StaticMeshPBR_Instanced.shader:124`, `StaticMeshGlass.shader:96`). Заголовки при этом **не мёртвые**: их
подключает генератор шейдер-графов (`Editor/Source/Editor/Panels/NodeGraph/ShaderGraph.cpp:389`, `:417`, `:419`).
Сгенерированный шейдер-граф, который когда-нибудь получит ещё и карты теней, столкнётся мгновенно.

**F8. Камера продублирована в восьми шейдерах вне `CameraUB`.** Они держат View/Projection/CameraPos в
собственных блоках на binding 0 и невидимы для поиска по `CameraUB`: `Programs/Terrain/Terrain.shader:31`
(`TerrainUB`), `Programs/Grass/Grass.shader:23` (`GrassUB`), `Programs/Grid/Grid.shader:9` (`GridUB`),
`Programs/Deferred/DeferredLighting.shader:70` (`DeferredUB`), `Programs/Deferred/SSAO.shader:35`,
`Programs/Deferred/SSR.shader:40`, `Programs/Deferred/GIResolve.shader:39`,
`Programs/Deferred/SSRResolve.shader:40`. Переезд `CameraUB` их не унифицирует — эти проходы останутся с
покадровым состоянием в материале, то есть **баг переживёт переделку**, если о них специально не позаботиться.

**F9. Раздельные sampler/sampled-image теряются молча.** `VulkanShaderReflection.cpp` не читает
`resources.separate_images` и `resources.separate_samplers` — шейдер с `texture2D` + `sampler` не получит ни
привязки, ни диагностики. Единственный оставшийся путь тихой потери после переписки `07c786d`.

**F10. Симуляция травы не постоянна.** `Docs/RENDERER_FRAME_STATE.md:73-76, 84-85` называет её примером
сознательно общего постоянного буфера. В коде `GrassIndirect` (`TerrainRenderer.cpp:428`) и `GrassVisible`
(`:435`) создаются с `persistent = false` по умолчанию, то есть размножены по `(кадр × слот)` — второй вид
получит свою копию. Постоянный буфер во всём движке ровно один: `ParticleState`
(`ParticleRenderer.cpp:119-120`).

**F11. Compute переиспользует набор кадра 0.** `VulkanPipelineCompute.cpp:200` — `backend->GetDescriptorSet( 0,
0 )`, то есть кадр 0 и набор 0 жёстко. Безопасно только потому, что путь немедленный и ждёт завершения
(комментарий `:194-195`), но слот при этом всё равно берётся из ambient-состояния внутри `GetDescriptorSet`.

**F12. `UpdateDescriptorSet` принимает `setIndex` и не использует его.** `VulkanPipelineCompute.cpp:367` —
параметр со значением по умолчанию `0`, в теле не встречается. Сигнатура обещает больше, чем делает.

**F13. Для 3D-образов нет getter'а моделей.** Есть `Image3DSamplers` в рефлексии и фолбэк для них в
`InitializeWithFallbacks`, но `VulkanShader` не даёт `GetUniformImage3DModels`, и `ShaderResourcesManager`
(`ShaderResourcesManager.cpp:102-142`) 3D-образы не создаёт. Соседняя территория (T3), не наша, но по той же
оси «рефлексия знает больше, чем доезжает до материала».

---

## 9. Что в старом документе устарело

`Docs/RENDERER_FRAME_STATE.md`, построчно. Каждая строка перепроверена в коде на `e34783f`.

| Строки | Утверждение | Вердикт |
|---|---|---|
| 5-13 | `MaterialPBRBase::Update*` пишут через `GetParentMaterial()`, один объект на шейдер; дескриптор ссылается, а не копирует | **Верно** (`MaterialPBRBase.cpp:27, 49, 61, 74, 102, 121, 146`) |
| 17-20 | Превью в панели свойств было удалено (`0b510b6`) | **Устарело.** Превью существует: `Editor/Source/Editor/Widgets/PreviewViewport.cpp:108` |
| 21-22 | «Multi-scene editing has the same flaw today» | **Устарело.** Исправлено вариантом B; см. `VulkanUniformBuffer.cpp:21-26` |
| 25-26 | «`SceneRenderer::BeginScene` logs a one-time warning when a second renderer draws the same frame» | **Устарело.** Такого предупреждения нет: все `LOG_WARN` в `SceneRenderer.cpp` — это `:145`, `:158`, `:163`, `:169`, `:177` (недоступные подсистемы) и `:227` (исчерпание слотов). Про «второй рендерер в кадре» — ни одного |
| 32-35 | «Reflection already handles multiple sets», `GetDescriptorSetLayout(set)` существует | **Верно и сейчас** (`VulkanShaderReflection.cpp:104`, `VulkanShader.hpp:64`). Переписка рефлексии этого не затронула |
| 36-39 | «The MATERIAL owns every set», `firstSet = 0`, ничто больше не владеет набором для графического пайплайна | **Верно** (`VulkanMaterialBackend.cpp:129-156`, `:325`). Уточнение: `VulkanPipelineCompute` владеет собственным кольцом на 64 набора (`:255-266`), но это compute |
| 40-42 | Буферы создаются из рефлексии, `UniformBuffer::Create` приватен для менеджера | **Верно** (`ShaderResourcesManager.cpp:102-142`) |
| 43 | «Sets are already per-frame-in-flight… only the OWNER dimension is missing» | **Устарело.** Измерение владельца добавлено (вариант B) |
| 51-58 | Описание вариантов A и B | **Верно** |
| 62-82 | «B, as landed» — B1…B4, аренда слотов | **Верно**, включая «слоты арендуются, а не тратятся» |
| 73-76, 84-85 | Постоянные storage-буферы = «grass simulation» | **Неверно.** См. F10: единственный постоянный буфер — `ParticleState` |
| 87-91 | Оценки памяти («~6 КБ на позу», «~640 КБ на 10k объектов», «~5 МБ») | **Не подтверждается источником.** Размеры SSBO динамические (`VulkanStorageBuffer.cpp:110-114`); статических чисел в коде нет |
| 99 | «Bound at `set = 0`; materials move to `set = 1`» | **Верно, и важнее, чем автор думал.** Это единственная нумерация, при которой затея вообще осуществима — см. 4.0. Прошлый заход счёл её ошибкой и предложил обратную; ошибся именно он |
| 102 | Список блоков к переезду: `u_Env*`, `u_BRDFLUTTexture` | Имена **неточны**: в шейдерах это `u_EnvIrradianceTex`, `u_EnvSpecularTex`, `u_BRDFLUTTexture` |
| 103-104 | «DShaderTool + VulkanShader build descriptor layouts from reflection» | **Неверно про DShaderTool.** Он раскладок не строит — это линтер парсинга на 108 строк (`Tools/DShaderTool/Source/Main.cpp`) |
| 106-107 | «Renderers bind the frame set at the start of their passes» | **Неверно.** Так нельзя — см. 4.0: раскладки пайплайна несовместимы (разные push-константные диапазоны), привязка не переживёт смену пайплайна |
| 108-110 | «Delete the ownership warning above, and re-enable the Details live preview» | **Устарело дважды:** предупреждения нет, превью уже включено |
| 112-113 | Порядок работ 1-3 → 4 | **Верно по духу**, но неполон: не учитывает хардкод `setIndex = 0` (4.1), дыру с `VK_NULL_HANDLE` (4.2), потерю номера набора в моделях ресурсов (4.10) и совместимость раскладок (4.0) |

### Поправки к заходу на `3771019`

Тот текст перепроверен целиком; он точен почти везде. Расхождения:

1. **Нумерация наборов.** Он рекомендовал «покадровое → набор 1, материал остаётся в наборе 0» как более
   дешёвую (36 шейдеров не трогаются). По правилу совместимости раскладок это **не работает**: привязка
   набора 1 требует совпадения набора 0, а набор 0 у каждого материала свой. Правильно — наоборот (4.0),
   и это дороже, а не дешевле (4.3). Это единственная содержательная ошибка, и она затрагивает вывод.
2. **«Привязать раз в начале прохода»** — принято обоими текстами без проверки. Невозможно, пока
   push-константные диапазоны различаются (4.0, 4.5).
3. **Число тестовых проектов** — 30, не 29: `Desert/Tests/Engine/SceneSkyMigration` пришёл с `4a8693c`.
4. **Места применения `FrameState`** — снимок берётся в трёх местах, а `ApplyTo` вызывается в **пяти**
   (`MeshRenderer.cpp:487`, `:556`, `:750`, `:825` плюс сам `:598` как точка снимка). Он писал «три места».
5. **Пути к буферам** — `VulkanUniformBuffer.cpp` / `VulkanStorageBuffer.cpp` лежат в
   `Desert/Desert/Source/Engine/ShaderResources/API/Vulkan/`, **не** под `Graphic/`.
6. **T6** — он писал «может начаться в любой момент». Она **идёт**: три WIP-коммита в ветке
   `worktree-agent-af898f0b361833a95` (5.2).

Всё остальное — счёт шейдеров, номера привязок, четыре `setIndex = 0`, аренда слотов, множитель памяти ×6,
список из 11 обращений к Vulkan API, состояние рефлексии, находки F1-F11 — сошлось при независимой проверке.
