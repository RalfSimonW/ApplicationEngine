#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include "imgui.h"
#include "imgui_internal.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include "Diagnostics\ResizeDiag.h"

using Microsoft::WRL::ComPtr;

#ifndef RESIZE_DIAG_ENABLED
#define RESIZE_DIAG_ENABLED 1
#endif

#ifndef SINGLE_THREAD_RENDER_TEST
#define SINGLE_THREAD_RENDER_TEST 0
#endif

// -----------------------------------------------------------------------------
// Shared state
// -----------------------------------------------------------------------------

struct SceneSnapshot
{
	float angle = 0.0f;
};

struct InputEvent
{
	UINT message = 0;
	WPARAM wParam = 0;
	LPARAM lParam = 0;
	bool ctrl = false;
	bool shift = false;
	bool alt = false;
};

struct D3DState
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<IDXGISwapChain> swapChain;
	ComPtr<ID3D11RenderTargetView> backBufferRTV;

	ComPtr<ID3D11Texture2D> sceneTexture;
	ComPtr<ID3D11RenderTargetView> sceneRTV;
	ComPtr<ID3D11ShaderResourceView> sceneSRV;

	ComPtr<ID3D11VertexShader> sceneVS;
	ComPtr<ID3D11PixelShader> scenePS;
	ComPtr<ID3D11Buffer> sceneConstantBuffer;
	ComPtr<ID3D11RasterizerState> sceneRasterizerState;

	UINT backBufferWidth = 0;
	UINT backBufferHeight = 0;
	UINT sceneWidth = 0;
	UINT sceneHeight = 0;
};

static HWND g_hwnd = nullptr;
static std::atomic<bool> g_running{ true };

static std::atomic<std::shared_ptr<const SceneSnapshot>> g_snapshot{
	std::make_shared<const SceneSnapshot>()
};

static std::atomic<uint64_t> g_mainResizeRequest{ 0 };
static std::atomic<uint64_t> g_sceneResizeRequest{ 0 };

static std::atomic<bool> g_interactiveResize{ false };
static std::atomic<bool> g_forceMainResize{ false };

static std::mutex g_inputMutex;
static std::deque<InputEvent> g_inputQueue;

static D3DState g_d3d;

// Pack width and height into one atomic 64-bit value.
static uint64_t PackSize(UINT width, UINT height)
{
	return (static_cast<uint64_t>(width) << 32) |
		static_cast<uint64_t>(height);
}

static void UnpackSize(uint64_t packed, UINT& width, UINT& height)
{
	width = static_cast<UINT>(packed >> 32);
	height = static_cast<UINT>(packed & 0xFFFFFFFFu);
}

static float DpiScale(HWND hwnd)
{
	const UINT dpi = GetDpiForWindow(hwnd);
	return static_cast<float>(dpi ? dpi : 96) / 96.0f;
}

static void QueueInput(UINT message, WPARAM wParam, LPARAM lParam)
{
	InputEvent event{};
	event.message = message;
	event.wParam = wParam;
	event.lParam = lParam;
	event.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

	std::lock_guard lock(g_inputMutex);
	g_inputQueue.push_back(event);
}

// -----------------------------------------------------------------------------
// Logging and D3D helpers
// -----------------------------------------------------------------------------

static void Log(const char* text)
{
	OutputDebugStringA(text);
	OutputDebugStringA("\n");
}

static void LogHr(const char* operation, HRESULT hr)
{
	char text[256]{};
	std::snprintf(
		text, sizeof(text),
		"%s failed (HRESULT=0x%08lX)",
		operation, static_cast<unsigned long>(hr));
	Log(text);
}

static bool CreateBackBufferRTV()
{
	ComPtr<ID3D11Texture2D> backBuffer;

	HRESULT hr = g_d3d.swapChain->GetBuffer(
		0, IID_PPV_ARGS(backBuffer.GetAddressOf()));

	if (FAILED(hr))
	{
		LogHr("GetBuffer(back buffer)", hr);
		return false;
	}

	D3D11_TEXTURE2D_DESC desc{};
	backBuffer->GetDesc(&desc);

#if RESIZE_DIAG_ENABLED
	const auto total = ++ResizeDiag::rtvCreateCount;
	const auto number = ++ResizeDiag::mainRtvCreateCount;

	ResizeDiag::Log(
		"MainSwapChainRTV #%llu total=%llu begin size=%ux%u resource=%p",
		number, total, desc.Width, desc.Height,
		static_cast<void*>(backBuffer.Get()));
#endif

	ComPtr<ID3D11RenderTargetView> newRTV;
	hr = g_d3d.device->CreateRenderTargetView(
		backBuffer.Get(), nullptr, newRTV.GetAddressOf());

#if RESIZE_DIAG_ENABLED
	ResizeDiag::Log(
		"MainSwapChainRTV #%llu end hr=0x%08X",
		number, static_cast<unsigned int>(hr));
#endif

	if (FAILED(hr))
	{
		LogHr("CreateRenderTargetView(back buffer)", hr);
		return false;
	}

	g_d3d.backBufferRTV = std::move(newRTV);
	g_d3d.backBufferWidth = desc.Width;
	g_d3d.backBufferHeight = desc.Height;
	return true;
}

// -----------------------------------------------------------------------------
// D3D11 device and main swap chain
// -----------------------------------------------------------------------------

static bool CreateD3D(HWND hwnd)
{
	RECT rc{};
	GetClientRect(hwnd, &rc);

	const UINT width = std::max<UINT>(
		1, static_cast<UINT>(rc.right - rc.left));
	const UINT height = std::max<UINT>(
		1, static_cast<UINT>(rc.bottom - rc.top));

	DXGI_SWAP_CHAIN_DESC swapDesc{};
	swapDesc.BufferDesc.Width = width;
	swapDesc.BufferDesc.Height = height;
	swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapDesc.SampleDesc.Count = 1;
	swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapDesc.BufferCount = 2;
	swapDesc.OutputWindow = hwnd;
	swapDesc.Windowed = TRUE;
	swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	const D3D_FEATURE_LEVEL requestedLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	D3D_FEATURE_LEVEL featureLevel{};
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		flags,
		requestedLevels,
		static_cast<UINT>(std::size(requestedLevels)),
		D3D11_SDK_VERSION,
		&swapDesc,
		g_d3d.swapChain.GetAddressOf(),
		g_d3d.device.GetAddressOf(),
		&featureLevel,
		g_d3d.context.GetAddressOf());

	// Some older runtimes reject the 11_1 feature-level value.
	if (hr == E_INVALIDARG)
	{
		const D3D_FEATURE_LEVEL fallbackLevels[] =
		{
			D3D_FEATURE_LEVEL_11_0
		};

		hr = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			flags,
			fallbackLevels,
			1,
			D3D11_SDK_VERSION,
			&swapDesc,
			g_d3d.swapChain.GetAddressOf(),
			g_d3d.device.GetAddressOf(),
			&featureLevel,
			g_d3d.context.GetAddressOf());
	}

	if (FAILED(hr))
	{
		LogHr("D3D11CreateDeviceAndSwapChain", hr);
		return false;
	}

	return CreateBackBufferRTV();
}

