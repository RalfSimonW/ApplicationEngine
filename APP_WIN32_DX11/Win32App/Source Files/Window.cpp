#include "Window.h"
////#include "RedirectConsole.h"
#include "WindowsMessageMap.h"
#include "spdlog.h"
/* #include "resource.h" */
#include "WindowsThrowMacros.h"
#include "imgui_impl_win32.h"

#include <iostream>
#include <vector>

// Window class stuff
Window::WindowClass Window::WindowClass::wndClass;

// Message display stuff
static WindowsMessageMap winMessageMap;

// Forward declare Win32 message handler located in imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandlerEx(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, ImGuiIO& io);

Window::WindowClass::WindowClass() noexcept
	:
	hInst(GetModuleHandle(nullptr))
{
	ImGui_ImplWin32_EnableDpiAwareness();

	WNDCLASSEX wc = { 0 };
	wc.cbSize = sizeof(wc);
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = HandleMsgSetup;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = GetInstance();
	wc.hIcon = static_cast<HICON>(LoadImage(
		GetInstance(), 0 /* MAKEINTRESOURCE(IDI_ICON1) */,
		IMAGE_ICON, 32, 32, 0
	));
	wc.hCursor = nullptr;
	wc.hbrBackground = nullptr;
	wc.lpszMenuName = nullptr;
	wc.lpszClassName = GetName();
	wc.hIconSm = static_cast<HICON>(LoadImage(
		GetInstance(), 0 /* MAKEINTRESOURCE(IDI_ICON1) */,
		IMAGE_ICON, 16, 16, 0
	));
	RegisterClassEx(&wc);
}

Window::WindowClass::~WindowClass()
{
	UnregisterClass(wndClassName, GetInstance());
}

const char* Window::WindowClass::GetName() noexcept
{
	return wndClassName;
}

HINSTANCE Window::WindowClass::GetInstance() noexcept
{
	return wndClass.hInst;
}


void Window::QueueInputEvent(const InputEvent& event)
{
	std::lock_guard<std::mutex> lock(inputMutex);
	pendingInput.push_back(event);
}

std::vector<Window::InputEvent> Window::DrainInputEvents()
{
	std::vector<InputEvent> events;

	{
		std::lock_guard<std::mutex> lock(inputMutex);
		events.reserve(pendingInput.size());

		while (!pendingInput.empty())
		{
			events.push_back(pendingInput.front());
			pendingInput.pop_front();
		}
	}

	return events;
}

// Window Stuff
Window::Window(int width, int height, const char* name)
	:
	width(width),
	height(height)
{
	// Calculate window size based on desired client region size
	RECT wr;
	wr.left = 0;
	wr.right = width + wr.left;
	wr.top = 0;
	wr.bottom = height + wr.top;
	if (AdjustWindowRect(&wr, WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_VISIBLE /*WS_CAPTION | WS_SYSMENU*/, FALSE) == 0)
	{
		throw WND_LAST_EXCEPT();
	}
	// Create window and get window handle
	hWnd = CreateWindow(
		WindowClass::GetName(), name,
		WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX |	WS_MAXIMIZEBOX | WS_SYSMENU | WS_VISIBLE,
		/*WS_CAPTION | WS_SYSMENU | WS_OVERLAPPEDWINDOW*/
		CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
		nullptr, nullptr, WindowClass::GetInstance(), this
	);
	// check for error
	if (hWnd == nullptr)
	{
		throw WND_LAST_EXCEPT();
	}
	// Newly created windows start off as hidden
	ShowWindow(hWnd, SW_SHOWDEFAULT);

	// Register mouse raw input device
	RAWINPUTDEVICE rid;
	rid.usUsagePage = 0x01; // Mouse page
	rid.usUsage = 0x02; // Mouse usage
	rid.dwFlags = 0;
	rid.hwndTarget = nullptr;
	if (RegisterRawInputDevices(&rid, 1, sizeof(rid)) == FALSE)
	{
		throw WND_LAST_EXCEPT();
	}
}

Window::~Window()
{
	DestroyWindow(hWnd);

	ShutdownGraphics();
}

