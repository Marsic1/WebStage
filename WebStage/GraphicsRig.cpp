// ============================================================================
// WebStage - Graphics Rig Implementation
// ============================================================================

#include "GraphicsRig.h"
#include "Shaders.h"
#include "Ui.h"

#include <dxgi1_2.h>
#include <wincodec.h>

#include <string>

#pragma comment(lib, "windowscodecs.lib")

namespace
{

struct PsParams
{
    // NOTE: HLSL packs cbuffers into 16-byte rows and no member may
    // straddle a row: tint sits whole in row 0, mode opens row 1.
    // Mirrors the cbuffer in Shaders.h - keep both in the same order.
    float tintR = 0.0f; // mode 5 flat color (linear-ish 0..1 RGB)
    float tintG = 0.0f;
    float tintB = 0.0f;
    float opacity = 1.0f;
    int mode = 0;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
    float pad2 = 0.0f;
};
// D3D11 constant buffers require multiples of 16 bytes: fail the build,
// not the user, if anyone edits this struct again.
static_assert(sizeof(PsParams) % 16 == 0, "PsParams must stay 16-byte aligned");

struct QuadVertex
{
    float x, y;   // NDC
    float u, v;   // texture uv (v=0 top)
};

} // namespace

bool GraphicsRig::Initialize()
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    static const D3D_FEATURE_LEVEL levels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    wil::com_ptr<ID3D11Device> device;
    wil::com_ptr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        &device,
        nullptr,
        &context);
    if (FAILED(hr))
    {
        Ui::Log(L"Rig: HW D3D11CreateDevice failed 0x%08X, trying WARP", (unsigned)hr);
        // Fallback: WARP (keeps the app functional on machines without D3D11 HW)
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags & ~D3D11_CREATE_DEVICE_DEBUG,
            levels,
            ARRAYSIZE(levels),
            D3D11_SDK_VERSION,
            &device,
            nullptr,
            &context);
        if (FAILED(hr))
        {
            Ui::Log(L"Rig: WARP D3D11CreateDevice failed 0x%08X", (unsigned)hr);
            return false;
        }
        Ui::Log(L"Rig: running on WARP");
    }

    m_device = device;
    m_context = context;

    return CompileShaders();
}

void GraphicsRig::Shutdown()
{
    DestroyPreviewSwapchain();
    m_overlaySrv.reset();
    m_overlayTex.reset();
    m_overlayW = m_overlayH = 0;
    m_sceneSrv.reset();
    m_sceneRtv.reset();
    m_scene.reset();
    m_samplerLinear.reset();
    m_samplerPoint.reset();
    m_psConstants.reset();
    m_ps.reset();
    m_vsQuad.reset();
    m_quadLayout.reset();
    m_quadVb.reset();
    m_blendAlpha.reset();
    m_rsNoCull.reset();
    m_vs.reset();
    m_context.reset();
    m_device.reset();
}

static void LogShaderFail(const wchar_t* step, HRESULT hr, ID3DBlob* errors)
{
    if (errors && errors->GetBufferSize() > 0)
    {
        // Blob is ANSI text from the compiler; clamp the length for the log.
        size_t n = errors->GetBufferSize();
        if (n > 900)
            n = 900;
        std::string msg((const char*)errors->GetBufferPointer(), n);
        Ui::Log(L"Rig: %s failed 0x%08X: %hs", step, (unsigned)hr, msg.c_str());
    }
    else
    {
        Ui::Log(L"Rig: %s failed 0x%08X", step, (unsigned)hr);
    }
}

