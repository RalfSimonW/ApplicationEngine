#pragma once
#include "Win.h"
#include "ExceptionBase.h"
#include <d3d11.h>
#include <wrl.h> // Windows Runtime C++ Template Library (WRL)
#include <vector>
#include "DxgiInfoManager.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <memory>
#include <optional>
#include <random>
#include <chrono>
#include "ConditionalNoexcept.h"

#include "imgui.h"

namespace Bind
{
	class Bindable;
}

class Graphics
{
	friend class Bindable;
	friend class Bind::Bindable;

public:
	// ----
	class Exception : public ExceptionBase
	{
		using ExceptionBase::ExceptionBase;
	};
	// ----
	class HrException : public Exception
	{
	public:
		HrException(int line, const char* file, HRESULT hr, std::vector<std::string> infoMsgs = {}) noexcept;
		const char* what() const noexcept override;
		const char* GetType() const noexcept override;
		HRESULT GetErrorCode() const noexcept; // Guaranteed not to throw exceptions
		std::string GetErrorString() const noexcept;
		std::string GetErrorDescription() const noexcept;
		std::string GetErrorInfo() const noexcept;
	private:
		HRESULT hr;
		std::string info;
	};
	// ----
	class InfoException : public Exception
	{
	public:
		InfoException(int line, const char* file, std::vector<std::string> infoMsgs) noexcept;
		const char* what() const noexcept override;
		const char* GetType() const noexcept override;
		std::string GetErrorInfo() const noexcept;
	private:
		std::string info;
	};
	// ----
	class DeviceRemovedException : public HrException
	{
		using HrException::HrException;
	public:
		const char* GetType() const noexcept override;
	private:
		std::string reason;
	};
public:
	Graphics(HWND hWnd, int width, int height);
	Graphics(const Graphics&) = delete;
	Graphics& operator=(const Graphics&) = delete;
	~Graphics();


	void Initialize(HWND hWnd, int width, int height);
	void CreateDeviceAndSwapChain(HWND hWnd, int width, int height); // Device and swap chain
	void CreateMainRenderTarget();
	void CreateDepthBuffer();
	void CreateSceneTexture();
	void ReleaseMainRenderTarget();
	void ReleaseDepthBuffer();
	void ReleaseSceneTexture();
	void Resize(UINT width, UINT height, bool resizeScene);

	void RenderFrameBegin(const ImVec4& clearColor);
	void RenderFrameEnd();

	void Present(bool vsync);
	void Shutdown();
	
	void Begin();
	void End();

	void ShowScene();

	void OnWindowSizeChanged(int width, int height, bool resizeScene);

	void DrawCustomTitleAndMenuAboveDockSpace();

	void DrawIndexed(UINT count) noxnd;
	void SetProjection(DirectX::XMMATRIX proj) noexcept;
	DirectX::XMMATRIX GetProjection() const noexcept;
	void SetCamera(DirectX::XMMATRIX cam) noexcept;
	DirectX::XMMATRIX GetCamera() const noexcept;
	void EnableImgui() noexcept;
	void DisableImgui() noexcept;
	bool IsImguiEnabled() const noexcept;
private:
	DirectX::XMMATRIX projection;
	DirectX::XMMATRIX camera;
	bool imguiEnabled = true;

#ifndef NDEBUG
	DxgiInfoManager infoManager;
#endif
	
	// 
	Microsoft::WRL::ComPtr<ID3D11Device> cpDevice;	
	//
	Microsoft::WRL::ComPtr<IDXGISwapChain> cpSwapChain;	
	//
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> cpDeviceContext;
	// Swap-chain back buffer.
	Microsoft::WRL::ComPtr<ID3D11Texture2D> cpBackBufferSceneTexture;
	// Render Target View (RTV) enables rendering scene
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> cpBackBufferRTV; // m_backBufferRTV

	// Main depth buffer.
	Microsoft::WRL::ComPtr<ID3D11Texture2D> cpDepthBuffer; // m_depthBuffer
	// Depth Stencil View (DSV) enables a texture to hold depth and stencil information
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> cpDepthStencilView; // m_depthDSV

	// Off-screen scene render target.
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sceneTexture;

	// Shader Resource View (SRV) wraps a texture in a format that the shaders can read
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cpSRV;

	// Device resources
	HWND hWindow = nullptr;
	int  mOutputWidth = 0;  // m_width
	int  mOutputHeight = 0; // m_height
	
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> SceneRTV;   // m_sceneRTV
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> SceneSRV; // m_sceneTextureSRV

	ImVec4 clearColor = ImVec4(0.10f, 0.12f, 0.15f, 1.0f);

	D3D11_VIEWPORT m_viewport = {};
	UINT g_pendingWidth = 0;
	UINT g_pendingHeight = 0;
	bool g_pendingSceneResize = false;
	bool g_minimized = false;

	UINT SceneWidth = 0;
	UINT SceneHeight = 0;

	////----RECT gMaximizeButtonRect{0,0,50,50};

	std::optional<bool> consoleOpen = false;

	std::chrono::steady_clock::time_point m_lastInteractiveResizeApply{};
	bool m_hasInteractiveResizeApplied = false;

};