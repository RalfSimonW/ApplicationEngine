#include "dxerr.h"
#include "Graphics.h"
#include "GraphicsThrowMacros.h"
#include "RedirectConsole.h"
#include "ResizeDiag.h"


#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <sstream>
#include <d3dcompiler.h>
#include <cmath>
#include <DirectXMath.h>

#include <utility>
#include <cmath>
#include <unordered_set>
#include <unordered_map>

namespace wrl = Microsoft::WRL;
namespace dx = DirectX;

static void SetSecondaryViewportFramebufferScales()
{
	ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();

	for (int i = 0; i < platformIO.Viewports.Size; ++i)
	{
		ImGuiViewport* viewport = platformIO.Viewports[i];

		// Leave the main viewport at its existing 1.50 scale.
		if (viewport == mainViewport || viewport->DrawData == nullptr)
			continue;

		ImDrawData* drawData = viewport->DrawData;
		if (drawData->DisplaySize.x <= 0.0f ||
			drawData->DisplaySize.y <= 0.0f)
			continue;

		HWND hwnd = reinterpret_cast<HWND>(viewport->PlatformHandle);
		if (hwnd == nullptr)
			continue;

		RECT clientRect{};
		if (!GetClientRect(hwnd, &clientRect))
			continue;

		const float clientWidth =
			static_cast<float>(clientRect.right - clientRect.left);
		const float clientHeight =
			static_cast<float>(clientRect.bottom - clientRect.top);

		drawData->FramebufferScale = ImVec2(
			clientWidth / drawData->DisplaySize.x,
			clientHeight / drawData->DisplaySize.y);
	}
}

static void LogViewportScaleDiagnostic()
{
	using Clock = std::chrono::steady_clock;

	static Clock::time_point lastLog{};
	const auto now = Clock::now();

	// Avoid flooding the log.
	if (now - lastLog < std::chrono::milliseconds(500))
		return;

	lastLog = now;

	ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();

	for (int i = 0; i < platformIO.Viewports.Size; ++i)
	{
		ImGuiViewport* viewport = platformIO.Viewports[i];
		ImDrawData* drawData = viewport->DrawData;
		HWND hwnd = reinterpret_cast<HWND>(viewport->PlatformHandle);

		if (hwnd == nullptr || drawData == nullptr)
			continue;

		RECT clientRect{};
		if (!GetClientRect(hwnd, &clientRect))
			continue;

		const LONG clientWidth = clientRect.right - clientRect.left;
		const LONG clientHeight = clientRect.bottom - clientRect.top;

		const float ratioX = drawData->DisplaySize.x > 0.0f
			? static_cast<float>(clientWidth) / drawData->DisplaySize.x
			: 0.0f;

		const float ratioY = drawData->DisplaySize.y > 0.0f
			? static_cast<float>(clientHeight) / drawData->DisplaySize.y
			: 0.0f;

		ResizeDiag::Log(
			"Viewport scale: id=%u hwnd=%p dpi=%.2f "
			"client=%ldx%ld display=%.1fx%.1f "
			"fbScale=%.2fx%.2f client/display=%.2fx%.2f",
			static_cast<unsigned>(viewport->ID),
			hwnd,
			viewport->DpiScale,
			clientWidth,
			clientHeight,
			drawData->DisplaySize.x,
			drawData->DisplaySize.y,
			drawData->FramebufferScale.x,
			drawData->FramebufferScale.y,
			ratioX,
			ratioY);
	}
}

static float DpiScale(HWND hwnd) noexcept
{
	using GetDpiForWindowFn = UINT(WINAPI*)(HWND);

	static const auto getDpiForWindow =
		reinterpret_cast<GetDpiForWindowFn>(
			GetProcAddress(
				GetModuleHandleW(L"user32.dll"),
				"GetDpiForWindow"));

	const UINT dpi = getDpiForWindow ? getDpiForWindow(hwnd) : 96u;
	return static_cast<float>(dpi) / 96.0f;
}

