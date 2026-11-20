#pragma once
#include "Window.h"
#include "SimpleTimer.h"
#include "ImguiManager.h"
#include "Camera.h"
#include "PointLight.h"
#include "Mesh.h"

#include <set>
#include <atomic>
#include <thread>
#include <future>

class App
{
public:
	App(const std::string& commandLine = "");
	// Run master frame / message loop
	int Go();
	~App();
public:
	void DoFrame();
private:
	void ShowModelWindow();
	void ShowImguiDemoWindow();
	void ShowRawInputWindow();
	void SpawnSimulationWindow() noexcept;
	void SpawnBoxWindowManagerWindow() noexcept;
	void SpawnBoxWindows() noexcept;

	void InitializeRenderResources();

	void ShutdownRenderResources();

	void RenderThreadMain();

private:
	std::string commandLine;

	int x = 0, y = 0;
	bool showDemoWindow = false;

	std::unique_ptr<ImguiManager> imgui;
	Window wnd;
	SimpleTimer timer;

	std::vector<std::unique_ptr<class Drawable>> drawables;
	std::vector<class Box*> boxes;
	bool show_demo_window = true;
	float speed_factor = .2f;
	Camera cam;

	std::unique_ptr<PointLight> light;
	std::unique_ptr<Model> nano;

	// useSimple
	static constexpr size_t nDrawables = 180;
	std::optional<int> comboBoxIndex = { 0 };
	std::set<int> boxControlIds;

	// useSimple
	struct
	{
		float roll = 0.0f;
		float pitch = 0.0f;
		float yaw = 0.0f;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	} pos;

	std::thread renderThread;
	std::atomic<bool> renderStop{ false };
	std::promise<void> renderInitPromise;

};