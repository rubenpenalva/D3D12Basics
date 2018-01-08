#pragma once

// c++ includes
#include <string>

// windows includes
#include "d3d12fwd.h"
#include <d3d12.h>

// project includes
#include "d3d12basicsfwd.h"

namespace D3D12Render
{
    void CreateD3D12Texture(const char* textureFileName, const wchar_t* debugName, 
                            D3D12GpuPtr d3d12Gpu);

    size_t CreateD3D12VertexBuffer(void* bufferData, unsigned int bufferDataSize, 
                                   const wchar_t* debugName, D3D12GpuPtr d3d12Gpu);
}