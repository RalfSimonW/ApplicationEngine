#include "ImguiManager.h"
#include "imgui.h"

ImguiManager::ImguiManager()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows

	io.Fonts->AddFontFromFileTTF("Assets/fonts/opensans/OpenSans-Bold.ttf", 24.0f);
	io.FontDefault = io.Fonts->AddFontFromFileTTF("Assets/fonts/opensans/OpenSans-Regular.ttf", 24.0f);

	ImGui::StyleColorsClassic();
}

ImguiManager::~ImguiManager()
{
	ImGui::DestroyContext();
}
