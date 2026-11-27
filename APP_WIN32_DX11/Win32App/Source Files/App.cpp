#include <Windows.h>
#include <commctrl.h>

#include "App.h"
#include "Melon.h"
#include "Pyramid.h"
#include "Box.h"
#include "Cylinder.h"
#include "Sheet.h"
#include "SkinnedBox.h"
#include "AssTest.h"
#include <algorithm>
#include <functional>
#include "MathBase.h"
#include "Surface.h"
#include "GDIPlusManager.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "ResizeDiag.h"

#pragma comment(lib, "Comctl32.lib")

namespace
{
	constexpr UINT_PTR kViewportSubclassId = 0xDA10;
	constexpr UINT_PTR kViewportResizeTimerId = 0xDA11;

	LRESULT CALLBACK ViewportSizingProc(
		HWND hwnd,
		UINT message,
		WPARAM wParam,
		LPARAM lParam,
		UINT_PTR subclassId,
		DWORD_PTR refData)
	{
		auto* app = reinterpret_cast<App*>(refData);

		if (message == WM_ERASEBKGND)
		{
			return 1; // The DX11 swap chain supplies the window contents.
		}
		else if (message == WM_ENTERSIZEMOVE)
		{
			SetTimer(hwnd, kViewportResizeTimerId, 16, nullptr);
		}
		else if (message == WM_TIMER && wParam == kViewportResizeTimerId)
		{
			// This runs on the render thread inside the native sizing loop.
			app->DoFrame();
			return 0;
		}
		else if (message == WM_EXITSIZEMOVE)
		{
			KillTimer(hwnd, kViewportResizeTimerId);
		}
		else if (message == WM_NCDESTROY)
		{
			KillTimer(hwnd, kViewportResizeTimerId);
			RemoveWindowSubclass(hwnd, ViewportSizingProc, subclassId);
		}

		return DefSubclassProc(hwnd, message, wParam, lParam);
	}
}

namespace dx = DirectX;

GDIPlusManager gdipm;


App::App(const std::string& commandLine) : commandLine(commandLine), wnd(1920, 1200, "To Texture")
{
}

App::~App()
{
}

void App::DoFrame()
{
#if RESIZE_DIAG_ENABLED
	using Clock = std::chrono::steady_clock;
	const auto frameStart = Clock::now();
	const uint64_t session = ResizeDiag::ActiveMeasurementSession();

	const auto elapsedUs = [](Clock::time_point start)
		{
			return static_cast<unsigned long long>(
				std::chrono::duration_cast<std::chrono::microseconds>(
					Clock::now() - start
				).count()
				);
		};
#endif

#if RESIZE_DIAG_ENABLED
	const auto inputStart = Clock::now();
#endif

	wnd.ApplyPendingResize();
	wnd.ApplyQueuedInputToImGui();

#if RESIZE_DIAG_ENABLED
	const auto inputQueueUs = elapsedUs(inputStart);
#endif

	const auto dt = timer.Mark() * speed_factor;

#if RESIZE_DIAG_ENABLED
	const auto graphicsBeginStart = Clock::now();
#endif

	wnd.Gfx().Begin();

#if RESIZE_DIAG_ENABLED
	const auto graphicsBeginUs = elapsedUs(graphicsBeginStart);
	const auto mainResizeUs = wnd.Gfx().LastMainResizeDurationUs();
	const auto sceneApplyUs = wnd.Gfx().LastSceneResizeDurationUs();
	const auto gpuQueryPollUs = wnd.Gfx().LastGpuQueryPollDurationUs();
	const bool sceneApplyCalled = wnd.Gfx().LastSceneResizeCalled();

	// Graphics::Begin includes resize work and GPU-query polling.
	// Subtract both so GraphicsBeginOther is the remaining time.
	const auto accountedBeginUs =
		mainResizeUs + sceneApplyUs + gpuQueryPollUs;

	const auto graphicsBeginOtherUs =
		graphicsBeginUs > accountedBeginUs
		? graphicsBeginUs - accountedBeginUs
		: 0;
#endif

#if RESIZE_DIAG_ENABLED
	const auto sceneSetupStart = Clock::now();
#endif
	wnd.Gfx().SetCamera(cam.GetMatrix());
	light->Bind(wnd.Gfx(), cam.GetMatrix());
#if RESIZE_DIAG_ENABLED
	const auto sceneSetupUs = elapsedUs(sceneSetupStart);
	const auto sceneStart = Clock::now();
#endif

	UpdateScene(dt);
	DrawScene();

#if RESIZE_DIAG_ENABLED
	const auto renderSceneUs = elapsedUs(sceneStart);
	const auto imguiBuildStart = Clock::now();
#endif

	SpawnSimulationWindow();
	cam.SpawnControlWindow();
	light->SpawnControlWindow();
	SpawnBoxWindowManagerWindow();
	SpawnBoxWindows();
	wnd.PublishImGuiCaptureState();

#if RESIZE_DIAG_ENABLED
	const auto imguiBuildUs = elapsedUs(imguiBuildStart);
#endif

	wnd.Gfx().End();

#if RESIZE_DIAG_ENABLED
	const auto viewportCallbacksStart = Clock::now();
#endif

	InstallViewportSizingCallbacks();

#if RESIZE_DIAG_ENABLED
	const auto viewportCallbacksUs = elapsedUs(viewportCallbacksStart);
#endif

#if RESIZE_DIAG_ENABLED
	if (session != 0)
	{
		const auto frameUs = elapsedUs(frameStart);

		ResizeDiag::RecordFrameTimingMulti(
			session,
			frameUs,
			wnd.Gfx().LastPresentDurationUs(),
			sceneApplyUs,
			mainResizeUs,
			inputQueueUs,
			gpuQueryPollUs,
			graphicsBeginOtherUs,
			sceneSetupUs,
			renderSceneUs,
			imguiBuildUs,
			wnd.Gfx().LastMainDrawDurationUs(),
			wnd.Gfx().LastPlatformWindowsDurationUs(),
			viewportCallbacksUs,
			sceneApplyCalled
		);
	}
#endif
}