bool GraphicsRig::CompileShaders()
{
    // Shaders come precompiled from Shaders.h (fxc offline): no runtime
    // D3DCompile, no d3dcompiler_47.dll dependency, faster startup.
    HRESULT hr = m_device->CreateVertexShader(kVsBytecode,
        kVsBytecodeSize, nullptr, &m_vs);
    if (FAILED(hr))
    {
        LogShaderFail(L"vs create", hr, nullptr);
        return false;
    }

    hr = m_device->CreatePixelShader(kPsBytecode,
        kPsBytecodeSize, nullptr, &m_ps);
    if (FAILED(hr))
    {
        LogShaderFail(L"ps create", hr, nullptr);
        return false;
    }

    hr = m_device->CreateVertexShader(kVsQuadBytecode,
        kVsQuadBytecodeSize, nullptr, &m_vsQuad);
    if (FAILED(hr))
    {
        LogShaderFail(L"vsquad create", hr, nullptr);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout),
        kVsQuadBytecode, kVsQuadBytecodeSize, &m_quadLayout);
    if (FAILED(hr))
    {
        LogShaderFail(L"input layout", hr, nullptr);
        return false;
    }

    D3D11_BUFFER_DESC vb = {};
    vb.ByteWidth = sizeof(QuadVertex) * 4;
    vb.Usage = D3D11_USAGE_DYNAMIC;
    vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = m_device->CreateBuffer(&vb, nullptr, &m_quadVb);
    if (FAILED(hr))
    {
        LogShaderFail(L"quad vb", hr, nullptr);
        return false;
    }

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = m_device->CreateBlendState(&bd, &m_blendAlpha);
    if (FAILED(hr))
    {
        LogShaderFail(L"blend state", hr, nullptr);
        return false;
    }

    // The preview uses a fullscreen triangle whose winding is CCW, while
    // D3D11's default rasterizer treats clockwise as front (so the
    // triangle was backface-culled and the preview stayed black).
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    hr = m_device->CreateRasterizerState(&rd, &m_rsNoCull);
    if (FAILED(hr))
    {
        LogShaderFail(L"rasterizer state", hr, nullptr);
        return false;
    }

    D3D11_BUFFER_DESC cb = {};
    cb.ByteWidth = sizeof(PsParams);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = m_device->CreateBuffer(&cb, nullptr, &m_psConstants);
    if (FAILED(hr))
    {
        LogShaderFail(L"ps constants", hr, nullptr);
        return false;
    }

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = m_device->CreateSamplerState(&sd, &m_samplerPoint);
    if (FAILED(hr))
    {
        LogShaderFail(L"point sampler", hr, nullptr);
        return false;
    }

    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    hr = m_device->CreateSamplerState(&sd, &m_samplerLinear);
    if (FAILED(hr))
    {
        LogShaderFail(L"linear sampler", hr, nullptr);
        return false;
    }

    return true;
}

bool GraphicsRig::CreateSceneTexture(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)width;
    td.Height = (UINT)height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    wil::com_ptr<ID3D11Texture2D> tex;
    if (FAILED(m_device->CreateTexture2D(&td, nullptr, &tex)))
        return false;

    wil::com_ptr<ID3D11RenderTargetView> rtv;
    if (FAILED(m_device->CreateRenderTargetView(tex.get(), nullptr, &rtv)))
        return false;

    wil::com_ptr<ID3D11ShaderResourceView> srv;
    if (FAILED(m_device->CreateShaderResourceView(tex.get(), nullptr, &srv)))
        return false;

    m_scene = tex;
    m_sceneRtv = rtv;
    m_sceneSrv = srv;
    m_sceneW = width;
    m_sceneH = height;

    ClearScene();
    return true;
}

void GraphicsRig::ClearScene()
{
    if (!m_sceneRtv)
        return;
#ifdef WS_RED_SCENE_TEST
    const float clear[4] = { 1, 0, 0, 1 }; // diagnostics: opaque red
#else
    const float clear[4] = { 0, 0, 0, 0 };
#endif
    m_context->ClearRenderTargetView(m_sceneRtv.get(), clear);
}

bool GraphicsRig::SaveScenePng(const wchar_t* path)
{
    if (!m_scene || !path)
        return false;

    D3D11_TEXTURE2D_DESC desc = {};
    m_scene->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC sd = desc;
    sd.BindFlags = 0;
    sd.MiscFlags = 0;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    wil::com_ptr<ID3D11Texture2D> staging;
    if (FAILED(m_device->CreateTexture2D(&sd, nullptr, &staging)))
        return false;
    m_context->CopyResource(staging.get(), m_scene.get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    bool ok = false;
    wil::com_ptr<IWICImagingFactory> wic;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
    {
        wil::com_ptr<IWICStream> stream;
        if (SUCCEEDED(wic->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)))
        {
            wil::com_ptr<IWICBitmapEncoder> enc;
            if (SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
                SUCCEEDED(enc->Initialize(stream.get(), WICBitmapEncoderNoCache)))
            {
                wil::com_ptr<IWICBitmapFrameEncode> frame;
                wil::com_ptr<IPropertyBag2> bag;
                if (SUCCEEDED(enc->CreateNewFrame(&frame, &bag)) &&
                    SUCCEEDED(frame->Initialize(bag.get())) &&
                    SUCCEEDED(frame->SetSize(desc.Width, desc.Height)))
                {
                    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
                    if (SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
                        IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA) &&
                        SUCCEEDED(frame->WritePixels(desc.Height, mapped.RowPitch,
                            mapped.RowPitch * desc.Height, (BYTE*)mapped.pData)) &&
                        SUCCEEDED(frame->Commit()) &&
                        SUCCEEDED(enc->Commit()))
                    {
                        ok = true;
                    }
                }
            }
        }
    }

    m_context->Unmap(staging.get(), 0);
    return ok;
}