// -----------------------------------------------------------------------------
// Scene shaders and pipeline
// -----------------------------------------------------------------------------

static bool CompileShader(
	const char* source,
	const char* entryPoint,
	const char* target,
	ComPtr<ID3DBlob>& bytecode)
{
	ComPtr<ID3DBlob> errors;

	const HRESULT hr = D3DCompile(
		source,
		std::strlen(source),
		nullptr,
		nullptr,
		nullptr,
		entryPoint,
		target,
		D3DCOMPILE_ENABLE_STRICTNESS,
		0,
		bytecode.GetAddressOf(),
		errors.GetAddressOf());

	if (FAILED(hr))
	{
		if (errors)
			OutputDebugStringA(
				static_cast<const char*>(errors->GetBufferPointer()));

		LogHr("D3DCompile", hr);
		return false;
	}

	return true;
}

static bool CreateScenePipeline()
{
	static constexpr char shaderSource[] = R"(
        cbuffer SceneConstants : register(b0)
        {
            float angle;
            float3 padding;
        };

        struct VSOutput
        {
            float4 position : SV_Position;
            float4 color    : COLOR0;
        };

        VSOutput VSMain(uint vertexId : SV_VertexID)
        {
            const float2 positions[3] =
            {
                float2( 0.0,  0.65),
                float2( 0.65, -0.65),
                float2(-0.65, -0.65)
            };

            const float4 colors[3] =
            {
                float4(1.0, 0.25, 0.20, 1.0),
                float4(0.20, 1.0, 0.30, 1.0),
                float4(0.20, 0.45, 1.0, 1.0)
            };

            float s = sin(angle);
            float c = cos(angle);

            float2 p = positions[vertexId];
            float2 rotated = float2(
                p.x * c - p.y * s,
                p.x * s + p.y * c);

            VSOutput output;
            output.position = float4(rotated, 0.0, 1.0);
            output.color = colors[vertexId];
            return output;
        }

        float4 PSMain(VSOutput input) : SV_Target
        {
            return input.color;
        }
    )";

	ComPtr<ID3DBlob> vsBytecode;
	ComPtr<ID3DBlob> psBytecode;

	if (!CompileShader(shaderSource, "VSMain", "vs_5_0", vsBytecode))
		return false;

	if (!CompileShader(shaderSource, "PSMain", "ps_5_0", psBytecode))
		return false;

	HRESULT hr = g_d3d.device->CreateVertexShader(
		vsBytecode->GetBufferPointer(),
		vsBytecode->GetBufferSize(),
		nullptr,
		g_d3d.sceneVS.GetAddressOf());

	if (FAILED(hr))
	{
		LogHr("CreateVertexShader", hr);
		return false;
	}

	hr = g_d3d.device->CreatePixelShader(
		psBytecode->GetBufferPointer(),
		psBytecode->GetBufferSize(),
		nullptr,
		g_d3d.scenePS.GetAddressOf());

	if (FAILED(hr))
	{
		LogHr("CreatePixelShader", hr);
		return false;
	}

	D3D11_BUFFER_DESC bufferDesc{};
	bufferDesc.ByteWidth = 16;
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = g_d3d.device->CreateBuffer(
		&bufferDesc,
		nullptr,
		g_d3d.sceneConstantBuffer.GetAddressOf());

	if (FAILED(hr))
	{
		LogHr("CreateBuffer(scene constants)", hr);
		return false;
	}

	// Explicitly disable culling. The scene pass sets this state every frame,
	// rather than depending on state left by ImGui or another pass.
	D3D11_RASTERIZER_DESC rasterDesc{};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.DepthClipEnable = TRUE;
	rasterDesc.ScissorEnable = FALSE;

	hr = g_d3d.device->CreateRasterizerState(
		&rasterDesc,
		g_d3d.sceneRasterizerState.GetAddressOf());

	if (FAILED(hr))
	{
		LogHr("CreateRasterizerState(scene)", hr);
		return false;
	}

	return true;
}

// -----------------------------------------------------------------------------
// Offscreen scene texture
// -----------------------------------------------------------------------------

static bool ResizeSceneTexture(UINT width, UINT height)
{
	width = std::clamp<UINT>(width, 1, 8192);
	height = std::clamp<UINT>(height, 1, 8192);

	if (width == g_d3d.sceneWidth &&
		height == g_d3d.sceneHeight &&
		g_d3d.sceneRTV &&
		g_d3d.sceneSRV)
	{
		return true;
	}

	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags =
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	// Build the replacement resources first. If any creation fails, the
	// currently usable scene texture and its views remain untouched.
	ComPtr<ID3D11Texture2D> newTexture;
	ComPtr<ID3D11RenderTargetView> newRTV;
	ComPtr<ID3D11ShaderResourceView> newSRV;

	HRESULT hr = g_d3d.device->CreateTexture2D(
		&textureDesc,
		nullptr,
		newTexture.GetAddressOf());

	if (FAILED(hr))
	{
		LogHr("CreateTexture2D(scene)", hr);
		return false;
	}

#if RESIZE_DIAG_ENABLED
	const auto total = ++ResizeDiag::rtvCreateCount;
	const auto number = ++ResizeDiag::sceneRtvCreateCount;

	ResizeDiag::Log(
		"SceneRTV #%llu total=%llu begin size=%ux%u texture=%p",
		number, total, width, height,
		static_cast<void*>(newTexture.Get()));
#endif

	hr = g_d3d.device->CreateRenderTargetView(
		newTexture.Get(),
		nullptr,
		newRTV.GetAddressOf());

#if RESIZE_DIAG_ENABLED
	ResizeDiag::Log(
		"SceneRTV #%llu end size=%ux%u hr=0x%08X",
		number, width, height, static_cast<unsigned int>(hr));
#endif

	if (FAILED(hr))
	{
		LogHr("CreateRenderTargetView(scene)", hr);
		return false;
	}

	hr = g_d3d.device->CreateShaderResourceView(
		newTexture.Get(),
		nullptr,
		newSRV.GetAddressOf());

	if (FAILED(hr))
	{
		LogHr("CreateShaderResourceView(scene)", hr);
		return false;
	}

	// Detach the old texture from the pipeline before releasing it. The
	// render thread should be the only thread executing D3D context calls.
	ID3D11ShaderResourceView* nullSRV = nullptr;
	g_d3d.context->PSSetShaderResources(0, 1, &nullSRV);
	g_d3d.context->OMSetRenderTargets(0, nullptr, nullptr);

	g_d3d.sceneTexture = std::move(newTexture);
	g_d3d.sceneRTV = std::move(newRTV);
	g_d3d.sceneSRV = std::move(newSRV);
	g_d3d.sceneWidth = width;
	g_d3d.sceneHeight = height;

#if RESIZE_DIAG_ENABLED
	const auto generation = ++ResizeDiag::sceneTextureGeneration;

	ResizeDiag::Log(
		"Scene target replaced generation=%llu size=%ux%u texture=%p rtv=%p srv=%p",
		generation,
		width, height,
		static_cast<void*>(g_d3d.sceneTexture.Get()),
		static_cast<void*>(g_d3d.sceneRTV.Get()),
		static_cast<void*>(g_d3d.sceneSRV.Get()));
#endif

	return true;
}