static void EnsureSecondaryViewportsAreResizable()
{
	static std::unordered_set<HWND> correctedViewports;

	// Remove handles for secondary windows that have been destroyed.
	for (auto it = correctedViewports.begin();
		it != correctedViewports.end();)
	{
		if (!IsWindow(*it))
			it = correctedViewports.erase(it);
		else
			++it;
	}

	using AdjustWindowRectExForDpiFn =
		BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);

	static const auto adjustForDpi =
		reinterpret_cast<AdjustWindowRectExForDpiFn>(
			GetProcAddress(
				GetModuleHandleW(L"user32.dll"),
				"AdjustWindowRectExForDpi"));

	ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();

	for (int i = 1; i < platformIO.Viewports.Size; ++i)
	{
		ImGuiViewport* viewport = platformIO.Viewports[i];
		if (viewport == nullptr || viewport->PlatformHandle == nullptr)
			continue;

		HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);

		if (correctedViewports.find(hwnd) != correctedViewports.end())
			continue;

		const int targetWidth =
			static_cast<int>(std::lround(viewport->Size.x));
		const int targetHeight =
			static_cast<int>(std::lround(viewport->Size.y));

		if (targetWidth <= 0 || targetHeight <= 0)
			continue;

		const LONG_PTR oldStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
		const LONG_PTR newStyle = oldStyle | WS_THICKFRAME;
		const bool styleChanged = (oldStyle != newStyle);

		RECT client{};
		if (!GetClientRect(hwnd, &client))
			continue;

		const int clientWidth = client.right - client.left;
		const int clientHeight = client.bottom - client.top;

		// This viewport already has the desired style and client size.
		if (!styleChanged &&
			clientWidth == targetWidth &&
			clientHeight == targetHeight)
		{
			correctedViewports.insert(hwnd);
			continue;
		}

		if (styleChanged)
		{
			SetLastError(ERROR_SUCCESS);
			SetWindowLongPtrW(hwnd, GWL_STYLE, newStyle);

			if (GetLastError() != ERROR_SUCCESS)
				continue;
		}

		// viewport->Size is the desired client size. Calculate the outer
		// dimensions needed to produce that client size with this window style.
		RECT outer{ 0, 0, targetWidth, targetHeight };
		const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		const UINT dpi =
			static_cast<UINT>(std::lround(DpiScale(hwnd) * 96.0f));

		const BOOL adjusted = adjustForDpi
			? adjustForDpi(
				&outer,
				static_cast<DWORD>(newStyle),
				FALSE, // These ImGui viewport windows have no menu.
				static_cast<DWORD>(exStyle),
				dpi)
			: AdjustWindowRectEx(
				&outer,
				static_cast<DWORD>(newStyle),
				FALSE,
				static_cast<DWORD>(exStyle));

		if (!adjusted)
			continue;

		const BOOL positioned = SetWindowPos(
			hwnd,
			nullptr,
			0, 0,
			outer.right - outer.left,
			outer.bottom - outer.top,
			SWP_NOMOVE | SWP_NOZORDER |
			SWP_NOACTIVATE | SWP_FRAMECHANGED);

		if (!positioned)
		{
			if (styleChanged)
			{
				SetWindowLongPtrW(hwnd, GWL_STYLE, oldStyle);
				SetWindowPos(
					hwnd,
					nullptr,
					0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					SWP_NOACTIVATE | SWP_FRAMECHANGED);
			}

			continue;
		}

		correctedViewports.insert(hwnd);
	}
}


static ImVec2 previousSize(-1.0f, -1.0f);

Graphics::Graphics(HWND hWnd, int width, int height)
{
	Initialize(hWnd, width, height);	

	// Init ImGui Win32 Impl
	ImGui_ImplWin32_Init(hWnd);

	// Init ImGui D3D Impl
	ImGui_ImplDX11_Init(cpDevice.Get(), cpDeviceContext.Get());
}

Graphics::~Graphics()
{
	// Important: destroy ImGui before destroying the D3D11 device
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// This also destroys Dear ImGui's secondary platform windows and their associated DX11 resources
	ImGui_ImplDX11_Shutdown(); // ImGui::DestroyContext() is done in ImGuiManager destructor
	Shutdown();
}

void Graphics::Initialize(HWND hWnd, int width, int height) {
	hWindow = hWnd;

	RECT clientRect{};
	if (::GetClientRect(hWindow, &clientRect) &&
		clientRect.right > clientRect.left &&
		clientRect.bottom > clientRect.top)
	{
		width = clientRect.right - clientRect.left;
		height = clientRect.bottom - clientRect.top;
	}

	// Avoid creating zero-sized buffers.
	mOutputWidth = std::max(width, 1);
	mOutputHeight = std::max(height, 1);

	const float dpiScale = DpiScale(hWindow);
	ImGui::GetStyle().ScaleAllSizes(dpiScale);
	ImGui::GetIO().FontGlobalScale = dpiScale;
	// OutputDebugString(("Graphics::Initialize (dpiScale): " + std::to_string(dpiScale) + "\n").c_str());

	// Create device and swap chain before init ImGui
	CreateDeviceAndSwapChain(hWindow, mOutputWidth, mOutputHeight);

	CreateGpuTimingQueries();

	CreateMainRenderTarget();
	
	SceneWidth = mOutputWidth;
	SceneHeight = mOutputHeight;

	CreateDepthBuffer();
	
	CreateSceneTexture();

	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;
	m_viewport.Width = static_cast<float>(mOutputWidth);
	m_viewport.Height = static_cast<float>(mOutputHeight);
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
}

void Graphics::CreateDeviceAndSwapChain(HWND hWnd, int width, int height)
{
	// If the swap chain does not exist, create it.
	DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };

	// Use two buffers to enable flip effect.
	swapChainDesc.BufferCount = 2;

	// Do not use automatic sizing.
	swapChainDesc.BufferDesc.Width = width;
	swapChainDesc.BufferDesc.Height = height;

	// This is the most common swap chain format.
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

	swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;

	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.Flags = 0;

	// Don't use multi-sampling.
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;

	swapChainDesc.OutputWindow = hWnd;
	swapChainDesc.Windowed = TRUE;
	
	//[???] When using DXGI_SWAP_EFFECT_FLIP_DISCARD instead of DXGI_SWAP_EFFECT_DISCARD:
	//      After resizing the window, release references to the swap chain's back buffer before calling ResizeBuffers.
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	const D3D_FEATURE_LEVEL featureLevelArray[7] = {
		// Modify for supported Direct3D feature levels
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_3,
		D3D_FEATURE_LEVEL_9_2,
		D3D_FEATURE_LEVEL_9_1, };

	UINT createDeviceFlags = 0u;