void App::SpawnSimulationWindow() noexcept
{
	static char buffer[1024];
	if (ImGui::Begin("Simulation Speed"))
	{
		ImGui::SliderFloat("Speed Factor", &speed_factor, 0.0f, 6.0f, "%.4f", ImGuiSliderFlags_Logarithmic);

		// OutputDebugString(("App::SpawnSimulationWindow (spacebar pressed): " + std::to_string(wnd.kbd.KeyIsPressed(VK_SPACE)) + "\n").c_str());

		ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::Text("Status: %s", wnd.kbd.KeyIsPressed(VK_SPACE) ? "PAUSED" : "RUNNING (hold spacebar to pause)");
		ImGui::InputText("Some input text", buffer, sizeof(buffer));
	}
	ImGui::End();
}

void App::SpawnBoxWindowManagerWindow() noexcept
{
	if (ImGui::Begin("Boxes"))
	{
		using namespace std::string_literals;
		const auto preview = comboBoxIndex ? std::to_string(*comboBoxIndex) : "Choose a box..."s;
		if (ImGui::BeginCombo("Box Number", preview.c_str()))
		{
			for (int i = 0; i < boxes.size(); i++)
			{
				bool selected = false;
				if (comboBoxIndex.has_value())
				{
					selected = *comboBoxIndex == i;
				}
				if (ImGui::Selectable(std::to_string(i).c_str(), selected))
				{
					comboBoxIndex = i;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button("Spawn Control Window") && comboBoxIndex)
		{
			boxControlIds.insert(*comboBoxIndex);
			comboBoxIndex.reset();
		}
	}
	ImGui::End();
}

void App::SpawnBoxWindows() noexcept
{
	for (auto i = boxControlIds.begin(); i != boxControlIds.end(); )
	{
		if (!boxes[*i]->SpawnControlWindow(*i, wnd.Gfx()))
		{
			i = boxControlIds.erase(i);
		}
		else
		{
			i++;
		}
	}
}

void App::InitializeRenderResources()
{
	imgui = std::make_unique<ImguiManager>();
	wnd.InitializeGraphics();

	light = std::make_unique<PointLight>(wnd.Gfx());
	nano = std::make_unique<Model>(
		wnd.Gfx(),
		"Models\\nano_textured\\nanosuit.obj"
	);

	// Move the existing local Factory class definition here unchanged.
	class Factory
	{
	public:
		Factory(Graphics& gfx)
			:
			gfx(gfx)
		{
		}
		std::unique_ptr<Drawable> operator()()
		{
			const DirectX::XMFLOAT3 mat = { cdist(rng),cdist(rng),cdist(rng) };

			switch (sdist(rng))
			{
			case 0:
				return std::make_unique<Box>(
					gfx, rng, adist, ddist,
					odist, rdist, bdist, mat
				);
			case 1:
				return std::make_unique<Cylinder>(
					gfx, rng, adist, ddist, odist,
					rdist, bdist, tdist
				);
			case 2:
				return std::make_unique<Pyramid>(
					gfx, rng, adist, ddist, odist,
					rdist, tdist
				);
			case 3:
				return std::make_unique<SkinnedBox>(
					gfx, rng, adist, ddist,
					odist, rdist
				);
			case 4:
				return std::make_unique<AssTest>(
					gfx, rng, adist, ddist,
					odist, rdist, mat, 1.5f
				);
			default:
				assert(false && "impossible drawable option in factory");
				return {};
			}
		} // operator()
	private:
		Graphics& gfx;
		std::mt19937 rng{ std::random_device{}() };
		std::uniform_int_distribution<int> sdist{ 0,4 };

		std::uniform_real_distribution<float> adist{ 0.0f,PI * 2.0f };
		std::uniform_real_distribution<float> ddist{ 0.0f,PI * 0.5f };
		std::uniform_real_distribution<float> odist{ 0.0f,PI * 0.08f };
		std::uniform_real_distribution<float> rdist{ 6.0f,20.0f };
		std::uniform_real_distribution<float> bdist{ 0.4f,3.0f };
		std::uniform_real_distribution<float> cdist{ 0.0f,1.0f };
		std::uniform_int_distribution<int> tdist{ 3,30 };
		std::uniform_int_distribution<int> latdist{ 5,20 };
		std::uniform_int_distribution<int> longdist{ 10,40 };
		std::uniform_int_distribution<int> typedist{ 0, 2 };
	}; // Factory

	drawables.reserve(nDrawables);
	std::generate_n(
		std::back_inserter(drawables),
		nDrawables,
		Factory{ wnd.Gfx() }
	);

	for (auto& pd : drawables)
	{
		if (auto pb = dynamic_cast<Box*>(pd.get()))
		{
			boxes.push_back(pb);
		}
	}

	wnd.Gfx().SetProjection(
		dx::XMMatrixPerspectiveLH(1.0f, 9.0f / 16.0f, 0.5f, 40.0f)
	);
}

void App::ShutdownRenderResources()
{
	boxes.clear();       // Non-owning pointers; clear before their objects.
	drawables.clear();
	nano.reset();
	light.reset();

	wnd.ShutdownGraphics();
	imgui.reset();
}

void App::RenderThreadMain()
{
	try
	{
		InitializeRenderResources();
		renderInitPromise.set_value();
	}
	catch (...)
	{
		const auto error = std::current_exception();
		ShutdownRenderResources();
		renderInitPromise.set_exception(error);
		return;
	}

	while (!renderStop.load(std::memory_order_acquire))
	{
		MSG msg{};
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				renderStop.store(true, std::memory_order_release);
				break;
			}

			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		if (renderStop.load(std::memory_order_acquire))
			break;

		DoFrame();
	}

	ShutdownRenderResources();
}

void App::InstallViewportSizingCallbacks()
{
	if (ImGui::GetCurrentContext() == nullptr)
		return;

	ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();

	// Viewport 0 is the main window, owned by the UI thread.
	for (int i = 1; i < platformIO.Viewports.Size; ++i)
	{
		ImGuiViewport* viewport = platformIO.Viewports[i];
		if (viewport == nullptr || viewport->PlatformHandle == nullptr)
			continue;

		HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);

		SetWindowSubclass(
			hwnd,
			ViewportSizingProc,
			kViewportSubclassId,
			reinterpret_cast<DWORD_PTR>(this));
	}
}