static bool ApplySceneResize()
{
	static uint64_t lastObservedRequest = 0;
	static auto lastRequestChange = std::chrono::steady_clock::now();
	static auto lastApplyTime = std::chrono::steady_clock::time_point{};
	static bool hasApplied = false;

	const uint64_t request =
		g_mainResizeRequest.load(std::memory_order_acquire);

	const auto now = std::chrono::steady_clock::now();

	const bool requestChanged = request != lastObservedRequest;
	if (requestChanged)
	{
		lastObservedRequest = request;
		lastRequestChange = now;
	}

	UINT width = 0;
	UINT height = 0;
	UnpackSize(request, width, height);

#if RESIZE_DIAG_ENABLED
	if (requestChanged)
	{
		UINT mainWidth = 0;
		UINT mainHeight = 0;
		const uint64_t mainRequest =
			g_mainResizeRequest.load(std::memory_order_acquire);
		UnpackSize(mainRequest, mainWidth, mainHeight);

		UINT appliedWidth = 0;
		UINT appliedHeight = 0;
		const uint64_t mainApplied =
			ResizeDiag::mainResizeAppliedSize.load(std::memory_order_acquire);
		UnpackSize(mainApplied, appliedWidth, appliedHeight);

		ResizeDiag::Log(
			"ApplySceneResize request=0x%016llX requested=%ux%u "
			"sceneCurrent=%ux%u mainRequest=0x%016llX (%ux%u) "
			"mainApplied=0x%016llX (%ux%u)",
			static_cast<unsigned long long>(request),
			width, height,
			g_d3d.sceneWidth, g_d3d.sceneHeight,
			static_cast<unsigned long long>(mainRequest),
			mainWidth, mainHeight,
			static_cast<unsigned long long>(mainApplied),
			appliedWidth, appliedHeight);
	}
#endif

	if (width == 0 || height == 0)
		return true;

	if (width == g_d3d.sceneWidth &&
		height == g_d3d.sceneHeight)
	{
		return true;
	}

	constexpr auto updateInterval = std::chrono::milliseconds(50);
	constexpr auto settleDelay = std::chrono::milliseconds(120);

	const bool settled = now - lastRequestChange >= settleDelay;
	const bool intervalElapsed =
		!hasApplied || now - lastApplyTime >= updateInterval;

	if (!settled && !intervalElapsed)
		return true;

	if (!ResizeSceneTexture(width, height))
		return false;

	lastApplyTime = now;
	hasApplied = true;
	return true;
}

// -----------------------------------------------------------------------------
// Main swap-chain resize
// -----------------------------------------------------------------------------

bool ApplyMainResize()
{
	static bool hasResizeAttempt = false;
	static auto lastResizeAttempt =
		std::chrono::steady_clock::time_point{};

	UINT width = 0;
	UINT height = 0;

	const uint64_t requestedSize =
		g_mainResizeRequest.load(std::memory_order_acquire);

	UnpackSize(requestedSize, width, height);

	if (width == 0 || height == 0)
		return true;

	const bool forceResize =
		g_forceMainResize.exchange(false, std::memory_order_acq_rel);

	if (width == g_d3d.backBufferWidth &&
		height == g_d3d.backBufferHeight)
	{
#if RESIZE_DIAG_ENABLED
		// No ResizeBuffers call is needed; this requested size is already applied.
		ResizeDiag::mainResizeAppliedSize.store(
			PackSize(width, height),
			std::memory_order_release);
#endif
		return true;
	}

	const auto now = std::chrono::steady_clock::now();
	const bool interactive =
		g_interactiveResize.load(std::memory_order_acquire);

	constexpr auto throttleInterval = std::chrono::milliseconds(50);

	if (interactive &&
		!forceResize &&
		hasResizeAttempt &&
		now - lastResizeAttempt < throttleInterval)
	{
		return true;
	}

	hasResizeAttempt = true;
	lastResizeAttempt = now;

	g_d3d.context->OMSetRenderTargets(0, nullptr, nullptr);
	g_d3d.backBufferRTV.Reset();

#if RESIZE_DIAG_ENABLED
	const auto resizeNumber = ++ResizeDiag::resizeBuffersCount;

	ResizeDiag::Log(
		"ResizeBuffers #%llu begin size=%ux%u",
		resizeNumber,
		width,
		height);

	// Capture the session before the call. If a resize session starts or ends
	// while ResizeBuffers is in progress, this call won't be misattributed.
	const uint64_t diagSession =
		ResizeDiag::ActiveMeasurementSession();

	const auto resizeStart = std::chrono::steady_clock::now();
#endif

	const HRESULT resizeHr = g_d3d.swapChain->ResizeBuffers(
		0, width, height, DXGI_FORMAT_UNKNOWN, 0);

#if RESIZE_DIAG_ENABLED
	const auto resizeEnd = std::chrono::steady_clock::now();

	const auto resizeUs =
		static_cast<unsigned long long>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				resizeEnd - resizeStart).count());

	ResizeDiag::RecordResizeBuffersTiming(
		diagSession,
		SUCCEEDED(resizeHr),
		resizeUs);

	ResizeDiag::Log(
		"ResizeBuffers #%llu end hr=0x%08X duration=%llu us",
		resizeNumber,
		static_cast<unsigned int>(resizeHr),
		resizeUs);
