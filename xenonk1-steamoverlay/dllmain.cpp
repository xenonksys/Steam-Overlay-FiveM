#include "includes.h"
#include <stdio.h>
#include <stdarg.h>
#include <dxgi.h>
/*
quick readme

1. steam must be open 
2. Drag dll to plugins or inject using testLucher.exe Source https://github.com/xenonksys
3. menu key ins 


*/

HWND hWnd = NULL;
Present oPresent = NULL;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* pRenderTargetView;
WNDPROC oWndProc;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void DebugPrint(const char* fmt, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, fmt);
	_vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
	va_end(args);
	OutputDebugStringA(buffer);
	OutputDebugStringA("\n");
}

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (true && ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
	{
		return true;
	}
	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

// Fallback: fetch IDXGISwapChain::Present address from a dummy swap chain vtable
static uintptr_t GetDXGIPresentAddressFromDummySwapchain()
{
	
	typedef HRESULT (WINAPI *PFN_D3D11CreateDeviceAndSwapChain)(
		IDXGIAdapter*,
		D3D_DRIVER_TYPE,
		HMODULE,
		UINT,
		const D3D_FEATURE_LEVEL*,
		UINT,
		UINT,
		const DXGI_SWAP_CHAIN_DESC*,
		IDXGISwapChain**,
		ID3D11Device**,
		D3D_FEATURE_LEVEL*,
		ID3D11DeviceContext**);

	uintptr_t presentAddr = 0;
	PFN_D3D11CreateDeviceAndSwapChain fnCreate = NULL;
	void** vtbl = NULL;

	WNDCLASSEXA wc = { 0 };
	wc.cbSize = sizeof(WNDCLASSEXA);
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(NULL);
	wc.lpszClassName = "DX11DummyWindowClass";
	RegisterClassExA(&wc);
	HWND hDummy = CreateWindowExA(0, wc.lpszClassName, "DX11Dummy", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 1;
	sd.BufferDesc.Width = 100;
	sd.BufferDesc.Height = 100;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hDummy;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL flOut = D3D_FEATURE_LEVEL_11_0;
	D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
	ID3D11Device* pDev = NULL;
	ID3D11DeviceContext* pCtx = NULL;
	IDXGISwapChain* pSC = NULL;

	HMODULE hD3D11 = GetModuleHandleA("d3d11.dll");
	if (!hD3D11) hD3D11 = LoadLibraryA("d3d11.dll");
	if (!hD3D11) goto cleanup;
	fnCreate = (PFN_D3D11CreateDeviceAndSwapChain)GetProcAddress(hD3D11, "D3D11CreateDeviceAndSwapChain");
	if (!fnCreate) goto cleanup;

	if (FAILED(fnCreate(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, fls, 2, D3D11_SDK_VERSION, &sd, &pSC, &pDev, &flOut, &pCtx)))
		goto cleanup;

	vtbl = *(void***)(pSC);
	if (vtbl)
		presentAddr = (uintptr_t)vtbl[8];

cleanup:
	if (pSC) pSC->Release();
	if (pCtx) pCtx->Release();
	if (pDev) pDev->Release();
	if (hDummy) DestroyWindow(hDummy);
	UnregisterClassA(wc.lpszClassName, wc.hInstance);
	return presentAddr;
}

bool ImGuiInit = false;
bool menu = false;
bool myCheckbox = false;
static bool gShownFirstPresentMsg = false;
static ID3D11Texture2D* g_BackBuffer = NULL;
static bool g_WndProcHooked = false;

static void CleanupRenderTarget()
{
	if (pRenderTargetView) { pRenderTargetView->Release(); pRenderTargetView = NULL; }
	if (g_BackBuffer) { g_BackBuffer->Release(); g_BackBuffer = NULL; }
}

static void EnsureRenderTarget(IDXGISwapChain* pSwapchain)
{
	ID3D11Texture2D* currentBackBuffer = NULL;
	if (SUCCEEDED(pSwapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&currentBackBuffer)))
	{
		if (currentBackBuffer != g_BackBuffer || pRenderTargetView == NULL)
		{
			CleanupRenderTarget();
			g_BackBuffer = currentBackBuffer; //  GetBuffer
			if (pDevice)
				pDevice->CreateRenderTargetView(g_BackBuffer, NULL, &pRenderTargetView);
		}
		else
		{
			currentBackBuffer->Release();
		}
	}
}
HRESULT hkPresent(IDXGISwapChain* pSwapchain, UINT SyncInterval, UINT Flags) {
	if (!gShownFirstPresentMsg)
	{
		gShownFirstPresentMsg = true;
		OutputDebugStringA("[OverlayHook] hkPresent called.\n");
	}
	if (!ImGuiInit) {
		if (SUCCEEDED(pSwapchain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice))) {
			pDevice->GetImmediateContext(&pContext);
			DXGI_SWAP_CHAIN_DESC sd;
			pSwapchain->GetDesc(&sd);
			hWnd = sd.OutputWindow;
			EnsureRenderTarget(pSwapchain);
			oWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
			g_WndProcHooked = (oWndProc != NULL);
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
			ImGui_ImplWin32_Init(hWnd);
			ImGui_ImplDX11_Init(pDevice, pContext);
			ImGuiInit = true;
			DebugPrint("ImGui initialized. Device=%p Context=%p HWND=%p RTV=%p", pDevice, pContext, hWnd, pRenderTargetView);
		}
		else {
			DebugPrint("GetDevice failed in hkPresent");
			return oPresent(pSwapchain, SyncInterval, Flags);
		}
	}
	else
	{
		//  RTV 
		EnsureRenderTarget(pSwapchain);
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	if (GetAsyncKeyState(VK_INSERT) & 1)
	{
		menu = !menu;
		

    }
	if (menu)
	{

		ImGui::GetMouseCursor();
		ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
		ImGui::GetIO().WantCaptureMouse = menu;
		ImGui::GetIO().MouseDrawCursor = menu;

		ImGuiStyle& style = ImGui::GetStyle();

		
		style.Alpha = 0.95f;
		style.ChildBorderSize = 1.0f;
		style.WindowPadding = ImVec2(8, 8);
		style.WindowMinSize = ImVec2(32, 32);
		style.WindowRounding = 6.f;
		style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		style.WindowBorderSize = 0.f;
		style.FrameRounding = 4.0f;
		style.ItemSpacing = ImVec2(4, 9);
		style.ItemInnerSpacing = ImVec2(8, 8);
		style.TouchExtraPadding = ImVec2(0, 0);
		style.IndentSpacing = 21.0f;
		style.ColumnsMinSpacing = 3.0f;
		style.ScrollbarSize = 14.0f;
		style.ScrollbarRounding = 0.0f;
		style.GrabMinSize = 5.0f;
		style.GrabRounding = 0.0f;
		style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
		style.DisplayWindowPadding = ImVec2(22, 22);
		style.DisplaySafeAreaPadding = ImVec2(4, 4);
		style.AntiAliasedLines = true;
		

		style.CurveTessellationTol = 1.25f;
		style.Colors[ImGuiCol_TitleBg] = ImColor(25,25,25, 185);
		style.Colors[ImGuiCol_TitleBgActive] = ImColor(36, 38, 52, 255);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImColor(36, 38, 52, 255);
		style.Colors[ImGuiCol_FrameBg] = ImColor(0, 143,248 , 200);
		style.Colors[ImGuiCol_FrameBgActive] = ImColor(36, 38, 52, 255);
		style.Colors[ImGuiCol_FrameBgHovered] = ImColor(36, 38, 52, 255);
		style.Colors[ImGuiCol_Button] = ImColor(78, 79, 85, 255);
		style.Colors[ImGuiCol_ButtonActive] = ImColor(78, 79, 85, 200);
		style.Colors[ImGuiCol_ButtonHovered] = ImColor(78, 79, 85, 255);
		style.Colors[ImGuiCol_Text] = ImColor(255, 255, 255, 255);
		style.Colors[ImGuiCol_WindowBg] = ImColor(29, 29, 29, 255);
		style.Colors[ImGuiCol_TabActive] = ImColor(28, 28, 39, 255);
		style.Colors[ImGuiCol_TabHovered] = ImColor(28, 28, 39, 255);
		style.Colors[ImGuiCol_TabUnfocused] = ImColor(28, 28, 39, 255);
		style.Colors[ImGuiCol_TabUnfocusedActive] = ImColor(28, 28, 39, 255);
		style.Colors[ImGuiCol_Tab] = ImColor(28, 28, 39, 255);
		style.Colors[ImGuiCol_MenuBarBg] = ImColor(17, 40, 55, 255);

		ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
		ImGui::Begin("", &menu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
		ImGui::SetWindowSize(ImVec2(500, 450), ImGuiCond_Always);
		ImGui::Checkbox("Checkbox", &myCheckbox);

		ImGui::End();

	}
	
	ImGui::Begin("##dbg", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
	ImGui::SetWindowPos(ImVec2(10, 10), ImGuiCond_Always);
	ImGui::Text("ImGui hook active. menu=%d", menu ? 1 : 0);
	ImGui::End();
	ImGui::Render();
	if (pContext && pRenderTargetView)
	{
		pContext->OMSetRenderTargets(1, &pRenderTargetView, NULL);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}
	return oPresent(pSwapchain, SyncInterval, Flags);
}

DWORD WINAPI MainThread(LPVOID)
{
	
	uintptr_t steamModule = 0;
	for (int i = 0; i < 200; ++i) // up to ~10s
	{
		steamModule = SteamOverlay::GetSteamModule();
		if (steamModule) break;
		Sleep(50);
	}
	DebugPrint("SteamOverlay module base: %p", (void*)steamModule);
	if (steamModule)
	{
		uintptr_t pPresentAddr = SteamOverlay::FindPattern(steamModule, "48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 54 41 56 41 57 48 83 EC ? 41 8B E8");
		DebugPrint("Present pattern address: %p", (void*)pPresentAddr);
		if (pPresentAddr)
		{
			SteamOverlay::CreateHook(pPresentAddr, (uintptr_t)hkPresent, (long long*)&oPresent);
			DebugPrint("CreateHook invoked (Steam pattern).");
		}
		else
		{
			DebugPrint("Present pattern not found in Steam overlay module.");
		}
	}
	else
	{
		DebugPrint("GameOverlayRenderer64.dll not loaded. Proceeding to fallback.");
	}

	if (oPresent == NULL)
	{
		
		uintptr_t vtPresent = GetDXGIPresentAddressFromDummySwapchain();
		DebugPrint("Fallback vtable Present address: %p", (void*)vtPresent);
		if (vtPresent)
		{
			SteamOverlay::CreateHook(vtPresent, (uintptr_t)hkPresent, (long long*)&oPresent);
			DebugPrint("CreateHook invoked (vtable fallback).");
		}
		else
		{
			DebugPrint("Failed to obtain Present address via dummy swap chain.");
		}
	}

	if (oPresent == NULL)
	{
		DebugPrint("Hook attempted but original Present (oPresent) is NULL.");
	}
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) 
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
		HANDLE hThread = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
		if (hThread) CloseHandle(hThread);
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
		if (ImGuiInit)
		{
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
		}
		if (g_WndProcHooked && oWndProc && hWnd)
		{
			SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
		}
		CleanupRenderTarget();
	}
	return TRUE;
}