bool GraphicsRig::CreatePreviewSwapchain(HWND hwnd, int width, int height)
{
    DestroyPreviewSwapchain();
    // The preview window exists (and gets WM_SIZE) before the D3D device
    // is created during startup: refuse to dereference a null device.
    if (!m_device || !hwnd || width <= 0 || height <= 0)
        return false;

    wil::com_ptr<IDXGIDevice> dxgiDevice;
    if (FAILED(m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
        return false;

    wil::com_ptr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter)))
        return false;

    wil::com_ptr<IDXGIFactory> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
        return false;

    // SEQUENTIAL (not DISCARD): the backbuffer never rotates, so the RTV
    // created here and the buffer returned by GetBuffer(0) for the GDI
    // overlay stay the same buffer. (With DISCARD the RTV goes stale after
    // the first Present and the overlay lands on a stale buffer = ghosts.)
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = (UINT)width;
    sd.BufferDesc.Height = (UINT)height;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 1;
    sd.OutputWindow = hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;

    wil::com_ptr<IDXGISwapChain> swapchain;
    if (FAILED(factory->CreateSwapChain(m_device.get(), &sd, &swapchain)))
        return false;

    wil::com_ptr<ID3D11Texture2D> backbuffer;
    if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))))
        return false;

    wil::com_ptr<ID3D11RenderTargetView> rtv;
    if (FAILED(m_device->CreateRenderTargetView(backbuffer.get(), nullptr, &rtv)))
        return false;

    m_swapchain = swapchain;
    m_previewRtv = rtv;
    m_previewHwnd = hwnd;
    return true;
}

void GraphicsRig::DestroyPreviewSwapchain()
{
    m_previewRtv.reset();
    m_swapchain.reset();
    m_previewHwnd = nullptr;
    m_occluded = false;
}

void GraphicsRig::ResizePreview(int width, int height)
{
    if (!m_swapchain || width <= 0 || height <= 0)
        return;

    m_previewRtv.reset();
    HRESULT hr = m_swapchain->ResizeBuffers(0, (UINT)width, (UINT)height,
        DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
        Ui::Log(L"Preview ResizeBuffers failed 0x%08X (%dx%d)", (unsigned)hr, width, height);
    // Re-acquire regardless: on failure the old-size buffers still exist.

    wil::com_ptr<ID3D11Texture2D> backbuffer;
    if (FAILED(m_swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))))
    {
        Ui::Log(L"Preview GetBuffer failed after resize");
        return;
    }

    wil::com_ptr<ID3D11RenderTargetView> rtv;
    if (FAILED(m_device->CreateRenderTargetView(backbuffer.get(), nullptr, &rtv)))
    {
        Ui::Log(L"Preview RTV recreate failed after resize");
        return;
    }

    m_previewRtv = rtv;
}

void GraphicsRig::BeginComposite()
{
    if (!m_sceneRtv)
        return;

    const float clear[4] = { 0, 0, 0, 0 };
    m_context->ClearRenderTargetView(m_sceneRtv.get(), clear);

    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)m_sceneW;
    vp.Height = (FLOAT)m_sceneH;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
    m_context->OMSetRenderTargets(1, m_sceneRtv.addressof(), nullptr);

    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blendAlpha.get(), blendFactor, 0xFFFFFFFF);

    m_context->IASetInputLayout(m_quadLayout.get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_context->VSSetShader(m_vsQuad.get(), nullptr, 0);
    m_context->PSSetShader(m_ps.get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_psConstants.addressof());
    m_context->PSSetSamplers(0, 1, m_samplerLinear.addressof());
}