#endif

	if (FAILED(resizeHr))
	{
		LogHr("ResizeBuffers(main)", resizeHr);
		return false;
	}

	if (!CreateBackBufferRTV())
		return false;

#if RESIZE_DIAG_ENABLED
	// Acknowledge the size only after ResizeBuffers and RTV creation succeeded.
	ResizeDiag::mainResizeAppliedSize.store(
		PackSize(width, height),
		std::memory_order_release);
#endif

	return true;
}

// -----------------------------------------------------------------------------
// Scene rendering: called on the render thread before ImGui::NewFrame.
// -----------------------------------------------------------------------------

static void RenderScene(const SceneSnapshot& snapshot)
{
	if (!g_d3d.sceneRTV ||
		!g_d3d.sceneRasterizerState ||
		!g_d3d.sceneConstantBuffer)
	{
		return;
	}

	auto* context = g_d3d.context.Get();

	D3D11_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(g_d3d.sceneWidth);
	viewport.Height = static_cast<float>(g_d3d.sceneHeight);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	const float clearColor[] = { 0.075f, 0.085f, 0.11f, 1.0f };

	ID3D11ShaderResourceView* nullSRV = nullptr;
	context->PSSetShaderResources(0, 1, &nullSRV);

	context->OMSetRenderTargets(
		1, g_d3d.sceneRTV.GetAddressOf(), nullptr);
	context->RSSetViewports(1, &viewport);

	// Establish scene state explicitly instead of inheriting UI state.
	context->RSSetState(g_d3d.sceneRasterizerState.Get());
	context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	context->OMSetDepthStencilState(nullptr, 0);

	context->ClearRenderTargetView(
		g_d3d.sceneRTV.Get(), clearColor);

	struct SceneConstants
	{
		float angle;
		float padding[3];
	};

	static_assert(sizeof(SceneConstants) == 16);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	const HRESULT mapHr = context->Map(
		g_d3d.sceneConstantBuffer.Get(),
		0,
		D3D11_MAP_WRITE_DISCARD,
		0,
		&mapped);

	if (SUCCEEDED(mapHr))
	{
		auto* constants =
			static_cast<SceneConstants*>(mapped.pData);

		constants->angle = snapshot.angle;
		constants->padding[0] = 0.0f;
		constants->padding[1] = 0.0f;
		constants->padding[2] = 0.0f;

		context->Unmap(g_d3d.sceneConstantBuffer.Get(), 0);

		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		context->VSSetShader(g_d3d.sceneVS.Get(), nullptr, 0);
		context->VSSetConstantBuffers(
			0, 1, g_d3d.sceneConstantBuffer.GetAddressOf());
		context->PSSetShader(g_d3d.scenePS.Get(), nullptr, 0);

		context->Draw(3, 0);
	}
	else
	{
		LogHr("Map(scene constant buffer)", mapHr);
	}

	// ImGui samples the scene SRV after this function returns.
	context->OMSetRenderTargets(0, nullptr, nullptr);
}

// -----------------------------------------------------------------------------
// Input queue: called only on the render thread, before ImGui::NewFrame().
// The main-window WndProc never calls ImGui.
// -----------------------------------------------------------------------------

static ImGuiKey KeyFromVirtualKey(WPARAM vk)
{
	if (vk >= 'A' && vk <= 'Z')
		return static_cast<ImGuiKey>(ImGuiKey_A + (vk - 'A'));

	if (vk >= '0' && vk <= '9')
		return static_cast<ImGuiKey>(ImGuiKey_0 + (vk - '0'));

	if (vk >= VK_F1 && vk <= VK_F12)
		return static_cast<ImGuiKey>(ImGuiKey_F1 + (vk - VK_F1));

	switch (vk)
	{
	case VK_TAB: return ImGuiKey_Tab;
	case VK_LEFT: return ImGuiKey_LeftArrow;
	case VK_RIGHT: return ImGuiKey_RightArrow;
	case VK_UP: return ImGuiKey_UpArrow;
	case VK_DOWN: return ImGuiKey_DownArrow;
	case VK_PRIOR: return ImGuiKey_PageUp;
	case VK_NEXT: return ImGuiKey_PageDown;
	case VK_HOME: return ImGuiKey_Home;
	case VK_END: return ImGuiKey_End;
	case VK_INSERT: return ImGuiKey_Insert;
	case VK_DELETE: return ImGuiKey_Delete;
	case VK_BACK: return ImGuiKey_Backspace;
	case VK_SPACE: return ImGuiKey_Space;
	case VK_RETURN: return ImGuiKey_Enter;
	case VK_ESCAPE: return ImGuiKey_Escape;
	case VK_OEM_7: return ImGuiKey_Apostrophe;
	case VK_OEM_COMMA: return ImGuiKey_Comma;
	case VK_OEM_MINUS: return ImGuiKey_Minus;
	case VK_OEM_PERIOD: return ImGuiKey_Period;
	case VK_OEM_2: return ImGuiKey_Slash;
	case VK_OEM_1: return ImGuiKey_Semicolon;
	case VK_OEM_PLUS: return ImGuiKey_Equal;
	case VK_OEM_4: return ImGuiKey_LeftBracket;
	case VK_OEM_5: return ImGuiKey_Backslash;
	case VK_OEM_6: return ImGuiKey_RightBracket;
	case VK_OEM_3: return ImGuiKey_GraveAccent;
	case VK_CAPITAL: return ImGuiKey_CapsLock;
	case VK_SCROLL: return ImGuiKey_ScrollLock;
	case VK_NUMLOCK: return ImGuiKey_NumLock;
	case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
	case VK_PAUSE: return ImGuiKey_Pause;
	case VK_NUMPAD0: return ImGuiKey_Keypad0;
	case VK_NUMPAD1: return ImGuiKey_Keypad1;
	case VK_NUMPAD2: return ImGuiKey_Keypad2;
	case VK_NUMPAD3: return ImGuiKey_Keypad3;
	case VK_NUMPAD4: return ImGuiKey_Keypad4;
	case VK_NUMPAD5: return ImGuiKey_Keypad5;
	case VK_NUMPAD6: return ImGuiKey_Keypad6;
	case VK_NUMPAD7: return ImGuiKey_Keypad7;
	case VK_NUMPAD8: return ImGuiKey_Keypad8;
	case VK_NUMPAD9: return ImGuiKey_Keypad9;
	case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
	case VK_DIVIDE: return ImGuiKey_KeypadDivide;
	case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
	case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
	case VK_ADD: return ImGuiKey_KeypadAdd;
	case VK_LSHIFT: return ImGuiKey_LeftShift;
	case VK_RSHIFT: return ImGuiKey_RightShift;
	case VK_LCONTROL: return ImGuiKey_LeftCtrl;
	case VK_RCONTROL: return ImGuiKey_RightCtrl;
	case VK_LMENU: return ImGuiKey_LeftAlt;
	case VK_RMENU: return ImGuiKey_RightAlt;
	case VK_LWIN: return ImGuiKey_LeftSuper;
	case VK_RWIN: return ImGuiKey_RightSuper;
	default: return ImGuiKey_None;
	}
}