#ifndef NDEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	// Create device and front/back buffers, and swap chain and rendering context
	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		nullptr,                       // Default adapter
		D3D_DRIVER_TYPE_HARDWARE,      // Hardware acceleration
		nullptr,                       // No software rasterizer
		createDeviceFlags,             // No flags or debug
		featureLevelArray,             // Feature levels
		7,                             // Number of feature levels
		D3D11_SDK_VERSION,             // SDK version
		&swapChainDesc,                // Swap chain description
		cpSwapChain.GetAddressOf(),    // Swap chain
		cpDevice.GetAddressOf(),       // Device
		nullptr,                       // Feature level
		cpDeviceContext.GetAddressOf() // Device context
	);

	GFX_THROW_INFO(hr);

} // CreateDeviceAndSwapChain

void Graphics::CreateMainRenderTarget() {
	cpBackBufferSceneTexture.Reset();
	cpBackBufferRTV.Reset();

	HRESULT hr = cpSwapChain->GetBuffer(
		0,
		IID_PPV_ARGS(cpBackBufferSceneTexture.GetAddressOf())
	);

	GFX_THROW_INFO(hr);

	hr = cpDevice->CreateRenderTargetView(
		cpBackBufferSceneTexture.Get(),
		nullptr,
		cpBackBufferRTV.GetAddressOf()
	);

	GFX_THROW_INFO(hr);
} // CreateMainRenderTarget

void Graphics::CreateDepthBuffer(){
	cpDepthBuffer.Reset();
	cpDepthStencilView.Reset();

	D3D11_TEXTURE2D_DESC depthDesc = {};
	depthDesc.Width = SceneWidth;
	depthDesc.Height = SceneHeight;
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Usage = D3D11_USAGE_DEFAULT;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	HRESULT hr = cpDevice->CreateTexture2D(
		&depthDesc,
		nullptr,
		cpDepthBuffer.GetAddressOf()
	);

	GFX_THROW_INFO(hr);

	hr = cpDevice->CreateDepthStencilView(
		cpDepthBuffer.Get(),
		nullptr,
		cpDepthStencilView.GetAddressOf()
	);

	GFX_THROW_INFO(hr);
} // CreateDepthBuffer

void Graphics::CreateSceneTexture() {
	m_sceneTexture.Reset();
	SceneRTV.Reset();
	SceneSRV.Reset(); // m_sceneTextureSRV

	D3D11_TEXTURE2D_DESC sceneDesc = {};
	sceneDesc.Width = SceneWidth;
	sceneDesc.Height = SceneHeight;
	sceneDesc.MipLevels = 1;
	sceneDesc.ArraySize = 1;
	sceneDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sceneDesc.SampleDesc.Count = 1;
	sceneDesc.Usage = D3D11_USAGE_DEFAULT;
	sceneDesc.BindFlags =
		D3D11_BIND_RENDER_TARGET |
		D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = cpDevice->CreateTexture2D(
		&sceneDesc,
		nullptr,
		m_sceneTexture.GetAddressOf()
	);

	GFX_THROW_INFO(hr);

	hr = cpDevice->CreateRenderTargetView(
		m_sceneTexture.Get(),
		nullptr,
		SceneRTV.GetAddressOf()
	);

	GFX_THROW_INFO(hr);

	hr = cpDevice->CreateShaderResourceView(
		m_sceneTexture.Get(),
		nullptr,
		SceneSRV.GetAddressOf()
	);

	GFX_THROW_INFO(hr);
} // CreateSceneTexture()

void Graphics::ReleaseMainRenderTarget(){
	// Remove the swap-chain render target from all output-merger slots.
	cpDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

	// Clear SRV/UAV/RTV/DSV bindings that could retain resources.
	cpDeviceContext->ClearState();

	// Ensure queued commands are submitted before releasing resources.
	// cpDeviceContext->Flush();

	// The RTV and texture are separate COM objects referencing the same buffer.
	cpBackBufferRTV.Reset();
	cpBackBufferSceneTexture.Reset();
} // ReleaseMainRenderTarget

void Graphics::ReleaseDepthBuffer() {
	cpDepthStencilView.Reset();
	cpDepthBuffer.Reset();
} // ReleaseDepthBuffer

void Graphics::ReleaseSceneTexture() {
	// The scene texture may be bound as an RTV or SRV.
	cpDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	ID3D11ShaderResourceView* nullSRV = nullptr;
	cpDeviceContext->PSSetShaderResources(0, 1, &nullSRV); // Unbind slot 0

	SceneSRV.Reset();
	SceneRTV.Reset();
	m_sceneTexture.Reset();
} // ReleaseSceneTexture

