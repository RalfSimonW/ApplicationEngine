#include "dxerr.h"
#include "Graphics.h"
#include "GraphicsThrowMacros.h"
#include "RedirectConsole.h"


#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <sstream>
#include <d3dcompiler.h>
#include <cmath>
#include <DirectXMath.h>

#include <utility>

namespace wrl = Microsoft::WRL;
namespace dx = DirectX;

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

	// Avoid creating zero-sized buffers.
	mOutputWidth = std::max(width, 1);
	mOutputHeight = std::max(height, 1);

	// Create device and swap chain before init ImGui
	CreateDeviceAndSwapChain(hWindow, width, height);

	CreateMainRenderTarget();
	
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
	depthDesc.Width = mOutputWidth;
	depthDesc.Height = mOutputHeight;
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
	sceneDesc.Width = mOutputWidth;
	sceneDesc.Height = mOutputHeight;
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
	cpDeviceContext->Flush();

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

void Graphics::Resize(UINT width, UINT height) {
	if (!cpSwapChain || !cpDevice || !cpDeviceContext)
		return;

	if (width == 0 || height == 0)
		return;

	if (width == mOutputWidth && height == mOutputHeight)
		return;

	mOutputWidth = width;
	mOutputHeight = height;

	// Release every resource created with the old dimensions.
	ReleaseMainRenderTarget();
	ReleaseDepthBuffer();
	ReleaseSceneTexture();

	// ResizeBuffers requires all references to the old swap-chain buffers
	// to have been released.
	HRESULT hr = cpSwapChain->ResizeBuffers(
		0,
		mOutputWidth,
		mOutputHeight,
		DXGI_FORMAT_UNKNOWN,
		0
	);

	if (FAILED(hr))
		return;

	CreateMainRenderTarget();
	CreateDepthBuffer();
	CreateSceneTexture();

	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;
	m_viewport.Width = static_cast<float>(mOutputWidth);
	m_viewport.Height = static_cast<float>(mOutputHeight);
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
} // Resize

void Graphics::BeginFrame() {
	// Render the application scene into the off-screen texture.
	cpDeviceContext->OMSetRenderTargets(
		1,
		SceneRTV.GetAddressOf(),
		cpDepthStencilView.Get()
	);

	cpDeviceContext->RSSetViewports(1, &m_viewport);
} // BeginFrame

void Graphics::RenderFrame(const ImVec4& clearColor) {
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

	// Now render ImGui to the swap-chain back buffer.
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

	ImGui::Render();

	ImGui_ImplDX11_RenderDrawData(
		ImGui::GetDrawData()
	);

	// Required for Dear ImGui multi-viewport support.
	ImGuiIO& io = ImGui::GetIO();

	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();

		// Dear ImGui's secondary viewport rendering can change the current
		// D3D11 render state. Restore the main target for safety.
		cpDeviceContext->OMSetRenderTargets(
			1,
			cpBackBufferRTV.GetAddressOf(),
			nullptr
		);
	}
} // RenderFrame

void Graphics::RenderFrameBegin(const ImVec4& clearColor)
{
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

	ImGui::Render();

	ImGui_ImplDX11_RenderDrawData(
		ImGui::GetDrawData()
	);

	// Required for Dear ImGui multi-viewport support.
	ImGuiIO& io = ImGui::GetIO();

	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();

		// Dear ImGui's secondary viewport rendering can change the current
		// D3D11 render state. Restore the main target for safety.
		cpDeviceContext->OMSetRenderTargets(
			1,
			cpBackBufferRTV.GetAddressOf(),
			nullptr
		);
	}
}

void Graphics::Present(bool vsync) {
	cpSwapChain->Present(
		vsync ? 1 : 0,
		0
	);
} // Present

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
} // Shutdown

void Graphics::Begin() {
	// Apply a pending resize outside the window procedure
	if (!g_minimized &&	g_pendingWidth > 0 && g_pendingHeight > 0)
	{
		// Copy
		UINT width = g_pendingWidth;
		UINT height = g_pendingHeight;

		g_pendingWidth = 0;
		g_pendingHeight = 0;

		Resize(width, height);
	}

	// Start a new ImGui frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	DrawCustomTitleAndMenuAboveDockSpace();

	ShowScene();

	// Build the scene and render it.
	BeginFrame();
	RenderFrameBegin(clearColor);
}

void Graphics::End() {	
	RenderFrameEnd();
	Present(true);
}

void Graphics::ShowScene()
{
	ImGui::Begin("Scene");
	ImVec2 availableSize = ImGui::GetContentRegionAvail();
	if (SceneSRV.Get())
	{
		ImGui::Image((ImTextureID)SceneSRV.Get(), availableSize);
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
	const float dpiScale = DpiScale(hWindow);
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
		ImVec2(0.0f, 32.0f * dpiScale), // Title bar height
		false,
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse
	);

	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
	// Title text position
	ImGui::SetCursorPos(ImVec2(12.0f * dpiScale, 7.0f * dpiScale));
	ImGui::TextUnformatted("ImGui with Win32 + DX11 backend");
	ImGui::PopStyleColor();

	// Right-side button placement
	ImGui::SameLine(ImGui::GetWindowWidth() - 70.0f * dpiScale);

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
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * dpiScale, 4.0f * dpiScale));	// Menu bar padding

	ImGui::BeginChild(
		"MenuBar",
		ImVec2(0.0f, 24.0f * dpiScale), // Menu bar height
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

void Graphics::OnWindowSizeChanged(int width, int height)
{
	if (!hWindow)
		return;
	
	g_pendingWidth = width;
	g_pendingHeight = height;	
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