void GraphicsRig::DrawSourceQuad(ID3D11Texture2D* src,
    ID3D11ShaderResourceView* srcSrvOrNull,
    int dstX, int dstY, int dstW, int dstH, float opacity)
{
    if (!src || !m_sceneRtv || dstW <= 0 || dstH <= 0)
        return;

    // WebSources cache one SRV per texture (rebuilt on realloc) so the
    // per-frame composite - and preview drags at mousemove rates - never
    // pay for view creation. Fall back to a throwaway view when absent.
    wil::com_ptr<ID3D11ShaderResourceView> tempSrv;
    ID3D11ShaderResourceView* srv = srcSrvOrNull;
    if (!srv)
    {
        D3D11_TEXTURE2D_DESC td = {};
        src->GetDesc(&td);

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.ViewDimension = (td.ArraySize > 1)
            ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
        if (sd.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY)
        {
            sd.Texture2DArray.MipLevels = 1;
            sd.Texture2DArray.ArraySize = 1;
        }
        else
        {
            sd.Texture2D.MipLevels = 1;
        }
        if (FAILED(m_device->CreateShaderResourceView(src, &sd, &tempSrv)))
            return;
        srv = tempSrv.get();
    }

    // Scene pixels -> NDC (y-up), uv v=0 at top
    float x0 = (dstX / (float)m_sceneW) * 2.0f - 1.0f;
    float x1 = ((dstX + dstW) / (float)m_sceneW) * 2.0f - 1.0f;
    float y0 = 1.0f - ((dstY + dstH) / (float)m_sceneH) * 2.0f;
    float y1 = 1.0f - (dstY / (float)m_sceneH) * 2.0f;

    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(m_context->Map(m_quadVb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
        return;
    auto v = static_cast<QuadVertex*>(map.pData);
    v[0] = { x0, y0, 0.0f, 1.0f }; // BL
    v[1] = { x0, y1, 0.0f, 0.0f }; // TL
    v[2] = { x1, y0, 1.0f, 1.0f }; // BR
    v[3] = { x1, y1, 1.0f, 0.0f }; // TR
    m_context->Unmap(m_quadVb.get(), 0);

    PsParams p;
    p.mode = 4;
    p.opacity = opacity;
    m_context->UpdateSubresource(m_psConstants.get(), 0, nullptr, &p, 0, 0);

    UINT stride = sizeof(QuadVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_quadVb.addressof(), &stride, &offset);
    m_context->PSSetShaderResources(0, 1, &srv);
    m_context->Draw(4, 0);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_context->PSSetShaderResources(0, 1, &nullSrv);
}

void GraphicsRig::EndComposite()
{
    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
}

void GraphicsRig::DrawPreviewScene(int dstX, int dstY, int dstW, int dstH)
{
    if (!m_swapchain || !m_sceneSrv)
        return;

    // Self-heal: if the RTV went missing (e.g. a failed resize left it
    // null), re-acquire it from the live backbuffer instead of staying
    // black until the next successful resize.
    if (!m_previewRtv)
    {
        wil::com_ptr<ID3D11Texture2D> bb;
        wil::com_ptr<ID3D11RenderTargetView> rtv;
        HRESULT hrGb = m_swapchain->GetBuffer(0, IID_PPV_ARGS(&bb));
        if (FAILED(hrGb))
            Ui::Log(L"Preview heal GetBuffer failed 0x%08X", (unsigned)hrGb);
        else if (FAILED(m_device->CreateRenderTargetView(bb.get(), nullptr, &rtv)))
            Ui::Log(L"Preview heal RTV recreate failed");
        else
            m_previewRtv = rtv;
        if (!m_previewRtv)
            return;
    }

    wil::com_ptr<ID3D11Texture2D> backbuffer;
    if (FAILED(m_swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))))
        return;

    D3D11_TEXTURE2D_DESC bd = {};
    backbuffer->GetDesc(&bd);

    // Letterbox bars first (stale pixels outside the image rect).
    // Cleared to the app background (not black) so the preview melts
    // into the surrounding UI.
    const COLORREF bgc = Ui::Bg();
    const float bg[4] = { GetRValue(bgc) / 255.0f, GetGValue(bgc) / 255.0f,
        GetBValue(bgc) / 255.0f, 1.0f };
    m_context->ClearRenderTargetView(m_previewRtv.get(), bg);

    if (dstW <= 0 || dstH <= 0)
        return;

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = (FLOAT)dstX;
    vp.TopLeftY = (FLOAT)dstY;
    vp.Width = (FLOAT)dstW;
    vp.Height = (FLOAT)dstH;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
    m_context->RSSetState(m_rsNoCull.get());
    m_context->OMSetRenderTargets(1, m_previewRtv.addressof(), nullptr);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vs.get(), nullptr, 0);

    PsParams p;
    p.mode = 3; // scene over checkerboard
    m_context->UpdateSubresource(m_psConstants.get(), 0, nullptr, &p, 0, 0);

    m_context->PSSetShader(m_ps.get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_psConstants.addressof());
    m_context->PSSetShaderResources(0, 1, m_sceneSrv.addressof());
    m_context->PSSetSamplers(0, 1, m_samplerLinear.addressof());
    m_context->Draw(3, 0);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_context->PSSetShaderResources(0, 1, &nullSrv);
}