void Window::InitializeGraphics()
{
	if (!pGfx)
	{
		pGfx = std::make_unique<Graphics>(hWnd, width, height);
	}
}

void Window::RequestResize(UINT width, UINT height) noexcept
{
	// Ignore minimized/zero-sized dimensions; they are not valid ResizeBuffers sizes.
	if (width == 0 || height == 0)
		return;

	const std::uint64_t packed =
		(static_cast<std::uint64_t>(width) << 32) | height;

	// Keep only the newest requested size.
	pendingResize.store(packed, std::memory_order_release);
}

void Window::ApplyPendingResize()
{
	const std::uint64_t packed =
		pendingResize.exchange(0, std::memory_order_acquire);

	if (packed == 0 || !pGfx)
		return;

	const UINT width = static_cast<UINT>(packed >> 32);
	const UINT height = static_cast<UINT>(packed & 0xFFFFFFFFu);

	pGfx->OnWindowSizeChanged(width, height);
}

void Window::PublishImGuiCaptureState()
{
	const ImGuiIO& io = ImGui::GetIO();

	imguiWantsMouse.store(io.WantCaptureMouse, std::memory_order_relaxed);
	imguiWantsKeyboard.store(io.WantCaptureKeyboard, std::memory_order_relaxed);
	imguiItemHovered.store(ImGui::IsAnyItemHovered(), std::memory_order_relaxed);
	imguiMouseCursor.store(static_cast<int>(ImGui::GetMouseCursor()), std::memory_order_relaxed);

}

void Window::ApplyQueuedInputToImGui()
{
	ImGuiIO& io = ImGui::GetIO();

	const int noMouse =
		pendingImGuiNoMouse.exchange(-1, std::memory_order_acq_rel);

	if (noMouse == 0)
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
	else if (noMouse == 1)
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;

	const auto events = DrainInputEvents();
	for (const auto& event : events)
	{
		switch (event.type)
		{
		case InputEvent::Type::MousePosition:
			io.AddMousePosEvent(event.x, event.y);
			break;

		case InputEvent::Type::MouseButton:
			io.AddMouseButtonEvent(
				static_cast<int>(event.code),
				event.down
			);
			break;

		case InputEvent::Type::MouseWheel:
			io.AddMouseWheelEvent(event.x, event.y);
			break;

		case InputEvent::Type::Win32KeyboardMessage:
			ImGui_ImplWin32_WndProcHandlerEx(
				hWnd, event.message, event.wParam, event.lParam, io);

			switch (event.message)
			{
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				if (!imguiWantsKeyboard.load(std::memory_order_relaxed) &&
					(!(event.lParam & 0x40000000) || kbd.AutorepeatIsEnabled()))
				{
					kbd.OnKeyPressed(static_cast<unsigned char>(event.wParam));
				}
				break;

			case WM_KEYUP:
			case WM_SYSKEYUP:
				if (!imguiWantsKeyboard.load(std::memory_order_relaxed))
				{
					kbd.OnKeyReleased(static_cast<unsigned char>(event.wParam));
				}
				break;

			case WM_CHAR:
				if (!imguiWantsKeyboard.load(std::memory_order_relaxed))
				{
					kbd.OnChar(static_cast<unsigned char>(event.wParam));
				}
				break;

			case WM_KILLFOCUS:
				kbd.ClearState();
				break;
			}
			break;

		default:
			break; // We’ll add keyboard, text, and focus next.
		}
	}
}

void Window::ShutdownGraphics()
{
	if (!pGfx)
		return;

	ImGui_ImplWin32_Shutdown();
	pGfx.reset();
}

void Window::SetTitle(const std::string& title)
{
	if (SetWindowText(hWnd, title.c_str()) == 0)
	{
		throw WND_LAST_EXCEPT();
	}
}

void Window::EnableCursor() noexcept
{
	cursorEnabled = true;
	ShowCursor();
	EnableImGuiMouse();
	FreeCursor();
}

void Window::DisableCursor() noexcept
{
	cursorEnabled = false;
	HideCursor();
	DisableImGuiMouse();
	ConfineCursor();
}

bool Window::CursorEnabled() const noexcept
{
	return cursorEnabled;
}