void App::UpdateScene(float dt)
{
	// Render geometry
	for (auto& d : drawables)
	{
		d->Update(wnd.kbd.KeyIsPressed(VK_SPACE) ? 0.0f : dt); // No update while SPACE is pressed
	}
}

void App::DrawScene()
{
	// Render geometry
	for (auto& d : drawables)
	{
		d->Draw(wnd.Gfx());
	}

	light->Draw(wnd.Gfx());
}

void App::ShowModelWindow()
{
	if (ImGui::Begin("Model"))
	{
		using namespace std::string_literals;

		ImGui::Text("Orientation");
		ImGui::SliderAngle("Roll", &pos.roll, -180.0f, 180.0f);
		ImGui::SliderAngle("Pitch", &pos.pitch, -180.0f, 180.0f);
		ImGui::SliderAngle("Yaw", &pos.yaw, -180.0f, 180.0f);

		ImGui::Text("Position");
		ImGui::SliderFloat("X", &pos.x, -20.0f, 20.0f);
		ImGui::SliderFloat("Y", &pos.y, -20.0f, 20.0f);
		ImGui::SliderFloat("Z", &pos.z, -20.0f, 20.0f);
	}
	ImGui::End();
}

void App::ShowImguiDemoWindow()
{
	if (showDemoWindow)
	{
		ImGui::ShowDemoWindow(&showDemoWindow);
	}
}

void App::ShowRawInputWindow()
{
	while (const auto d = wnd.mouse.ReadRawDelta())
	{
		x += d->x;
		y += d->y;
	}
	if (ImGui::Begin("Raw Input"))
	{
		ImGui::Text("Tally: (%d,%d)", x, y);
		ImGui::Text("Cursor: %s", wnd.CursorEnabled() ? "enabled" : "disabled");
	}
	ImGui::End();
}

int App::Go()
{
	auto renderReady = renderInitPromise.get_future();

	renderStop.store(false, std::memory_order_release);
	renderThread = std::thread(&App::RenderThreadMain, this);

	try
	{
		renderReady.get();
	}
	catch (...)
	{
		if (renderThread.joinable())
		{
			renderThread.join();
		}
		throw;
	}

	for (;;)
	{
		if (const auto ecode = Window::ProcessMessages())
		{
			renderStop.store(true, std::memory_order_release);

			if (renderThread.joinable())
			{
				renderThread.join();
			}

			return *ecode;
		}

		::WaitMessage();
	}
}
