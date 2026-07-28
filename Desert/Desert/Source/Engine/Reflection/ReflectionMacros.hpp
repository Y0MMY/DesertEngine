#pragma once

// Annotation macros consumed by DesertHeaderTool (the codegen step). They intentionally expand to
// NOTHING during normal C++ compilation — the tool parses them straight from source and emits the
// registration code into Source/Engine/Generated/Reflection.gen.cpp.
//
// Usage:
//   struct PBRSurfaceParams
//   {
//       REFLECT()
//
//       PROPERTY( DisplayName( "Albedo" ), Category( "Surface" ), Color )
//       glm::vec4 AlbedoColor;
//
//       PROPERTY( DisplayName( "Metallic" ), Category( "Surface" ), Range( 0.0f, 1.0f ) )
//       float MetallicFactor;
//   };
//
// Supported attribute tokens (parsed by the tool, never compiled):
//   DisplayName("..."), Category("..."), Range(min,max), Color,
//   Asset<TypeName>, Thumbnail, ReadOnly, Hidden
//
// REFLECT() marks a struct/class for reflection. PROPERTY(...) marks the field that follows it.
#define REFLECT()
#define PROPERTY( ... )
