# UE reference material — drop zone

What to put here, in order of usefulness to the implementation.

## 1. The material graph, as TEXT

Content Browser → gear icon → **Show Engine Content**. Search `m_SimpleVolumetricCloud`, open the one
**without** the `_Inst` suffix, click in the graph, **Ctrl+A**, **Ctrl+C**, paste into a file here.

Unreal copies the whole graph as text — nodes, links, constants, texture names. Worth more than any
number of screenshots, because it carries the actual values.

Suggested name: `m_SimpleVolumetricCloud.graph.txt`

## 2. The material instance's parameter values

Open `m_SimpleVolumetricCloud_Inst`, screenshot the parameter panel with the groups expanded.
Suggested name: `m_SimpleVolumetricCloud_Inst.png`

## 3. Facts about the volume textures — NOT the textures

Name, resolution, format, and what each channel holds. A screenshot of each asset's info panel is enough.

The asset files themselves are deliberately **not** wanted: the licence record (`../LICENCE_RECORD.md`)
excludes unmodified Epic binaries from the shipping product, and our noise is baked procedurally from a
seed anyway — it tiles exactly, costs nothing on disk and is reproducible. What we need from these
textures is their SHAPE, not their pixels.

## 4. A reference frame

An empty level with a **Volumetric Cloud**, a **Directional Light** and a **Sky Atmosphere** actor:

- screenshot of the Volumetric Cloud actor's Details panel (their defaults against ours);
- two or three viewport frames from a camera near the ground, looking at the horizon, at mid sky and at
  the zenith — the same three elevations this programme shoots, so the comparison is like for like.

## 5. Documentation extracts

Public Epic documentation quoted for engineering reference goes in `Documentation/`.