void Graphics::Resize(UINT width, UINT height, bool resizeScene)
{
	if (!cpSwapChain || !cpDevice || !cpDeviceContext)
		return;

	if (width == 0 || height == 0)
		return;

	const bool outputChanged =
		width != static_cast<UINT>(mOutputWidth) ||
		height != static_cast<UINT>(mOutputHeight);

	const bool sceneChanged =
		resizeScene &&
		(width != SceneWidth || height != SceneHeight);

	if (!outputChanged && !sceneChanged)
		return;

	if (outputChanged)
	{

#if RESIZE_DIAG_ENABLED
		const auto mainResizeStart = std::chrono::steady_clock::now();
#endif

		ReleaseMainRenderTarget();

#if RESIZE_DIAG_ENABLED
		const uint64_t resizeSession = ResizeDiag::ActiveMeasurementSession();
		const auto resizeStart = std::chrono::steady_clock::now();
#endif

		const HRESULT hr = cpSwapChain->ResizeBuffers(
			0,
			width,
			height,
			DXGI_FORMAT_UNKNOWN,
			0
		);

#if RESIZE_DIAG_ENABLED
		const auto resizeDurationUs =
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - resizeStart
			).count();

		ResizeDiag::resizeBuffersCount.fetch_add(
			1,
			std::memory_order_relaxed
		);

		ResizeDiag::RecordResizeBuffersTiming(
			resizeSession,
			SUCCEEDED(hr),
			static_cast<unsigned long long>(resizeDurationUs)
		);

		ResizeDiag::Log(
			"ResizeBuffers duration=%llu us success=%d size=%ux%u",
			static_cast<unsigned long long>(resizeDurationUs),
			SUCCEEDED(hr) ? 1 : 0,
			width,
			height);
#endif

		if (FAILED(hr))
		{
#if RESIZE_DIAG_ENABLED
			ResizeDiag::Log(
				"ResizeBuffers FAILED hr=0x%08X requested=%ux%u; restoring old target",
				static_cast<unsigned int>(hr),
				width,
				height);
#endif

			// ResizeBuffers failed: keep the old dimensions and reacquire the
			// still-valid swap-chain back buffer.
			CreateMainRenderTarget();

#if RESIZE_DIAG_ENABLED
			m_lastMainResizeDurationUs =
				static_cast<unsigned long long>(
					std::chrono::duration_cast<std::chrono::microseconds>(
						std::chrono::steady_clock::now() - mainResizeStart
					).count()
					);
#endif

			return;
		}

		mOutputWidth = static_cast<int>(width);
		mOutputHeight = static_cast<int>(height);
		CreateMainRenderTarget();

#if RESIZE_DIAG_ENABLED
		m_lastMainResizeDurationUs =
			static_cast<unsigned long long>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - mainResizeStart
				).count()
				);
#endif

#if RESIZE_DIAG_ENABLED
		D3D11_TEXTURE2D_DESC backBufferDesc{};
		cpBackBufferSceneTexture->GetDesc(&backBufferDesc);
		ResizeDiag::Log(
			"BackBuffer actual=%ux%u",
			backBufferDesc.Width,
			backBufferDesc.Height);
#endif
	}

	if (sceneChanged)
	{

#if RESIZE_DIAG_ENABLED
		const auto sceneResizeStart = std::chrono::steady_clock::now();
		m_lastSceneResizeCalled = true;
#endif

#if RESIZE_DIAG_ENABLED
		ResizeDiag::Log(
			"SceneTargets resize old=%ux%u new=%ux%u",
			SceneWidth,
			SceneHeight,
			width,
			height);
#endif

		ReleaseDepthBuffer();
		ReleaseSceneTexture();

		SceneWidth = width;
		SceneHeight = height;

		CreateDepthBuffer();
		CreateSceneTexture();

#if RESIZE_DIAG_ENABLED
		D3D11_TEXTURE2D_DESC sceneDesc{};
		m_sceneTexture->GetDesc(&sceneDesc);

		D3D11_TEXTURE2D_DESC depthDesc{};
		cpDepthBuffer->GetDesc(&depthDesc);

		ResizeDiag::Log(
			"SceneTargets ready scene=%ux%u depth=%ux%u",
			sceneDesc.Width,
			sceneDesc.Height,
			depthDesc.Width,
			depthDesc.Height);
#endif

		m_viewport.TopLeftX = 0.0f;
		m_viewport.TopLeftY = 0.0f;
		m_viewport.Width = static_cast<float>(SceneWidth);
		m_viewport.Height = static_cast<float>(SceneHeight);
		m_viewport.MinDepth = 0.0f;
		m_viewport.MaxDepth = 1.0f;

#if RESIZE_DIAG_ENABLED
		m_lastSceneResizeDurationUs =
			static_cast<unsigned long long>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - sceneResizeStart
				).count()
				);
#endif

	}
}

void Graphics::RenderFrameBegin(const ImVec4& clearColor)
{
	// Render the application scene into the off-screen texture.
	cpDeviceContext->OMSetRenderTargets(
		1,
		SceneRTV.GetAddressOf(),
		cpDepthStencilView.Get()
	);

	cpDeviceContext->RSSetViewports(1, &m_viewport);

	// Clear the off-screen scene texture.
	const float sceneClearColor[] =
	{
		clearColor.x,
		clearColor.y,
		clearColor.z,
		clearColor.w
	};

	cpDeviceContext->ClearRenderTargetView(
		SceneRTV.Get(),
		sceneClearColor
	);

	cpDeviceContext->ClearDepthStencilView(
		cpDepthStencilView.Get(),
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
		1.0f,
		0
	);
}

void Graphics::RenderFrameEnd()
{

	// Now render ImGui to the swap-chain back buffer RTV
	cpDeviceContext->OMSetRenderTargets(
		1,
		cpBackBufferRTV.GetAddressOf(),
		nullptr
	);

	const float backBufferClearColor[] =
	{
		0.08f,
		0.08f,
		0.10f,
		1.0f
	};

	cpDeviceContext->ClearRenderTargetView(
		cpBackBufferRTV.Get(),
		backBufferClearColor
	);

#if RESIZE_DIAG_ENABLED
	using Clock = std::chrono::steady_clock;

	const auto mainDrawStart = Clock::now();
#endif

	ImGui::Render();

	ImGui_ImplDX11_RenderDrawData(
		ImGui::GetDrawData()
	);

#if RESIZE_DIAG_ENABLED
	m_lastMainDrawDurationUs =
		static_cast<unsigned long long>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				Clock::now() - mainDrawStart
			).count()
			);

	m_lastPlatformWindowsDurationUs = 0;
#endif

	// Required for Dear ImGui multi-viewport support.
	ImGuiIO& io = ImGui::GetIO();

	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