static void ProcessQueuedImGuiInput(ImGuiIO& io)
{
	std::deque<InputEvent> events;
	{
		std::lock_guard lock(g_inputMutex);
		events.swap(g_inputQueue);
	}

	for (const InputEvent& event : events)
	{
		switch (event.message)
		{
		case WM_MOUSEMOVE:
			io.AddMousePosEvent(
				static_cast<float>(GET_X_LPARAM(event.lParam)),
				static_cast<float>(GET_Y_LPARAM(event.lParam)));
			break;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
			io.AddMouseButtonEvent(
				0, event.message == WM_LBUTTONDOWN);
			break;

		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
			io.AddMouseButtonEvent(
				1, event.message == WM_RBUTTONDOWN);
			break;

		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
			io.AddMouseButtonEvent(
				2, event.message == WM_MBUTTONDOWN);
			break;

		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		{
			const int button =
				GET_XBUTTON_WPARAM(event.wParam) == XBUTTON1 ? 3 : 4;
			io.AddMouseButtonEvent(
				button, event.message == WM_XBUTTONDOWN);
			break;
		}

		case WM_MOUSEWHEEL:
			io.AddMouseWheelEvent(
				0.0f,
				static_cast<float>(
					GET_WHEEL_DELTA_WPARAM(event.wParam)) /
				static_cast<float>(WHEEL_DELTA));
			break;

		case WM_MOUSEHWHEEL:
			io.AddMouseWheelEvent(
				static_cast<float>(
					GET_WHEEL_DELTA_WPARAM(event.wParam)) /
				static_cast<float>(WHEEL_DELTA),
				0.0f);
			break;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
		{
			const bool down =
				event.message == WM_KEYDOWN ||
				event.message == WM_SYSKEYDOWN;

			const ImGuiKey key = KeyFromVirtualKey(event.wParam);
			if (key != ImGuiKey_None)
				io.AddKeyEvent(key, down);

			io.AddKeyEvent(ImGuiMod_Ctrl, event.ctrl);
			io.AddKeyEvent(ImGuiMod_Shift, event.shift);
			io.AddKeyEvent(ImGuiMod_Alt, event.alt);
			break;
		}

		case WM_CHAR:
			io.AddInputCharacterUTF16(
				static_cast<unsigned short>(event.wParam));
			break;

		case WM_SETFOCUS:
			io.AddFocusEvent(true);
			break;

		case WM_KILLFOCUS:
			//io.AddFocusEvent(false);
			//break;
		{
			HWND nextWindow = reinterpret_cast<HWND>(event.wParam);
			DWORD nextProcessId = 0;

			if (nextWindow)
				GetWindowThreadProcessId(nextWindow, &nextProcessId);

			// Don't report app-wide focus loss when focus is moving to another
			// window belonging to this application, such as a detached ImGui viewport.
			if (!nextWindow || nextProcessId != GetCurrentProcessId())
				io.AddFocusEvent(false);

			break;
		}

		} // switch
	}
}

// -----------------------------------------------------------------------------
// Dockspace and UI
// -----------------------------------------------------------------------------

static void DrawSceneWindow()
{
	if (!ImGui::Begin("Scene"))
	{
		ImGui::End();
		return;
	}

	const ImVec2 available = ImGui::GetContentRegionAvail();

	if (available.x > 1.0f && available.y > 1.0f)
	{
		const UINT width = std::max<UINT>(
			1, static_cast<UINT>(std::lround(available.x)));
		const UINT height = std::max<UINT>(
			1, static_cast<UINT>(std::lround(available.y)));

		// The renderer applies this request on its next frame.
		g_sceneResizeRequest.store(
			PackSize(width, height),
			std::memory_order_release);

#if RESIZE_DIAG_ENABLED
		static UINT lastLoggedWidth = 0;
		static UINT lastLoggedHeight = 0;
		static ULONGLONG lastLogTime = 0;

		const ULONGLONG now = GetTickCount64();

		if (width != lastLoggedWidth ||
			height != lastLoggedHeight ||
			now - lastLogTime >= 250)
		{
			lastLoggedWidth = width;
			lastLoggedHeight = height;
			lastLogTime = now;

			ResizeDiag::Log(
				"Scene image request=%ux%u display=%.1fx%.1f texture=%ux%u generation=%llu",
				width, height,
				available.x, available.y,
				g_d3d.sceneWidth, g_d3d.sceneHeight,
				ResizeDiag::sceneTextureGeneration);
		}
#endif

		if (g_d3d.sceneSRV)
		{
			const ImTextureID textureId =
				(ImTextureID)(intptr_t)g_d3d.sceneSRV.Get();

			ImGui::Image(textureId, available);
		}
		else
		{
			ImGui::TextUnformatted("Scene texture not ready");
		}
	}

	ImGui::End();
}

static void DrawInspectorWindow(
	const std::shared_ptr<const SceneSnapshot>& snapshot)
{
	if (!ImGui::Begin("Inspector"))
	{
		ImGui::End();
		return;
	}

	ImGui::TextUnformatted("Scene snapshot");
	ImGui::Separator();

	if (snapshot)
		ImGui::Text("Angle: %.3f", snapshot->angle);
	else
		ImGui::TextUnformatted("No snapshot");

	ImGui::End();
}

