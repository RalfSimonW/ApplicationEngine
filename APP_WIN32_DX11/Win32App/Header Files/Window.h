#pragma once
#include "Win.h"
#include "ExceptionBase.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "Graphics.h"

#include <functional>
#include <optional>
#include <memory>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

class Window
{
public:
	class Exception : public ExceptionBase
	{
		using ExceptionBase::ExceptionBase;
	public:
		static std::string TranslateErrorCode(HRESULT hr) noexcept;
	};
	class HrException : public Exception
	{
	public:
		HrException(int line, const char* file, HRESULT hr) noexcept;
		const char* what() const noexcept override;
		const char* GetType() const noexcept override;
		HRESULT GetErrorCode() const noexcept;
		std::string GetErrorDescription() const noexcept;
	private:
		HRESULT hr;
	};
	class NoGfxException : public Exception
	{
	public:
		using Exception::Exception;
		const char* GetType() const noexcept override;
	};
	struct InputEvent
	{
		enum class Type
		{
			MousePosition,
			MouseButton,
			MouseWheel,
			Key,
			Character,
			Focus,
			Win32KeyboardMessage
		};

		Type type{};
		float x = 0.0f;
		float y = 0.0f;
		std::uint32_t code = 0;       // Button, virtual key, or character
		std::uint32_t scanCode = 0;   // Keyboard events
		bool down = false;            // Button/key down, or focused
		bool extended = false;        // Extended-key flag
		
		UINT message = 0;
		WPARAM wParam = 0;
		LPARAM lParam = 0;
	};

	void QueueInputEvent(const InputEvent& event);
	std::vector<InputEvent> DrainInputEvents();

private:
	// Singleton manages registration/cleanup of window class
	class WindowClass
	{
	public:
		static const char* GetName() noexcept;
		static HINSTANCE GetInstance() noexcept;
	private:
		WindowClass() noexcept;
		~WindowClass();
		WindowClass(const WindowClass&) = delete;
		WindowClass& operator=(const WindowClass&) = delete;
		static constexpr const char* wndClassName = "Direct3D Engine Window";
		static WindowClass wndClass;
		HINSTANCE hInst;
	};
public:
	Window(int width, int height, const char* name);
	~Window();
	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	void InitializeGraphics();
	void RequestResize(UINT width, UINT height) noexcept;
	void ApplyPendingResize();
	void PublishImGuiCaptureState();
	void ApplyQueuedInputToImGui();

	void SetTitle(const std::string& title);

	void EnableCursor() noexcept;
	void DisableCursor() noexcept;
	bool CursorEnabled() const noexcept;

	using DoFrameCallbackFn = std::function<void()>;
	void SetDoFrameCallback(const DoFrameCallbackFn& doFrameCallback);

	static std::optional<int> ProcessMessages() noexcept;
	Graphics& Gfx();
private:
	void ConfineCursor() noexcept;
	void FreeCursor() noexcept;

	void HideCursor() noexcept;
	void ShowCursor() noexcept;

	void EnableImGuiMouse() noexcept;
	void DisableImGuiMouse() noexcept;

	// Manual: duck.ai_2026-09-22_05-27-48.txt
	void BeginWindowDrag();
	void UpdateWindowDrag();
	void EndWindowDrag();

	static LRESULT CALLBACK HandleMsgSetup(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
	static LRESULT CALLBACK HandleMsgThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
	LRESULT HandleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
public:
	Keyboard kbd;
	Mouse mouse;
private:
	bool cursorEnabled = true;

	int width;
	int height;
	HWND hWnd;
	std::unique_ptr<Graphics> pGfx;
	std::vector<BYTE> rawBuffer;

	bool inSizemove = false;

	// Manual: duck.ai_2026-09-22_05-27-48.txt
	bool gDragging = false;
	POINT gDragStartCursor{};
	RECT gDragStartWindow{};

	static constexpr int TITLE_BAR_HEIGHT = 34;

	DoFrameCallbackFn DoFrameCallback;

	std::atomic<std::uint64_t> pendingResize{ 0 };
	std::atomic<bool> imguiWantsMouse{ false };
	std::atomic<bool> imguiWantsKeyboard{ false };
	std::atomic<bool> imguiItemHovered{ false };

	std::mutex inputMutex;
	std::deque<InputEvent> pendingInput;

	bool titleBarDragActive = false;

};
