#include "d3d12resources.h"

// C includes
#include <cassert>

// Third party libraries
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

// Project includes
#include "d3d12gpus.h"

using namespace D3D12Render;

void D3D12Render::CreateD3D12Texture(const char* textureFileName, const wchar_t* debugName,
                         D3D12GpuPtr d3d12Gpu)
{
    assert(d3d12Gpu);
    ID3D12DevicePtr d3d12Device = d3d12Gpu->GetDevice();
    assert(d3d12Device);
    ID3D12DescriptorHeapPtr descriptorHeap = d3d12Gpu->GetSRVDescriptorHeap();
    assert(descriptorHeap);

    // NOTE: move this to a struct so it can be const when loading the data
    int textureWidth;
    int textureHeight;
    int textureChannelsCount;
    unsigned char* textureData = stbi_load(textureFileName, &textureWidth, &textureHeight, 
                                            &textureChannelsCount, 0);
    assert(textureData);

    D3D12GpuUploadTexture2DTask textureUploadTask;
    textureUploadTask.m_data = textureData;
    textureUploadTask.m_dataSize = textureWidth * textureHeight * textureChannelsCount * sizeof(unsigned char);
    textureUploadTask.m_width = textureWidth;
    textureUploadTask.m_height = textureHeight;
    textureUploadTask.m_debugName = debugName;

    textureUploadTask.m_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureUploadTask.m_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    textureUploadTask.m_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    textureUploadTask.m_desc.Texture2D.MostDetailedMip = 0;
    textureUploadTask.m_desc.Texture2D.MipLevels = 1;
    textureUploadTask.m_desc.Texture2D.PlaneSlice = 0;
    textureUploadTask.m_desc.Texture2D.ResourceMinLODClamp = 0.0f;

    d3d12Gpu->AddUploadTexture2DTask(textureUploadTask);
}

size_t D3D12Render::CreateD3D12Buffer(void* bufferData, unsigned int bufferDataSize, const wchar_t* debugName, D3D12GpuPtr d3d12Gpu)
{
    D3D12GpuUploadBufferTask bufferUploadTask;
    bufferUploadTask.m_bufferData = bufferData;
    bufferUploadTask.m_bufferDataSize = bufferDataSize;
    bufferUploadTask.m_bufferName = debugName;

    return d3d12Gpu->AddUploadBufferTask(bufferUploadTask);
}