static void BuildInitialDockLayout(
	ImGuiID dockId,
	const ImVec2& dockSize)
{
	ImGui::DockBuilderRemoveNode(dockId);
	ImGui::DockBuilderAddNode(
		dockId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockId, dockSize);

	ImGuiID sceneId = 0;
	ImGuiID inspectorId = 0;

	ImGui::DockBuilderSplitNode(
		dockId,
		ImGuiDir_Left,
		0.72f,
		&sceneId,
		&inspectorId);

	ImGui::DockBuilderDockWindow("Scene", sceneId);
	ImGui::DockBuilderDockWindow("Inspector", inspectorId);
	ImGui::DockBuilderFinish(dockId);
}

static void DrawMainImGui(
	const std::shared_ptr<const SceneSnapshot>& snapshot)
{
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	const float titleHeight = 36.0f * DpiScale(g_hwnd);
	const float buttonWidth = 48.0f * DpiScale(g_hwnd);

	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

	constexpr ImGuiWindowFlags hostFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoDocking;

	ImGui::Begin("##MainDockHost", nullptr, hostFlags);

	const ImVec2 hostPos = ImGui::GetWindowPos();
	const ImVec2 hostSize = ImGui::GetWindowSize();

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(
		hostPos,
		ImVec2(hostPos.x + hostSize.x, hostPos.y + titleHeight),
		IM_COL32(32, 35, 43, 255));

	ImGui::SetCursorPos(ImVec2(12.0f * DpiScale(g_hwnd), 0));
	ImGui::SetCursorPosY(
		(titleHeight - ImGui::GetTextLineHeight()) * 0.5f);
	ImGui::TextUnformatted("DX11 Scene Editor");

	// The right side of the native hit-test area is HTCLIENT, so these
	// ImGui buttons receive clicks instead of starting a title-bar drag.
	ImGui::SetCursorPos(ImVec2(
		hostSize.x - 3.0f * buttonWidth,
		0.0f));

	if (ImGui::Button("—", ImVec2(buttonWidth, titleHeight)))
		PostMessageW(g_hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);

	ImGui::SameLine(0.0f, 0.0f);

	if (ImGui::Button(
		IsZoomed(g_hwnd) ? "❐" : "□",
		ImVec2(buttonWidth, titleHeight)))
	{
		PostMessageW(
			g_hwnd,
			WM_SYSCOMMAND,
			IsZoomed(g_hwnd) ? SC_RESTORE : SC_MAXIMIZE,
			0);
	}

	ImGui::SameLine(0.0f, 0.0f);

	if (ImGui::Button("×", ImVec2(buttonWidth, titleHeight)))
		PostMessageW(g_hwnd, WM_CLOSE, 0, 0);

	const ImGuiID dockId = ImGui::GetID("MainDockspace");

	static bool layoutBuilt = false;
	if (!layoutBuilt)
	{
		BuildInitialDockLayout(
			dockId,
			ImVec2(hostSize.x, hostSize.y - titleHeight));
		layoutBuilt = true;
	}

	ImGui::SetCursorPos(ImVec2(0.0f, titleHeight));
	const ImVec2 dockSize = ImGui::GetContentRegionAvail();

	ImGui::DockSpace(
		dockId,
		dockSize,
		ImGuiDockNodeFlags_None);

	ImGui::End();
	ImGui::PopStyleVar(3);

	DrawSceneWindow();
	DrawInspectorWindow(snapshot);
}

// -----------------------------------------------------------------------------
// Render-thread message pump
// Secondary viewport HWNDs are created on this thread, so this thread pumps
// their messages. The UI thread separately pumps the main HWND.
// -----------------------------------------------------------------------------

static void PumpRenderThreadMessages()
{
	MSG msg{};

	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
		{
			g_running.store(false, std::memory_order_release);
			continue;
		}

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

// -----------------------------------------------------------------------------
// Render thread: owns D3D11, ImGui, and all ImGui calls.
// -----------------------------------------------------------------------------

void RenderThread()
{
	// Create this thread's message queue before ImGui creates platform windows.
	MSG initialMessage{};
	PeekMessageW(&initialMessage, nullptr, 0, 0, PM_NOREMOVE);

	if (!CreateD3D(g_hwnd) ||
		!CreateScenePipeline() ||
		!ResizeSceneTexture(640, 480))
	{
		g_running.store(false, std::memory_order_release);
		return;
	}

	IMGUI_CHECKVERSION();
	ImGuiContext* imgui = ImGui::CreateContext();
	ImGui::SetCurrentContext(imgui);

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.IniFilename = nullptr;

	ImGui::StyleColorsDark();

	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 0.0f;
	style.WindowBorderSize = 0.0f;
	style.Colors[ImGuiCol_WindowBg] =
		ImVec4(0.10f, 0.11f, 0.14f, 1.0f);

	if (!ImGui_ImplWin32_Init(g_hwnd))
	{
		Log("ImGui_ImplWin32_Init failed");
		ImGui::DestroyContext(imgui);
		g_running.store(false, std::memory_order_release);
		return;
	}

	if (!ImGui_ImplDX11_Init(
		g_d3d.device.Get(),
		g_d3d.context.Get()))
	{
		Log("ImGui_ImplDX11_Init failed");
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext(imgui);
		g_running.store(false, std::memory_order_release);
		return;
	}

	const auto elapsedUs =
		[](const std::chrono::steady_clock::time_point& start,
			const std::chrono::steady_clock::time_point& end)
		{
			return static_cast<unsigned long long>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					end - start).count());
		};

	while (g_running.load(std::memory_order_acquire))
	{
#if RESIZE_DIAG_ENABLED
		// A frame that began before the resize session won't be measured.
		const uint64_t diagSession =
			ResizeDiag::ActiveMeasurementSession();

		const auto frameStart = std::chrono::steady_clock::now();
#endif

		ImGui::SetCurrentContext(imgui);
		PumpRenderThreadMessages();

		if (!g_running.load(std::memory_order_acquire))
			break;

#if RESIZE_DIAG_ENABLED
		const auto mainResizeStart = std::chrono::steady_clock::now();
#endif

		if (!ApplyMainResize())
		{
			g_running.store(false, std::memory_order_release);
			break;
		}

#if RESIZE_DIAG_ENABLED
		const auto mainResizeEnd = std::chrono::steady_clock::now();
		const auto mainResizeUs =
			elapsedUs(mainResizeStart, mainResizeEnd);

		const auto sceneApplyStart = std::chrono::steady_clock::now();
#endif

		const bool sceneResizeOk = ApplySceneResize();

#if RESIZE_DIAG_ENABLED
		const auto sceneApplyEnd = std::chrono::steady_clock::now();
		const auto sceneApplyUs =
			elapsedUs(sceneApplyStart, sceneApplyEnd);
#endif

		if (!sceneResizeOk)
		{
			g_running.store(false, std::memory_order_release);
			break;
		}

		UINT clientWidth = 0;
		UINT clientHeight = 0;
		UnpackSize(
			g_mainResizeRequest.load(std::memory_order_acquire),
			clientWidth,
			clientHeight);

		if (clientWidth == 0 || clientHeight == 0)
		{
			Sleep(10);
			continue;
		}

		auto snapshot =
			g_snapshot.load(std::memory_order_acquire);

#if RESIZE_DIAG_ENABLED
		const auto renderSceneStart = std::chrono::steady_clock::now();
#endif

		RenderScene(*snapshot);

#if RESIZE_DIAG_ENABLED
		const auto renderSceneEnd = std::chrono::steady_clock::now();
		const auto renderSceneUs =
			elapsedUs(renderSceneStart, renderSceneEnd);

		const auto imguiBuildStart = std::chrono::steady_clock::now();
#endif

		ProcessQueuedImGuiInput(io);

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		DrawMainImGui(snapshot);
		ImGui::Render();

#if RESIZE_DIAG_ENABLED
		const auto imguiBuildEnd = std::chrono::steady_clock::now();
		const auto imguiBuildUs =
			elapsedUs(imguiBuildStart, imguiBuildEnd);

		const auto mainDrawStart = std::chrono::steady_clock::now();
#endif

		const float clearColor[] = { 0.12f, 0.13f, 0.16f, 1.0f };

		g_d3d.context->OMSetRenderTargets(
			1, g_d3d.backBufferRTV.GetAddressOf(), nullptr);

		D3D11_VIEWPORT viewport{};
		viewport.Width = static_cast<float>(g_d3d.backBufferWidth);
		viewport.Height = static_cast<float>(g_d3d.backBufferHeight);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;

		g_d3d.context->RSSetViewports(1, &viewport);
		g_d3d.context->ClearRenderTargetView(
			g_d3d.backBufferRTV.Get(), clearColor);

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

#if RESIZE_DIAG_ENABLED
		const auto mainDrawEnd = std::chrono::steady_clock::now();
		const auto mainDrawUs =
			elapsedUs(mainDrawStart, mainDrawEnd);

		unsigned long long platformWindowsUs = 0;
#endif

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
#if RESIZE_DIAG_ENABLED
			const auto platformWindowsStart =
				std::chrono::steady_clock::now();
#endif

			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();

#if RESIZE_DIAG_ENABLED
			const auto platformWindowsEnd =
				std::chrono::steady_clock::now();

			platformWindowsUs =
				elapsedUs(platformWindowsStart, platformWindowsEnd);
#endif
		}