#if RESIZE_DIAG_ENABLED
		const auto platformWindowsStart = Clock::now();
#endif

		ImGui::UpdatePlatformWindows();
		
		EnsureSecondaryViewportsAreResizable();
		SetSecondaryViewportFramebufferScales();
		LogViewportScaleDiagnostic();
				
		ImGui::RenderPlatformWindowsDefault();

		static std::unordered_map<ImGuiID, RECT> lastRects;

		const auto& viewports = ImGui::GetPlatformIO().Viewports;
		for (int i = 1; i < viewports.Size; ++i)
		{
			ImGuiViewport* viewport = viewports[i];
			if (!viewport || !viewport->PlatformHandle)
				continue;

			const HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);

			RECT outer{};
			RECT client{};
			if (!GetWindowRect(hwnd, &outer) ||
				!GetClientRect(hwnd, &client))
			{
				continue;
			}

			const auto previous = lastRects.find(viewport->ID);
			const bool rectChanged =
				previous == lastRects.end() ||
				previous->second.left != outer.left ||
				previous->second.top != outer.top ||
				previous->second.right != outer.right ||
				previous->second.bottom != outer.bottom;

			if (rectChanged)
			{
				lastRects[viewport->ID] = outer;

				ImGuiViewport* mainViewport = ImGui::GetMainViewport();
				const HWND mainHwnd =
					(mainViewport && mainViewport->PlatformHandle)
					? static_cast<HWND>(mainViewport->PlatformHandle)
					: nullptr;

				const float mainHwndDpiScale =
					mainHwnd ? DpiScale(mainHwnd) : 0.0f;
				const float mainViewportDpi =
					mainViewport ? mainViewport->DpiScale : 0.0f;

				ResizeDiag::Log(
					"Platform viewport=%u logicalSize=%.1fx%.1f "
					"mainHwndDpiScale=%.2f mainViewportDpi=%.2f "
					"floatingHwndDpiScale=%.2f floatingViewportDpi=%.2f "
					"scaleViewports=%d "
					"outer=(%ld,%ld %ldx%ld) client=%ldx%ld",
					static_cast<unsigned int>(viewport->ID),
					viewport->Size.x, viewport->Size.y,
					mainHwndDpiScale,
					mainViewportDpi,
					DpiScale(hwnd),
					viewport->DpiScale,
					io.ConfigDpiScaleViewports ? 1 : 0,
					outer.left, outer.top,
					outer.right - outer.left,
					outer.bottom - outer.top,
					client.right - client.left,
					client.bottom - client.top);

				// Separate call: these format specifiers match these arguments only.
				ResizeDiag::Log(
					"DPI scales: mainHwnd=%.2f mainViewport=%.2f "
					"floatingHwnd=%.2f floatingViewport=%.2f configScaleViewports=%d",
					mainHwnd ? DpiScale(mainHwnd) : 0.0f,
					mainViewport ? mainViewport->DpiScale : 0.0f,
					DpiScale(hwnd),
					viewport->DpiScale,
					static_cast<int>(io.ConfigDpiScaleViewports));
			}
		}

		// Dear ImGui's secondary viewport rendering can change the current D3D11 render state.
		// Restore the main target for safety.
		cpDeviceContext->OMSetRenderTargets(
			1,
			cpBackBufferRTV.GetAddressOf(),
			nullptr
		);

#if RESIZE_DIAG_ENABLED
		m_lastPlatformWindowsDurationUs =
			static_cast<unsigned long long>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					Clock::now() - platformWindowsStart
				).count()
				);
#endif

	}
}

void Graphics::Present(bool vsync)
{
	const auto start = std::chrono::steady_clock::now();

	const HRESULT hr = cpSwapChain->Present(
		vsync ? 1 : 0,
		0
	);

	m_lastPresentDurationUs =
		static_cast<unsigned long long>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - start
			).count()
			);

#if RESIZE_DIAG_ENABLED
	ResizeDiag::presentCount.fetch_add(
		1,
		std::memory_order_relaxed
	);

	if (FAILED(hr))
	{
		ResizeDiag::presentFailureCount.fetch_add(
			1,
			std::memory_order_relaxed
		);
	}
#endif

}

void Graphics::Shutdown() {
	if (!cpDeviceContext)
		return;

	// Release all application-owned resources before the device.
	ReleaseMainRenderTarget();
	ReleaseDepthBuffer();
	ReleaseSceneTexture();

	cpDeviceContext->ClearState();
	cpDeviceContext->Flush();

#if defined(_DEBUG)
	Microsoft::WRL::ComPtr<ID3D11Debug> debugDevice;

	if (SUCCEEDED(cpDeviceContext.As(&debugDevice)))
	{
		debugDevice->ReportLiveDeviceObjects(
			D3D11_RLDO_DETAIL
		);
	}
#endif

	cpSwapChain.Reset();
	cpDeviceContext.Reset();
	cpDevice.Reset();

	hWindow = nullptr;

	// Release query objects during shutdown
	for (auto& slot : m_gpuTimingSlots)
	{
		slot.disjoint.Reset();
		slot.start.Reset();
		slot.end.Reset();
		slot.pending = false;
	}
	m_gpuTimingAvailable = false;
	m_activeGpuTimingSlot = -1;

} // Shutdown