void Window::SetDoFrameCallback(const DoFrameCallbackFn& doFrameCallback)
{
	DoFrameCallback = doFrameCallback;
}

std::optional<int> Window::ProcessMessages() noexcept
{
	// Non-blocking message pump - While queue has messages, remove and dispatch them but do not block on empty queue
	MSG msg;
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) 
	{
		//[DEBUG]
		//OutputDebugString(winMessageMap(msg.message, 0, 0).c_str());
		//spdlog::info(winMessageMap(msg.message, 0, 0).c_str());

		// Check for quit because peekmessage does not signal this via return val
		if (msg.message == WM_QUIT)
		{
			// Return optional wrapping int (arg to PostQuitMessage is in wparam) signals quit
			return (int)msg.wParam;
		}

		// TranslateMessage will post auxilliary WM_CHAR messages from key msgs
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// Return empty optional when not quitting app
	return {};
}

Graphics& Window::Gfx()
{
	if (!pGfx)
	{
		throw WND_NOGFX_EXCEPT();
	}
	return *pGfx;
}

void Window::ConfineCursor() noexcept
{
	RECT rect;
	GetClientRect(hWnd, &rect);
	MapWindowPoints(hWnd, nullptr, reinterpret_cast<POINT*>(&rect), 2); // Map 2 points in the current window into screen space
	ClipCursor(&rect); // WinAPI
}

void Window::FreeCursor() noexcept
{
	ClipCursor(nullptr); // WinAPI
}

void Window::HideCursor() noexcept
{
	while (::ShowCursor(FALSE) >= 0); // WinAPI
}

void Window::ShowCursor() noexcept
{
	while (::ShowCursor(TRUE) < 0); // WinAPI
}

void Window::EnableImGuiMouse() noexcept
{
	pendingImGuiNoMouse.store(0, std::memory_order_release);
}

void Window::DisableImGuiMouse() noexcept
{
	pendingImGuiNoMouse.store(1, std::memory_order_release);
}

void Window::BeginWindowDrag()
{
	gDragging = true;

	GetCursorPos(&gDragStartCursor);
	GetWindowRect(hWnd, &gDragStartWindow);

	SetCapture(hWnd);
}