#if RESIZE_DIAG_ENABLED
		// Keep this lifetime counter; it is separate from session timings.
		const auto presentNumber = ++ResizeDiag::presentCount;
		const auto presentStart = std::chrono::steady_clock::now();
#endif

		const HRESULT presentHr =
			g_d3d.swapChain->Present(1, 0);

#if RESIZE_DIAG_ENABLED
		const auto presentEnd = std::chrono::steady_clock::now();
		const auto presentUs =
			elapsedUs(presentStart, presentEnd);
		const auto frameUs =
			elapsedUs(frameStart, presentEnd);

		ResizeDiag::RecordFrameTiming(
			diagSession,
			frameUs,
			presentUs,
			sceneApplyUs,
			mainResizeUs,
			renderSceneUs,
			imguiBuildUs,
			mainDrawUs,
			platformWindowsUs,
			true);

		if (FAILED(presentHr))
		{
			++ResizeDiag::presentFailureCount;
			ResizeDiag::Log(
				"Present #%llu FAILED hr=0x%08X",
				presentNumber,
				static_cast<unsigned int>(presentHr));
		}
		else if (presentNumber % 120 == 0)
		{
			ResizeDiag::Log("Present #%llu succeeded", presentNumber);
		}
#endif
	}

	ImGui::SetCurrentContext(imgui);
	ImGui::DestroyPlatformWindows();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext(imgui);

	if (g_d3d.context)
	{
		g_d3d.context->ClearState();
		g_d3d.context->Flush();
	}

	g_d3d = D3DState{};
}

// -----------------------------------------------------------------------------
// Main HWND: this procedure runs only on the UI thread.
// It queues input; it does not call ImGui.
// -----------------------------------------------------------------------------