void Graphics::Begin() {

#if RESIZE_DIAG_ENABLED
	m_lastMainResizeDurationUs = 0;
	m_lastSceneResizeDurationUs = 0;
	m_lastSceneResizeCalled = false;
#endif

	// Apply a pending resize outside the window procedure.
	if (!g_minimized && g_pendingWidth > 0 && g_pendingHeight > 0)
	{
		// In this code path, resizeScene == false means a live interactive resize.
		// The final request has resizeScene == true and must not wait.
		const bool interactiveResize = !g_pendingSceneResize;
		const auto now = std::chrono::steady_clock::now();
		constexpr auto throttleInterval = std::chrono::milliseconds(50);

		const bool throttleElapsed =
			!m_hasInteractiveResizeApplied ||
			now - m_lastInteractiveResizeApply >= throttleInterval;

		if (!interactiveResize || throttleElapsed)
		{
			const UINT width = g_pendingWidth;
			const UINT height = g_pendingHeight;
			const bool resizeScene = g_pendingSceneResize;

			g_pendingWidth = 0;
			g_pendingHeight = 0;
			g_pendingSceneResize = false;

			Resize(width, height, resizeScene);
			
			BeginGpuTiming();

			if (interactiveResize)
			{
				m_lastInteractiveResizeApply = now;
				m_hasInteractiveResizeApplied = true;
			}
			else
			{
				// The final resize is applied immediately; allow the next interactive resize session
				// to apply its first size immediately.
				m_hasInteractiveResizeApplied = false;
			}
		}
	}

	// Start a new ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	const float dpiScale = DpiScale(hWindow);
	ImGui::GetIO().DisplayFramebufferScale = ImVec2(dpiScale, dpiScale);

	ImGui::NewFrame();

	RECT rect;
	GetClientRect(hWindow, &rect);
	ImVec2 disSize = ImGui::GetIO().DisplaySize;
	ImVec2 disFrambuffScale = ImGui::GetIO().DisplayFramebufferScale;
	float dsx = disSize.x;
	float fbsx = disFrambuffScale.x;
	float dsy = disSize.y;
	float fbsy = disFrambuffScale.y;
	// OutputDebugString((std::to_string(dsx) + " - " + std::to_string(dsy) + "\n").c_str());
	// OutputDebugString((std::to_string(fbsx) + " - " + std::to_string(fbsy) + "\n").c_str());

	DrawCustomTitleAndMenuAboveDockSpace();

	ShowScene();

	// Build the scene and render it.
	RenderFrameBegin(clearColor);
}

void Graphics::End() {	
	RenderFrameEnd();
	EndGpuTiming();
	Present(true);
}

void Graphics::ShowScene()
{
	ImGui::Begin("Scene");
	ImVec2 availableSize = ImGui::GetContentRegionAvail();
	if (SceneSRV.Get() && SceneWidth > 0 && SceneHeight > 0 &&
		availableSize.x > 0.0f && availableSize.y > 0.0f)
	{
		const float width = static_cast<float>(SceneWidth);
		const float height = static_cast<float>(SceneHeight);

		const float fitScale = std::min(
			availableSize.x / width,
			availableSize.y / height);

		// Want the scene to fill the panel instead, cap—the upscaling is then expected.
		const float scale = std::min(fitScale, 1.0f); // Don't upscale
		const ImVec2 imageSize(width * scale, height * scale);

		const ImVec2 cursorPos = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(
			cursorPos.x + (availableSize.x - imageSize.x) * 0.5f,
			cursorPos.y + (availableSize.y - imageSize.y) * 0.5f));

		const ImTextureID textureId =
			(ImTextureID)(std::intptr_t)SceneSRV.Get();

		ImGui::Image(textureId, imageSize);
	}

	// Display scene size as an overlay in the bottom-right corner
	{
		ImVec2 windowPos = ImGui::GetWindowPos();
		ImVec2 windowSize = ImGui::GetWindowSize();
		ImVec2 padding(8.0f, 6.0f);

		char label[64];
		snprintf(label, sizeof(label), "%.0f x %.0f", windowSize.x, windowSize.y);

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		ImVec2 textPos(
			windowPos.x + windowSize.x - ImGui::CalcTextSize(label).x - padding.x,
			windowPos.y + windowSize.y - ImGui::GetFontSize() - padding.y
		);

		drawList->AddRectFilled(
			ImVec2(textPos.x - padding.x, textPos.y - padding.y),
			ImVec2(textPos.x + ImGui::CalcTextSize(label).x + padding.x,
				textPos.y + ImGui::GetFontSize() + padding.y),
			IM_COL32(0, 100, 0, 180)
		);

		drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), label);
	}

	ImGui::End();
}