void Window::UpdateWindowDrag()
{
	if (!gDragging)
		return;

	POINT cursor{};
	GetCursorPos(&cursor);

	const int dx = cursor.x - gDragStartCursor.x;
	const int dy = cursor.y - gDragStartCursor.y;

	const int newX = gDragStartWindow.left + dx;
	const int newY = gDragStartWindow.top + dy;

	SetWindowPos(
		hWnd,
		nullptr,
		newX,
		newY,
		0,
		0,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::EndWindowDrag()
{
	if (!gDragging)
		return;

	gDragging = false;

	if (GetCapture())
		ReleaseCapture();
}

LRESULT CALLBACK Window::HandleMsgSetup(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
{
	// Use create parameter passed in from CreateWindow() to store window class pointer at WinAPI side
	if (msg == WM_NCCREATE)
	{
		// Extract ptr to window class from creation data
		const CREATESTRUCTW* const pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
		Window* const pWnd = static_cast<Window*>(pCreate->lpCreateParams);
		// Set WinAPI-managed user data to store ptr to window instance
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWnd));
		// Set message proc to normal (non-setup) handler now that setup is finished
		SetWindowLongPtr(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Window::HandleMsgThunk));
		// forward message to window instance handler
		return pWnd->HandleMsg(hWnd, msg, wParam, lParam);
	}
	// If we get a message before the WM_NCCREATE message, handle with default handler
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK Window::HandleMsgThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
{
	// Retrieve ptr to window instance
	Window* const pWnd = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	// Forward message to window instance handler
	return pWnd->HandleMsg(hWnd, msg, wParam, lParam);
}

LRESULT Window::HandleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
{
	static std::string text;
	UINT_PTR uResult; // Set Timers return value
	static UINT sWidth = 0, sHeight = 0;
	
	static bool s_in_suspend = false;
	static bool s_minimized = false;
	static int mouseY = 0;

	switch (msg)
	{
	case WM_NCCALCSIZE:

		break;

	case WM_NCHITTEST:

		break;

	case WM_SIZE:
		sWidth = static_cast<UINT>(LOWORD(lParam));
		sHeight = static_cast<UINT>(HIWORD(lParam));
		RequestResize(sWidth, sHeight);

		break;

	case WM_ENTERSIZEMOVE:
		uResult = SetTimer(hWnd, 1, USER_TIMER_MINIMUM, NULL);
		if (uResult == 0)
		{
			// throw std::exception("No timer is available.");
			PostQuitMessage(0);
			return 0;
		}
		inSizemove = true;

		break;

	case WM_EXITSIZEMOVE:
		KillTimer(hWnd, 1);

		inSizemove = false;

		RECT rc;
		GetClientRect(hWnd, &rc);

		RequestResize(
			static_cast<UINT>(rc.right - rc.left),
			static_cast<UINT>(rc.bottom - rc.top)
		);
		
		break;

	case WM_TIMER:
		if (inSizemove && wParam == 1)
			RedrawWindow(hWnd, nullptr, nullptr, RDW_INTERNALPAINT);
		
		return 0;

	case WM_PAINT: {
		if (inSizemove && pGfx && DoFrameCallback) {
			DoFrameCallback();
		}
		else
		{
			PAINTSTRUCT ps;
			std::ignore = BeginPaint(hWnd, &ps);
			EndPaint(hWnd, &ps);
		}

		break;
	}

	case WM_CLOSE:
		PostQuitMessage(0);

		return 0;

	case WM_ACTIVATE:
		OutputDebugString("activeate\n");
		// Confine/free cursor on window to foreground/background if cursor disabled
		if (!cursorEnabled)
		{
			if (wParam & WA_ACTIVE)
			{
				OutputDebugString("activeate => confine\n");
				ConfineCursor();
				HideCursor();
			}
			else
			{
				OutputDebugString("activeate => free\n");
				FreeCursor();
				ShowCursor();
			}
		}

		break;

// ---- ---- ---- ---- ---- ---- ---- ---- ---- KEYBOARD MESSAGES ---- ---- ---- ---- ---- ---- ---- ---- ----
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYUP:
	case WM_CHAR:
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
	{
		InputEvent event{};
		event.type = InputEvent::Type::Win32KeyboardMessage;
		event.message = msg;
		event.wParam = wParam;
		event.lParam = lParam;
		QueueInputEvent(event);
		break;
	}

// ---- ---- ---- ---- ---- ---- ---- ---- ---- MOUSE MESSAGES ---- ---- ---- ---- ---- ---- ---- ---- ----

	case WM_MOUSEMOVE:
	{
		UpdateWindowDrag();
	
		const POINTS pt = MAKEPOINTS(lParam);

		// Cursorless exclusive gets first dibs
		if (!cursorEnabled)
		{
			if (!mouse.IsInWindow())
			{
				SetCapture(hWnd);
				mouse.OnMouseEnter();
				HideCursor();
			}
			break;
		}

		InputEvent event{};
		event.type = InputEvent::Type::MousePosition;
		event.x = static_cast<float>(pt.x);
		event.y = static_cast<float>(pt.y);
		QueueInputEvent(event);
		
		if (!imguiWantsMouse.load(std::memory_order_relaxed))
		{
			break;
		}
		// In client region -> log move, and log enter + capture mouse (if not previously in window)
		if (pt.x >= 0 && pt.x < width && pt.y >= 0 && pt.y < height)
		{
			mouse.OnMouseMove(pt.x, pt.y);
			if (!mouse.IsInWindow())
			{
				SetCapture(hWnd);
				mouse.OnMouseEnter();
			}
		}
		// Not in client -> log move / maintain capture if button down
		else
		{
			if (wParam & (MK_LBUTTON | MK_RBUTTON))
			{
				mouse.OnMouseMove(pt.x, pt.y);
			}
			// Button up -> release capture / log event for leaving
			else
			{
				ReleaseCapture();
				mouse.OnMouseLeave();
			}
		}
		break;
	}
	case WM_LBUTTONDOWN:
	{
		mouseY = static_cast<int>(HIWORD(lParam));

		const bool inCustomTitleBar =
			mouseY >= 0 && mouseY < TITLE_BAR_HEIGHT;

		const bool overImGuiItem =
			imguiItemHovered.load(std::memory_order_relaxed);

		if (inCustomTitleBar && !overImGuiItem)
		{
			titleBarDragActive = true;
			BeginWindowDrag();
			return 0;
		}

		titleBarDragActive = false;

		InputEvent event{};
		event.type = InputEvent::Type::MouseButton;
		event.code = 0; // ImGui left button
		event.down = true;
		QueueInputEvent(event);

		break;

	}
	case WM_RBUTTONDOWN:
	{
		InputEvent event{};
		event.type = InputEvent::Type::MouseButton;
		event.code = 1; // ImGui right button
		event.down = true;
		QueueInputEvent(event);

		if (imguiWantsMouse.load(std::memory_order_relaxed))
		{
			break;
		}
		const POINTS pt = MAKEPOINTS(lParam);
		mouse.OnRightPressed(pt.x, pt.y);
		break;
	}
	case WM_LBUTTONUP:
	{
		SetForegroundWindow(hWnd);

		if (!cursorEnabled)
		{
			OutputDebugString("lclick => recapture\n");
			ConfineCursor();
			HideCursor();
		}

		const bool wasTitleBarDrag = titleBarDragActive;
		titleBarDragActive = false;

		if (wasTitleBarDrag)
		{
			EndWindowDrag();
		}

		if (!wasTitleBarDrag)
		{
			InputEvent event{};
			event.type = InputEvent::Type::MouseButton;
			event.code = 0;
			event.down = false;
			QueueInputEvent(event);
		}

		if (imguiWantsMouse.load(std::memory_order_relaxed))
		{
			break;
		}
		const POINTS pt = MAKEPOINTS(lParam);
		mouse.OnLeftReleased(pt.x, pt.y);
		// Release mouse if outside of window
		if (pt.x < 0 || pt.x >= width || pt.y < 0 || pt.y >= height)
		{
			ReleaseCapture();
			mouse.OnMouseLeave();
		}
		break;
	}
	case WM_RBUTTONUP:
	{
		if (mouseY >= TITLE_BAR_HEIGHT)
		{
			InputEvent event{};
			event.type = InputEvent::Type::MouseButton;
			event.code = 1;
			event.down = false;
			QueueInputEvent(event);
		}

		if (imguiWantsKeyboard.load(std::memory_order_relaxed))
		{
			break;
		}
		const POINTS pt = MAKEPOINTS(lParam);
		mouse.OnRightReleased(pt.x, pt.y);
		// Release mouse if outside of window
		if (pt.x < 0 || pt.x >= width || pt.y < 0 || pt.y >= height)
		{
			ReleaseCapture();
			mouse.OnMouseLeave();
		}
		break;
	}
	case WM_MOUSEWHEEL:
	{
		// Another log
		//spdlog::info("Hello from spdlog!");
		//spdlog::info(winMessageMap(msg, lParam, wParam).c_str());

		InputEvent event{};
		event.type = InputEvent::Type::MouseWheel;
		event.y = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
			static_cast<float>(WHEEL_DELTA);
		QueueInputEvent(event);

		if (imguiWantsMouse.load(std::memory_order_relaxed))
		{
			break;
		}
		const POINTS pt = MAKEPOINTS(lParam);
		const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
		mouse.OnWheelDelta(pt.x, pt.y, delta);
		break;
	}
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT)
		{
			if (!cursorEnabled)
			{
				::SetCursor(nullptr);
				return TRUE;
			}

			HCURSOR cursor = ::LoadCursor(nullptr, IDC_ARROW);

			switch (static_cast<ImGuiMouseCursor>(
				imguiMouseCursor.load(std::memory_order_relaxed)))
			{
			case ImGuiMouseCursor_None:
				cursor = nullptr;
				break;
			case ImGuiMouseCursor_TextInput:
				cursor = ::LoadCursor(nullptr, IDC_IBEAM);
				break;
			case ImGuiMouseCursor_ResizeAll:
				cursor = ::LoadCursor(nullptr, IDC_SIZEALL);
				break;
			case ImGuiMouseCursor_ResizeNS:
				cursor = ::LoadCursor(nullptr, IDC_SIZENS);
				break;
			case ImGuiMouseCursor_ResizeEW:
				cursor = ::LoadCursor(nullptr, IDC_SIZEWE);
				break;
			case ImGuiMouseCursor_ResizeNESW:
				cursor = ::LoadCursor(nullptr, IDC_SIZENESW);
				break;
			case ImGuiMouseCursor_ResizeNWSE:
				cursor = ::LoadCursor(nullptr, IDC_SIZENWSE);
				break;
			case ImGuiMouseCursor_Hand:
				cursor = ::LoadCursor(nullptr, IDC_HAND);
				break;
			case ImGuiMouseCursor_NotAllowed:
				cursor = ::LoadCursor(nullptr, IDC_NO);
				break;
			case ImGuiMouseCursor_Arrow:
			default:
				break;
			}

			::SetCursor(cursor);
			return TRUE;
		}
		break;

	/************** END MOUSE MESSAGES **************/

	/************** RAW MOUSE MESSAGES **************/
	case WM_INPUT:
	{
		if (!mouse.RawEnabled())
		{
			break;
		}
		UINT size;
		// first get the size of the input data
		if (GetRawInputData(
			reinterpret_cast<HRAWINPUT>(lParam),
			RID_INPUT,
			nullptr,
			&size,
			sizeof(RAWINPUTHEADER)) == -1)
		{
			// bail msg processing if error
			break;
		}
		rawBuffer.resize(size);
		// read in the input data
		if (GetRawInputData(
			reinterpret_cast<HRAWINPUT>(lParam),
			RID_INPUT,
			rawBuffer.data(),
			&size,
			sizeof(RAWINPUTHEADER)) != size)
		{
			// bail msg processing if error
			break;
		}
		// process the raw input data
		auto& ri = reinterpret_cast<const RAWINPUT&>(*rawBuffer.data());
		if (ri.header.dwType == RIM_TYPEMOUSE &&
			(ri.data.mouse.lLastX != 0 || ri.data.mouse.lLastY != 0))
		{
			mouse.OnRawDelta(ri.data.mouse.lLastX, ri.data.mouse.lLastY);
		}
		break;
	}
	/************** END RAW MOUSE MESSAGES **************/

	} // switch

	return DefWindowProc(hWnd, msg, wParam, lParam);
}