static LRESULT CALLBACK MainWndProc(
	HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#if RESIZE_DIAG_ENABLED
	constexpr UINT IDT_RESIZE_SUMMARY = 1;
#endif
	switch (message)
	{
	case WM_NCCALCSIZE:
		// Remove the standard non-client frame; custom hit testing below
		// preserves resizing and dragging.
		if (wParam)
			return 0;
		break;

	case WM_NCHITTEST:
	{
		const float scale = DpiScale(hwnd);
		const int border = static_cast<int>(8.0f * scale);
		const int titleHeight = static_cast<int>(36.0f * scale);
		const int buttonsWidth = static_cast<int>(144.0f * scale);
		const bool maximized = IsZoomed(hwnd) != FALSE;

		RECT wr{};
		GetWindowRect(hwnd, &wr);

		const POINT screenPoint{
			GET_X_LPARAM(lParam),
			GET_Y_LPARAM(lParam)
		};

		// Only a restored window has resize borders.
		if (!maximized)
		{
			const bool left = screenPoint.x < wr.left + border;
			const bool right = screenPoint.x >= wr.right - border;
			const bool top = screenPoint.y < wr.top + border;
			const bool bottom = screenPoint.y >= wr.bottom - border;

			if (top && left) return HTTOPLEFT;
			if (top && right) return HTTOPRIGHT;
			if (bottom && left) return HTBOTTOMLEFT;
			if (bottom && right) return HTBOTTOMRIGHT;
			if (left) return HTLEFT;
			if (right) return HTRIGHT;
			if (top) return HTTOP;
			if (bottom) return HTBOTTOM;
		}

		POINT clientPoint = screenPoint;
		ScreenToClient(hwnd, &clientPoint);

		RECT clientRect{};
		GetClientRect(hwnd, &clientRect);

		if (clientPoint.y >= 0 && clientPoint.y < titleHeight)
		{
			// Keep the ImGui minimize/maximize/close buttons clickable.
			if (clientPoint.x >= clientRect.right - buttonsWidth)
				return HTCLIENT;

			return HTCAPTION;
		}

		return HTCLIENT;
	}

	case WM_ENTERSIZEMOVE:
	{
#if RESIZE_DIAG_ENABLED
		ResizeDiag::ResetResizeMeasurements();
#endif

		// Leave this disabled, as decided.
		// g_interactiveResize.store(true, std::memory_order_release);

		return 0;
	}

	case WM_SIZE:
	{
		const UINT width = LOWORD(lParam);
		const UINT height = HIWORD(lParam);

#if RESIZE_DIAG_ENABLED
		const auto n = ++ResizeDiag::wmSizeCount;

		ResizeDiag::Log(
			"WM_SIZE #%llu hwnd=%p size=%ux%u wParam=%llu",
			n,
			hwnd,
			width,
			height,
			static_cast<unsigned long long>(wParam));
#endif

		RECT rc{};
		GetClientRect(hwnd, &rc);

		g_mainResizeRequest.store(
			PackSize(
				static_cast<UINT>(rc.right - rc.left),
				static_cast<UINT>(rc.bottom - rc.top)),
			std::memory_order_release);

		return 0;
	}

	case WM_EXITSIZEMOVE:
	{
		RECT rc{};
		GetClientRect(hwnd, &rc);

		g_mainResizeRequest.store(
			PackSize(
				static_cast<UINT>(rc.right - rc.left),
				static_cast<UINT>(rc.bottom - rc.top)),
			std::memory_order_release);

		g_interactiveResize.store(false, std::memory_order_release);
		g_forceMainResize.store(true, std::memory_order_release);

#if RESIZE_DIAG_ENABLED
		SetTimer(hwnd, IDT_RESIZE_SUMMARY, 50, nullptr);
#endif
		return 0;
	}

	case WM_TIMER:
	{
#if RESIZE_DIAG_ENABLED
		if (wParam == IDT_RESIZE_SUMMARY)
		{
			const uint64_t requestedSize =
				g_mainResizeRequest.load(std::memory_order_acquire);

			const uint64_t appliedSize =
				ResizeDiag::mainResizeAppliedSize.load(
					std::memory_order_acquire);

			// Keep waiting until the render thread has applied this size.
			if (requestedSize != 0 && requestedSize == appliedSize)
			{
				KillTimer(hwnd, IDT_RESIZE_SUMMARY);

				UINT width = 0;
				UINT height = 0;
				UnpackSize(requestedSize, width, height);

				ResizeDiag::Log(
					"Final main resize applied: size=%ux%u",
					width,
					height);

				ResizeDiag::EndResizeMeasurements("final size acknowledged");
			}

			return 0;
		}
#endif
		break;
	}

	case WM_DPICHANGED:
	{
		const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
		SetWindowPos(
			hwnd, nullptr,
			suggested->left, suggested->top,
			suggested->right - suggested->left,
			suggested->bottom - suggested->top,
			SWP_NOZORDER | SWP_NOACTIVATE);
		return 0;
	}

	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_CHAR:
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		QueueInput(message, wParam, lParam);

		// Capture/release stays on the HWND-owning UI thread.
		if (message == WM_LBUTTONDOWN ||
			message == WM_RBUTTONDOWN ||
			message == WM_MBUTTONDOWN ||
			message == WM_XBUTTONDOWN)
		{
			SetCapture(hwnd);
		}
		else if (message == WM_LBUTTONUP ||
			message == WM_RBUTTONUP ||
			message == WM_MBUTTONUP ||
			message == WM_XBUTTONUP)
		{
			ReleaseCapture();
		}

		// Let Windows handle system shortcuts such as Alt+F4.
		if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
			return DefWindowProcW(hwnd, message, wParam, lParam);

		return 0;

	case WM_CLOSE:
		g_running.store(false, std::memory_order_release);
		PostQuitMessage(0);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(hwnd, message, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Scene worker: simulation only; it never calls ImGui or D3D11.
// -----------------------------------------------------------------------------

void SceneWorkerThread()
{
	float angle = 0.0f;

	while (g_running.load(std::memory_order_acquire))
	{
		angle += 0.015f;
		if (angle > 6.2831853f)
			angle -= 6.2831853f;

		auto next = std::make_shared<SceneSnapshot>();
		next->angle = angle;
		g_snapshot.store(
			std::const_pointer_cast<const SceneSnapshot>(next),
			std::memory_order_release);

		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
}

// -----------------------------------------------------------------------------
// UI thread entry point: creates and pumps the main HWND.
// -----------------------------------------------------------------------------

int WINAPI _wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	// Must happen before creating any HWND.
	SetProcessDpiAwarenessContext(
		DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.hInstance = instance;
	wc.lpfnWndProc = MainWndProc;
	wc.lpszClassName = L"ImGuiD3D11ThreeThreadSample";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	if (!RegisterClassExW(&wc))
		return 1;

	const DWORD style =
		WS_POPUP | WS_THICKFRAME |
		WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;

	g_hwnd = CreateWindowExW(
		0,
		wc.lpszClassName,
		L"DX11 ImGui Three-Thread Sample",
		style,
		100, 100, 1280, 800,
		nullptr, nullptr, instance, nullptr);

	if (!g_hwnd)
		return 1;

	RECT rc{};
	GetClientRect(g_hwnd, &rc);
	g_mainResizeRequest.store(
		PackSize(
			static_cast<UINT>(rc.right - rc.left),
			static_cast<UINT>(rc.bottom - rc.top)),
		std::memory_order_release);

	ShowWindow(g_hwnd, showCommand);
	UpdateWindow(g_hwnd);

	std::thread sceneThread(SceneWorkerThread);

#if SINGLE_THREAD_RENDER_TEST
	// Test path: message handling, ImGui, and D3D11 all run on this thread.
	RenderThread();
#else
	std::thread renderThread(RenderThread);

	MSG msg{};
	while (GetMessageW(&msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
#endif

	g_running.store(false, std::memory_order_release);

	if (sceneThread.joinable())
		sceneThread.join();
#if !SINGLE_THREAD_RENDER_TEST
	if (renderThread.joinable())
		renderThread.join();
#endif

	if (IsWindow(g_hwnd))
		DestroyWindow(g_hwnd);

	UnregisterClassW(wc.lpszClassName, instance);
	return 0;
}
