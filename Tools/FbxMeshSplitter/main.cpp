#include "FbxMeshSplitter.hpp"

#include <cstdlib>
#include <iostream>

// Usage: FbxMeshSplitter <path/to/file.fbx> [scale]
//   scale - optional uniform multiplier on the output geometry (default 1.0), e.g. 0.01 if a cm-authored pack
//           imports 100x too large. Splits every mesh into individual .obj files under <Resources>/Mesh/
//           <fbx-stem>/ and writes a collection.json next to the FBX. Exit 0 ok, 1 fail, 2 bad usage.
int main( int argc, char** argv )
{
    if ( argc < 2 )
    {
        std::cerr << "Usage: FbxMeshSplitter <file.fbx> [scale]\n";
        return 2;
    }

    const float scale  = ( argc >= 3 ) ? static_cast<float>( std::atof( argv[2] ) ) : 1.0f;
    const auto  result = FbxSplit::SplitFbxIntoMeshes( argv[1], scale > 0.0f ? scale : 1.0f );
    if ( !result.Success )
    {
        std::cerr << "[FbxMeshSplitter] FAILED: " << result.Error << '\n';
        return 1;
    }

    std::cout << "[FbxMeshSplitter] OK: " << result.MeshCount << " meshes written; manifest: "
              << result.ManifestPath << '\n';
    return 0;
}