bool GraphicsRig::RecreatePreviewSwapchain(HWND hwnd)
{
    if (!hwnd)
        return false;
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return false;
    DestroyPreviewSwapchain();
    return CreatePreviewSwapchain(hwnd, w, h);
}

void GraphicsRig::PresentPreview()
{
    if (!m_swapchain)
        return;
    // Interval 0 (no vsync wait): a vsync-blocking Present can stall the UI
    // thread for seconds when the swapchain is occluded or the display
    // path is disrupted (maximize/restore transitions), starving every
    // other paint and input. Tearing is irrelevant for an editor preview.
    HRESULT hr = m_swapchain->Present(0, 0);
    if (hr == DXGI_STATUS_OCCLUDED)
        m_occluded = true;
    else if (hr == S_OK)
        m_occluded = false;
    // Other statuses (e.g. MODE_CHANGE) resolve on the next present.
}

bool GraphicsRig::UploadOverlay(const void* bgra, int width, int height, int strideBytes)
{
    if (!bgra || width <= 0 || height <= 0 || strideBytes < width * 4)
        return false;

    if (!m_overlayTex || m_overlayW != width || m_overlayH != height)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = (UINT)width;
        td.Height = (UINT)height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        wil::com_ptr<ID3D11Texture2D> tex;
        wil::com_ptr<ID3D11ShaderResourceView> srv;
        if (FAILED(m_device->CreateTexture2D(&td, nullptr, &tex)))
            return false;
        if (FAILED(m_device->CreateShaderResourceView(tex.get(), nullptr, &srv)))
            return false;
        m_overlayTex = tex;
        m_overlaySrv = srv;
        m_overlayW = width;
        m_overlayH = height;
    }

    // Magenta key -> transparent; everything else opaque (the GDI paint
    // leaves alpha untouched, so resolve it here on the way in).
    // NOTE: in-place edit of the caller's DIB (already a scratch copy).
    auto px = static_cast<uint32_t*>(const_cast<void*>(bgra));
    size_t stridePx = (size_t)strideBytes / 4;
    for (int y = 0; y < height; y++)
    {
        uint32_t* row = px + (size_t)y * stridePx;
        for (int x = 0; x < width; x++)
        {
            uint32_t v = row[x];
            row[x] = ((v & 0x00FFFFFFu) == 0x00FF00FFu)
                ? 0x00000000u : (v | 0xFF000000u);
        }
    }

    m_context->UpdateSubresource(m_overlayTex.get(), 0, nullptr,
        bgra, (UINT)strideBytes, 0);
    return true;
}

