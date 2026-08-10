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
* Сломано ровно одно звено: **запись дескрипторов жёстко прибита к набору 0** — четыре строки
  `const uint32_t setIndex = 0; // Simplified`.
* Аренда слотов множит память **в 6 раз** на каждый uniform-буфер, каждый непостоянный storage-буфер и каждый
  дескрипторный набор каждого материала.
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