void Graphics::DrawCustomTitleAndMenuAboveDockSpace()
{
	// const float dpiScale = DpiScale(hWindow);
	// OutputDebugString((std::to_string(dpiScale) + "\n").c_str());

	ImGuiViewport* mainViewport = ImGui::GetMainViewport();

	ImGui::SetNextWindowPos(mainViewport->WorkPos);
	ImGui::SetNextWindowSize(mainViewport->WorkSize);
	ImGui::SetNextWindowViewport(mainViewport->ID);

	ImGuiWindowFlags host_flags =
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // No padding

	ImGui::Begin("MainWindow", nullptr, host_flags);

	// -------------------------------------------------
	// 1. Custom title bar
	// -------------------------------------------------
	ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4(1.0f/255.0f, 50.0f/255.0f, 32.0f/255.0f, 1.0f));

	ImGui::BeginChild(
		"TitleBar",
		ImVec2(0.0f, 32.0f /** dpiScale*/), // Title bar height
		false,
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse
	);

	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
	// Title text position
	ImGui::SetCursorPos(ImVec2(12.0f /** dpiScale*/, 7.0f /** dpiScale*/));
	ImGui::TextUnformatted("ImGui with Win32 + DX11 backend");
	ImGui::PopStyleColor();

	// Right-side button placement
	ImGui::SameLine(ImGui::GetWindowWidth() - 70.0f /** dpiScale*/);

	if (ImGui::Button("_"))
	{
		// Minimize window through your platform/backend code
		ShowWindow(hWindow, SW_MINIMIZE);
	}

	ImGui::SameLine();

	if (ImGui::Button("X"))
	{
		// Request application close
		PostMessageW(hWindow, WM_CLOSE, 0, 0);
	}

	ImGui::EndChild();

	ImGui::PopStyleColor();

	// -------------------------------------------------
	// 2. Menu bar below the title bar
	// -------------------------------------------------
	ImGui::PushStyleColor(ImGuiCol_ChildBg,	ImVec4(0.0f, 0.66f, 0.33f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f /** dpiScale*/, 4.0f/* * dpiScale*/));	// Menu bar padding

	ImGui::BeginChild(
		"MenuBar",
		ImVec2(0.0f, 24.0f /** dpiScale*/), // Menu bar height
		false,
		ImGuiWindowFlags_MenuBar |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse
	);

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Open"))
			{
			}

			if (ImGui::MenuItem("Save"))
			{
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Exit"))
			{
				// Request application shutdown
				PostMessageW(hWindow, WM_CLOSE, 0, 0);
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			ImGui::MenuItem("Undo");
			ImGui::MenuItem("Redo");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Properties");
			if (ImGui::MenuItem("Console",0, consoleOpen.has_value() && consoleOpen.value())) {
				if (consoleOpen.value())
				{
					ReleaseConsole();
					consoleOpen.emplace(false);
				}
				else {
					CreateNewConsole(1024);
					consoleOpen.emplace(true);
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	ImGui::EndChild();

	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	// -------------------------------------------------
	// 3. Dock space below both
	// -------------------------------------------------
	ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

	ImGui::DockSpace(
		dockspace_id,
		ImVec2(0.0f, 0.0f),
		ImGuiDockNodeFlags_None
	);

	ImGui::End(); // "MainWindow"

	ImGui::PopStyleVar(); // ImGuiStyleVar_WindowPadding
}

void Graphics::DrawIndexed(UINT count) noxnd
{
	GFX_THROW_INFO_ONLY(cpDeviceContext->DrawIndexed(count, 0u, 0u));
}

void Graphics::SetProjection(DirectX::XMMATRIX proj) noexcept
{
	projection = proj;
}

DirectX::XMMATRIX Graphics::GetProjection() const noexcept
{
	return projection;
}

void Graphics::SetCamera(DirectX::XMMATRIX cam) noexcept
{
	camera = cam;
}

DirectX::XMMATRIX Graphics::GetCamera() const noexcept
{
	return camera;
}

void Graphics::EnableImgui() noexcept
{
	imguiEnabled = true;
}

void Graphics::DisableImgui() noexcept
{
	imguiEnabled = false;
}

bool Graphics::IsImguiEnabled() const noexcept
{
	return imguiEnabled;
}

void Graphics::CreateGpuTimingQueries() noexcept
{
	m_gpuTimingAvailable = false;
	m_activeGpuTimingSlot = -1;
	m_nextGpuTimingSlot = 0;

	if (!cpDevice || !cpDeviceContext)
		return;

	D3D11_QUERY_DESC desc{};
	desc.MiscFlags = 0;

	for (auto& slot : m_gpuTimingSlots)
	{
		slot.disjoint.Reset();
		slot.start.Reset();
		slot.end.Reset();
		slot.pending = false;
		slot.session = 0;

		desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		if (FAILED(cpDevice->CreateQuery(
			&desc, slot.disjoint.GetAddressOf())))
		{
			return;
		}

		desc.Query = D3D11_QUERY_TIMESTAMP;
		if (FAILED(cpDevice->CreateQuery(
			&desc, slot.start.GetAddressOf())))
		{
			return;
		}

		if (FAILED(cpDevice->CreateQuery(
			&desc, slot.end.GetAddressOf())))
		{
			return;
		}
	}

	m_gpuTimingAvailable = true;
}

void Graphics::PollGpuTimingQueries() noexcept
{
	if (!m_gpuTimingAvailable || !cpDeviceContext)
		return;

	for (std::size_t i = 0; i < m_gpuTimingSlots.size(); ++i)
	{
		auto& slot = m_gpuTimingSlots[i];
		if (!slot.pending)
			continue;

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
		UINT64 startTimestamp = 0;
		UINT64 endTimestamp = 0;

		const UINT flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;

		const HRESULT hrDisjoint = cpDeviceContext->GetData(
			slot.disjoint.Get(),
			&disjointData,
			sizeof(disjointData),
			flags);

		const HRESULT hrStart = cpDeviceContext->GetData(
			slot.start.Get(),
			&startTimestamp,
			sizeof(startTimestamp),
			flags);

		const HRESULT hrEnd = cpDeviceContext->GetData(
			slot.end.Get(),
			&endTimestamp,
			sizeof(endTimestamp),
			flags);

		// The GPU hasn't finished this slot yet. Leave it pending and
		// try again on a later frame; never block waiting for it.
		if (hrDisjoint == S_FALSE || hrStart == S_FALSE || hrEnd == S_FALSE)
			continue;

		// A failed query won't become useful; free the slot.
		if (FAILED(hrDisjoint) || FAILED(hrStart) || FAILED(hrEnd))
		{
			slot.pending = false;
			continue;
		}

		slot.pending = false;

		if (disjointData.Disjoint ||
			disjointData.Frequency == 0 ||
			endTimestamp < startTimestamp)
		{
			continue;
		}

		const double gpuUs =
			static_cast<double>(endTimestamp - startTimestamp) *
			1000000.0 /
			static_cast<double>(disjointData.Frequency);

		// Temporary diagnostic: avoid logging every frame, since
		// OutputDebugString logging can affect timing.
		if (gpuUs >= 12000.0)
		{
			ResizeDiag::Log(
				"GPU frame time: session=%llu slot=%zu gpu=%.1f us",
				static_cast<unsigned long long>(slot.session),
				i,
				gpuUs);
		}
	}
}

void Graphics::BeginGpuTiming() noexcept
{
	// Poll first so completed slots can be reused.
	PollGpuTimingQueries();

	if (!m_gpuTimingAvailable ||
		!cpDeviceContext ||
		m_activeGpuTimingSlot >= 0)
	{
		return;
	}

	const uint64_t session = ResizeDiag::ActiveMeasurementSession();
	if (session == 0)
		return; // Only collect during an active diagnostic session.

	for (std::size_t offset = 0; offset < m_gpuTimingSlots.size(); ++offset)
	{
		const std::size_t index =
			(m_nextGpuTimingSlot + offset) % m_gpuTimingSlots.size();

		auto& slot = m_gpuTimingSlots[index];
		if (slot.pending)
			continue; // Don't overwrite a result the GPU hasn't completed.

		slot.session = session;

		cpDeviceContext->Begin(slot.disjoint.Get());
		cpDeviceContext->End(slot.start.Get());

		m_activeGpuTimingSlot = static_cast<int>(index);
		m_nextGpuTimingSlot = (index + 1) % m_gpuTimingSlots.size();
		return;
	}

	// All four slots are still pending. Skip this frame's sample.
}

void Graphics::EndGpuTiming() noexcept
{
	if (m_activeGpuTimingSlot < 0 ||
		!cpDeviceContext)
	{
		return;
	}

	auto& slot =
		m_gpuTimingSlots[static_cast<std::size_t>(m_activeGpuTimingSlot)];

	cpDeviceContext->End(slot.end.Get());
	cpDeviceContext->End(slot.disjoint.Get());

	slot.pending = true;
	m_activeGpuTimingSlot = -1;
}

void Graphics::OnWindowSizeChanged(
	int width,
	int height,
	bool resizeScene
)
{
	if (!hWindow)
		return;

	g_pendingWidth = static_cast<UINT>(width);
	g_pendingHeight = static_cast<UINT>(height);
	g_pendingSceneResize = resizeScene;
}

// - - - - Graphics exception stuff - - - -
// ----------------------------------------

Graphics::HrException::HrException(int line, const char* file, HRESULT hr, std::vector<std::string> infoMsgs) noexcept
	:
	Exception(line, file),
	hr(hr)
{
	// 
	// Join all info messages with newlines into single string
	for (const auto& m : infoMsgs)
	{
		info += m;
		info.push_back('\n');
	}
	// Remove final newline if exists
	if (!info.empty())
	{
		info.pop_back();
	}
}

const char* Graphics::HrException::what() const noexcept
{
	std::ostringstream oss;
	oss << GetType() << std::endl
		<< "[Error Code] 0x" << std::hex << std::uppercase << GetErrorCode()
		<< std::dec << " (" << (unsigned long)GetErrorCode() << ")" << std::endl
		<< "[Error String] " << GetErrorString() << std::endl
		<< "[Description] " << GetErrorDescription() << std::endl;
	if (!info.empty())
	{
		oss << "\n[Error Info]\n" << GetErrorInfo() << std::endl << std::endl;
	}
	oss << GetOriginString();
	whatBuffer = oss.str();
	return whatBuffer.c_str();
}

const char* Graphics::HrException::GetType() const noexcept
{
	return "Graphics Exception";
}

HRESULT Graphics::HrException::GetErrorCode() const noexcept
{
	return hr;
}

std::string Graphics::HrException::GetErrorString() const noexcept
{
	return DXGetErrorString(hr);
}

std::string Graphics::HrException::GetErrorDescription() const noexcept
{
	char buf[512];
	DXGetErrorDescription(hr, buf, sizeof(buf));
	return buf;
}

std::string Graphics::HrException::GetErrorInfo() const noexcept
{
	return info;
}

const char* Graphics::DeviceRemovedException::GetType() const noexcept
{
	return "Graphics Exception [Device Removed] (DXGI_ERROR_DEVICE_REMOVED)";
}

Graphics::InfoException::InfoException(int line, const char* file, std::vector<std::string> infoMsgs) noexcept
	:
	Exception(line, file)
{
	// Join all info messages with newlines into single string
	for (const auto& m : infoMsgs)
	{
		info += m;
		info.push_back('\n');
	}
	// Remove final newline if exists
	if (!info.empty())
	{
		info.pop_back();
	}
}

const char* Graphics::InfoException::what() const noexcept
{
	std::ostringstream oss;
	oss << GetType() << std::endl
		<< "\n[Error Info]\n" << GetErrorInfo() << std::endl << std::endl;
	oss << GetOriginString();
	whatBuffer = oss.str();
	return whatBuffer.c_str();
}

const char* Graphics::InfoException::GetType() const noexcept
{
	return "Extendet Graphics Info Exception";
}

std::string Graphics::InfoException::GetErrorInfo() const noexcept
{
	return info;
}