void GraphicsRig::DrawOverlayQuad(int clientW, int clientH)
{    if (!m_overlayTex || !m_overlaySrv || !m_previewRtv || clientW <= 0 || clientH <= 0)
        return;

    // Full-client NDC quad, uv v=0 at top
    float x0 = -1.0f, x1 = 1.0f, y0 = -1.0f, y1 = 1.0f;

    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(m_context->Map(m_quadVb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
        return;
    auto v = static_cast<QuadVertex*>(map.pData);
    v[0] = { x0, y0, 0.0f, 1.0f }; // BL
    v[1] = { x0, y1, 0.0f, 0.0f }; // TL
    v[2] = { x1, y0, 1.0f, 1.0f }; // BR
    v[3] = { x1, y1, 1.0f, 0.0f }; // TR
    m_context->Unmap(m_quadVb.get(), 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)clientW;
    vp.Height = (FLOAT)clientH;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
    m_context->RSSetState(m_rsNoCull.get());
    m_context->OMSetRenderTargets(1, m_previewRtv.addressof(), nullptr);

    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blendAlpha.get(), blendFactor, 0xFFFFFFFF);

    PsParams p;
    p.mode = 4;
    p.opacity = 1.0f;
    m_context->UpdateSubresource(m_psConstants.get(), 0, nullptr, &p, 0, 0);

    m_context->IASetInputLayout(m_quadLayout.get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    UINT stride = sizeof(QuadVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_quadVb.addressof(), &stride, &offset);
    m_context->VSSetShader(m_vsQuad.get(), nullptr, 0);
    m_context->PSSetShader(m_ps.get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_psConstants.addressof());
    m_context->PSSetSamplers(0, 1, m_samplerLinear.addressof());
    m_context->PSSetShaderResources(0, 1, m_overlaySrv.addressof());
    m_context->Draw(4, 0);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_context->PSSetShaderResources(0, 1, &nullSrv);
    m_context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
}

void GraphicsRig::DrawTintQuad(int x, int y, int w, int h,
    int clientW, int clientH, COLORREF color, float alpha,
    ID3D11ShaderResourceView* sceneSrv, int ox, int oy, int dw, int dh)
{
    if (!m_previewRtv || !sceneSrv || w <= 0 || h <= 0
        || clientW <= 0 || clientH <= 0 || dw <= 0 || dh <= 0)
        return;
    if (alpha <= 0.0f)
        return;
    if (alpha > 1.0f)
        alpha = 1.0f;

    // Client pixels -> NDC (y-up); UVs into scene space (v=0 at top, same
    // convention as the source quads) so mode 6 can mask by scene alpha.
    float x0 = (x / (float)clientW) * 2.0f - 1.0f;
    float x1 = ((x + w) / (float)clientW) * 2.0f - 1.0f;
    float y0 = 1.0f - ((y + h) / (float)clientH) * 2.0f;
    float y1 = 1.0f - (y / (float)clientH) * 2.0f;
    float u0 = (x - ox) / (float)dw;
    float u1 = (x + w - ox) / (float)dw;
    float v0 = (y + h - oy) / (float)dh; // BL row (v=0 at top)
    float v1 = (y - oy) / (float)dh;     // TL row

    D3D11_MAPPED_SUBRESOURCE map = {};
    if (FAILED(m_context->Map(m_quadVb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
        return;
    auto v = static_cast<QuadVertex*>(map.pData);
    v[0] = { x0, y0, u0, v0 }; // BL
    v[1] = { x0, y1, u0, v1 }; // TL
    v[2] = { x1, y0, u1, v0 }; // BR
    v[3] = { x1, y1, u1, v1 }; // TR
    m_context->Unmap(m_quadVb.get(), 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (FLOAT)clientW;
    vp.Height = (FLOAT)clientH;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
    m_context->RSSetState(m_rsNoCull.get());
    m_context->OMSetRenderTargets(1, m_previewRtv.addressof(), nullptr);

    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blendAlpha.get(), blendFactor, 0xFFFFFFFF);

    PsParams p;
    p.mode = 6;
    p.opacity = alpha;
    p.tintR = GetRValue(color) / 255.0f;
    p.tintG = GetGValue(color) / 255.0f;
    p.tintB = GetBValue(color) / 255.0f;
    m_context->UpdateSubresource(m_psConstants.get(), 0, nullptr, &p, 0, 0);

    m_context->IASetInputLayout(m_quadLayout.get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    UINT stride = sizeof(QuadVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_quadVb.addressof(), &stride, &offset);
    m_context->VSSetShader(m_vsQuad.get(), nullptr, 0);
    m_context->PSSetShader(m_ps.get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_psConstants.addressof());
    m_context->PSSetSamplers(0, 1, m_samplerLinear.addressof());
    m_context->PSSetShaderResources(0, 1, &sceneSrv);
    m_context->Draw(4, 0);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_context->PSSetShaderResources(0, 1, &nullSrv);
    m_context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
}