// Window Exception Stuff
std::string Window::Exception::TranslateErrorCode(HRESULT hr) noexcept
{
	char* pMsgBuf = nullptr;
	// Windows will allocate memory for err string and make our pointer point to it
	const DWORD nMsgLen = FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPSTR>(&pMsgBuf), 0, nullptr
	);
	// 0 string length returned indicates a failure
	if (nMsgLen == 0)
	{
		return "Unidentified error code";
	}
	// Copy error string from windows-allocated buffer to std::string
	std::string errorString = pMsgBuf;
	// Free windows buffer
	LocalFree(pMsgBuf);
	return errorString;
}


Window::HrException::HrException(int line, const char* file, HRESULT hr) noexcept
	:
	Exception(line, file),
	hr(hr)
{
}

const char* Window::HrException::what() const noexcept
{
	std::ostringstream oss;
	oss << GetType() << std::endl
		<< "[Error Code] 0x" << std::hex << std::uppercase << GetErrorCode()
		<< std::dec << " (" << (unsigned long)GetErrorCode() << ")" << std::endl
		<< "[Description] " << GetErrorDescription() << std::endl
		<< GetOriginString();
	whatBuffer = oss.str();
	return whatBuffer.c_str();
}

const char* Window::HrException::GetType() const noexcept
{
	return "Window Exception";
}

HRESULT Window::HrException::GetErrorCode() const noexcept
{
	return hr;
}

std::string Window::HrException::GetErrorDescription() const noexcept
{
	return Exception::TranslateErrorCode(hr);
}


const char* Window::NoGfxException::GetType() const noexcept
{
	return "Window Exception [No Graphics]";
}