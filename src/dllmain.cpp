#include <windows.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>
#include <string>
#include <list>
#include <intrin.h>
#include <DirectXMath.h>

#include "dinput/IDirectInput7A.h"
#include "keymapping.h"

#include "hook.h"

#pragma comment(lib, "shlwapi.lib")

bool IsG108K = false;
bool IsG112F = false;
bool IsG2A26F = false;

bool IsEnabledUnionWrapper = false;
bool IsRegistered = false;
bool IsKeyboardAcquired = false;
bool IsMouseAcquired = false;
bool IsJoystickAcquired = false;
bool IsWindowActive = true;
bool UseRawInput = true;

bool g_keyboardButtonState[256] = {};

std::list<RAWKEYBOARD> g_lastKeyboardEvents;
std::list<RAWMOUSE> g_lastMouseEvents;

HWND g_gothicHWND;
WNDPROC g_originalWndProc;
m_IDirectInput7A* g_directInput;
RECT g_clippedRect;

bool g_UseAccumulation = true;
float g_SpeedMultiplierX = 1.0f;
float g_SpeedMultiplierY = 1.0f;
float g_InternalMultiplierX = 1.0f;
float g_InternalMultiplierY = 1.0f;

bool NeedToWarpMouse()
{
	if(GetModuleHandleA("UNION_ADV_INVENTORY.DLL"))
	{
		// Handle union advanced inventory
		if(IsG108K)
		{
			DWORD player = *reinterpret_cast<DWORD*>(0x8DBBB0);
			if(player && reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(player + 0x550) + 0x1C))(player + 0x550))
				return false;
		}
		else if(IsG2A26F)
		{
			DWORD player = *reinterpret_cast<DWORD*>(0xAB2684);
			if(player && reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(player + 0x668) + 0x2C))(player + 0x668))
				return false;
		}
	}
	if(IsG2A26F && GetModuleHandleA("UNION_ABI.DLL"))
	{
		// Handle New Balance crafting menu
		DWORD screen = *reinterpret_cast<DWORD*>(0xAB6468);
		DWORD child = *reinterpret_cast<DWORD*>(screen + 0x38);
		while(child)
		{
			static const char searchChild[] = "AS_CRAFT_CURSOR.TGA";
			if(*reinterpret_cast<int*>(child + 0xB0) && *reinterpret_cast<DWORD*>(child + 0x40))
			{
				DWORD texName = reinterpret_cast<DWORD(__thiscall*)(DWORD)>(0x5A9CD0)(*reinterpret_cast<DWORD*>(child + 0x40));
				if(texName && *reinterpret_cast<int*>(texName + 0x0C) == sizeof(searchChild) - 1)
				{
					if(_stricmp(*reinterpret_cast<const char**>(texName + 0x08), searchChild) == 0)
						return false;
				}
			}
			child = *reinterpret_cast<DWORD*>(child + 0x0C);
		}
	}
	return true;
}

void ClearKeyBuffer()
{
	for(int i = 0; i < 256; ++i)
	{
		if(g_keyboardButtonState[i])
		{
			for(auto& it : VK_Keys_Map)
			{
				if(it.second == i)
				{
					RAWKEYBOARD keyboardKey;
					keyboardKey.Message = WM_KEYUP;
					keyboardKey.VKey = static_cast<USHORT>(it.first);
					g_lastKeyboardEvents.push_back(keyboardKey);
					break;
				}
			}
			g_keyboardButtonState[i] = false;
		}
	}
}

void UpdateClipCursor(HWND hwnd)
{
	RECT rect;
	if(IsWindowActive)
	{
		GetClientRect(hwnd, &rect);
		ClientToScreen(hwnd, reinterpret_cast<LPPOINT>(&rect) + 0);
		ClientToScreen(hwnd, reinterpret_cast<LPPOINT>(&rect) + 1);
		if(ClipCursor(&rect))
			g_clippedRect = rect;
	}
	else
	{
		if(GetClipCursor(&rect) && memcmp(&rect, &g_clippedRect, sizeof(RECT)) == 0)
		{
			ClipCursor(nullptr);
			ZeroMemory(&g_clippedRect, sizeof(RECT));
		}
	}
}

LRESULT CALLBACK RawInputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if(msg == WM_INPUT && UseRawInput)
	{
		UINT dwSize = 0;
		GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));

		LPBYTE rawData = new BYTE[dwSize];
		if(rawData == NULL)
			return 0;

		GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawData, &dwSize, sizeof(RAWINPUTHEADER));

		RAWINPUT* rinput = reinterpret_cast<RAWINPUT*>(rawData);
		if(rinput->header.dwType == RIM_TYPEMOUSE && IsMouseAcquired)
		{
			RAWMOUSE& rawMouse = rinput->data.mouse;
			g_lastMouseEvents.push_back(rawMouse);
		}
	}
	else if((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && IsKeyboardAcquired)
	{
		WPARAM new_vk = wParam;
		bool extended = (lParam & 0x01000000) != 0;
		switch(new_vk)
		{
			case VK_SHIFT: new_vk = MapVirtualKeyA((lParam & 0x00FF0000) >> 16, MAPVK_VSC_TO_VK_EX); break;
			case VK_CONTROL: new_vk = (extended ? VK_RCONTROL : VK_LCONTROL); break;
			case VK_MENU: new_vk = (extended ? VK_RMENU : VK_LMENU); break;
			case VK_RETURN: new_vk = (extended ? 0xE8 : VK_RETURN); break;
			case VK_INSERT: new_vk = (extended ? VK_INSERT : VK_NUMPAD0); break;
			case VK_END: new_vk = (extended ? VK_END : VK_NUMPAD1); break;
			case VK_DOWN: new_vk = (extended ? VK_DOWN : VK_NUMPAD2); break;
			case VK_NEXT: new_vk = (extended ? VK_NEXT : VK_NUMPAD3); break;
			case VK_LEFT: new_vk = (extended ? VK_LEFT : VK_NUMPAD4); break;
			case VK_CLEAR: new_vk = (extended ? VK_CLEAR : VK_NUMPAD5); break;
			case VK_RIGHT: new_vk = (extended ? VK_RIGHT : VK_NUMPAD6); break;
			case VK_HOME: new_vk = (extended ? VK_HOME : VK_NUMPAD7); break;
			case VK_UP: new_vk = (extended ? VK_UP : VK_NUMPAD8); break;
			case VK_PRIOR: new_vk = (extended ? VK_PRIOR : VK_NUMPAD9); break;
			case VK_DELETE: new_vk = (extended ? VK_DELETE : VK_DECIMAL); break;
		}

		RAWKEYBOARD keyboardKey;
		keyboardKey.Message = WM_KEYDOWN;
		keyboardKey.VKey = static_cast<USHORT>(new_vk);
		g_lastKeyboardEvents.push_back(keyboardKey);
	}
	else if((msg == WM_KEYUP || msg == WM_SYSKEYUP) && IsKeyboardAcquired)
	{
		WPARAM new_vk = wParam;
		bool extended = (lParam & 0x01000000) != 0;
		switch(new_vk)
		{
			case VK_SHIFT: new_vk = MapVirtualKeyA((lParam & 0x00FF0000) >> 16, MAPVK_VSC_TO_VK_EX); break;
			case VK_CONTROL: new_vk = (extended ? VK_RCONTROL : VK_LCONTROL); break;
			case VK_MENU: new_vk = (extended ? VK_RMENU : VK_LMENU); break;
			case VK_RETURN: new_vk = (extended ? 0xE8 : VK_RETURN); break;
			case VK_INSERT: new_vk = (extended ? VK_INSERT : VK_NUMPAD0); break;
			case VK_END: new_vk = (extended ? VK_END : VK_NUMPAD1); break;
			case VK_DOWN: new_vk = (extended ? VK_DOWN : VK_NUMPAD2); break;
			case VK_NEXT: new_vk = (extended ? VK_NEXT : VK_NUMPAD3); break;
			case VK_LEFT: new_vk = (extended ? VK_LEFT : VK_NUMPAD4); break;
			case VK_CLEAR: new_vk = (extended ? VK_CLEAR : VK_NUMPAD5); break;
			case VK_RIGHT: new_vk = (extended ? VK_RIGHT : VK_NUMPAD6); break;
			case VK_HOME: new_vk = (extended ? VK_HOME : VK_NUMPAD7); break;
			case VK_UP: new_vk = (extended ? VK_UP : VK_NUMPAD8); break;
			case VK_PRIOR: new_vk = (extended ? VK_PRIOR : VK_NUMPAD9); break;
			case VK_DELETE: new_vk = (extended ? VK_DELETE : VK_DECIMAL); break;
		}

		RAWKEYBOARD keyboardKey;
		keyboardKey.Message = WM_KEYUP;
		keyboardKey.VKey = static_cast<USHORT>(new_vk);
		g_lastKeyboardEvents.push_back(keyboardKey);
	}
	else if(msg == WM_ACTIVATE)
	{
		// Don't mark the window as active if it's activated before being shown
		if(IsWindowVisible(hwnd))
		{
			BOOL minimized = HIWORD(wParam);
			if(!minimized && LOWORD(wParam) != WA_INACTIVE)
				IsWindowActive = true;
			else
				IsWindowActive = false;

			UpdateClipCursor(hwnd);
			ClearKeyBuffer();
		}
	}
	else if(msg == WM_WINDOWPOSCHANGED)
		UpdateClipCursor(hwnd);

	if(!UseRawInput && IsMouseAcquired)
	{
		switch(msg)
		{
			case WM_LBUTTONDOWN:
			{
				RAWMOUSE rawMouse = {};
				rawMouse.usButtonFlags = RI_MOUSE_BUTTON_1_DOWN;
				g_lastMouseEvents.push_back(rawMouse);
			}
			break;
			case WM_LBUTTONUP:
			{
				RAWMOUSE rawMouse = {};
				rawMouse.usButtonFlags = RI_MOUSE_BUTTON_1_UP;
				g_lastMouseEvents.push_back(rawMouse);
			}
			break;
			case WM_RBUTTONDOWN:
			{
				RAWMOUSE rawMouse = {};
				rawMouse.usButtonFlags = RI_MOUSE_BUTTON_2_DOWN;
				g_lastMouseEvents.push_back(rawMouse);
			}
			break;
			case WM_RBUTTONUP:
			{
				RAWMOUSE rawMouse = {};
				rawMouse.usButtonFlags = RI_MOUSE_BUTTON_2_UP;
				g_lastMouseEvents.push_back(rawMouse);
			}
			break;
			case WM_MBUTTONDOWN:
			{
				RAWMOUSE rawMouse = {};
				rawMouse.usButtonFlags = RI_MOUSE_BUTTON_3_DOWN;
				g_lastMouseEvents.push_back(rawMouse);
			}
			break;
			case WM_MBUTTONUP:
			{
				RAWMOUSE rawMouse = {};
				rawMouse.usButtonFlags = RI_MOUSE_BUTTON_3_UP;
				g_lastMouseEvents.push_back(rawMouse);
			}
			break;
			case WM_XBUTTONDOWN:
			{
				RAWMOUSE rawMouse = {};
				rawMouse.usButtonFlags = ((GET_XBUTTON_WPARAM(wParam) & XBUTTON2) ? RI_MOUSE_BUTTON_5_DOWN : RI_MOUSE_BUTTON_4_DOWN);
				g_lastMouseEvents.push_back(rawMouse);
			}
			break;
			case WM_XBUTTONUP:
			{
				RAWMOUSE rawMouse = {};
				rawMouse.usButtonFlags = ((GET_XBUTTON_WPARAM(wParam) & XBUTTON2) ? RI_MOUSE_BUTTON_5_UP : RI_MOUSE_BUTTON_4_UP);
				g_lastMouseEvents.push_back(rawMouse);
			}
			break;
			case WM_MOUSEWHEEL:
			{
				RAWMOUSE rawMouse = {};
				rawMouse.usButtonFlags = RI_MOUSE_WHEEL;
				rawMouse.usButtonData = GET_WHEEL_DELTA_WPARAM(wParam);
				g_lastMouseEvents.push_back(rawMouse);
			}
			break;
			case WM_MOUSEHWHEEL:
			{
				RAWMOUSE rawMouse = {};
				rawMouse.usButtonFlags = RI_MOUSE_HWHEEL;
				rawMouse.usButtonData = GET_WHEEL_DELTA_WPARAM(wParam);
				g_lastMouseEvents.push_back(rawMouse);
			}
			break;
		}
	}
	return CallWindowProcA(g_originalWndProc, hwnd, msg, wParam, lParam);
}

void InitRawInput(HWND hwnd)
{
	if(IsRegistered)
		return;

	g_gothicHWND = hwnd;
	g_originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(RawInputWndProc)));
	IsRegistered = true;
}

void InitMouseRawInput(HWND hwnd)
{
	InitRawInput(hwnd);
}

void AcquireMouseInput()
{
	if(!IsMouseAcquired)
	{
		while(ShowCursor(false) >= 0);
	}
	IsMouseAcquired = true;
}

void UnAcquireMouseInput()
{
	if(IsMouseAcquired)
	{
		while(ShowCursor(true) < 0);
	}
	IsMouseAcquired = false;
}

void InitKeyboardRawInput(HWND hwnd)
{
	InitRawInput(hwnd);
	UpdateClipCursor(hwnd);

	RAWINPUTDEVICE Rid;
	Rid.usUsagePage = 0x01;
	Rid.usUsage = 0x02;
	Rid.dwFlags = 0;
	Rid.hwndTarget = hwnd;
	if(RegisterRawInputDevices(&Rid, 1, sizeof(Rid)) == FALSE)
		UseRawInput = false;
}

void AcquireKeyboardInput()
{
	IsKeyboardAcquired = true;
}

void UnAcquireKeyboardInput()
{
	IsKeyboardAcquired = false;
}

void ProcessInputEvents()
{
	// Make sure we got window messages readed
	MSG msg;
	while(PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}

	if(!UseRawInput && IsMouseAcquired && IsWindowActive)
	{
		static int ILastX = 0, ILastY = 0;
		POINT pt = {0, 0};
		GetCursorPos(&pt);
		ClientToScreen(g_gothicHWND, &pt);
		int relativeX = pt.x - ILastX;
		int relativeY = pt.y - ILastY;
		if(NeedToWarpMouse())
		{
			POINT ptNew;
			RECT rect;
			if(GetClipCursor(&rect))
			{
				ptNew.x = rect.left + (rect.right / 2);
				ptNew.y = rect.top + (rect.bottom / 2);
			}
			else
			{
				GetClientRect(g_gothicHWND, &rect);
				ClientToScreen(g_gothicHWND, reinterpret_cast<LPPOINT>(&rect) + 0);
				ClientToScreen(g_gothicHWND, reinterpret_cast<LPPOINT>(&rect) + 1);
				ptNew.x = rect.left + (rect.right / 2);
				ptNew.y = rect.top + (rect.bottom / 2);
			}
			SetCursorPos(ptNew.x, ptNew.y);
			ILastX = ptNew.x;
			ILastY = ptNew.y;
		}
		else
		{
			ILastX = pt.x;
			ILastY = pt.y;
		}

		if(relativeX != 0 || relativeY != 0)
		{
			RAWMOUSE rawMouse = {};
			rawMouse.lLastX = relativeX;
			rawMouse.lLastY = relativeY;
			g_lastMouseEvents.push_back(rawMouse);
		}
	}

	if(IsWindowActive)
	{
		RECT rect;
		if(GetClipCursor(&rect) && memcmp(&rect, &g_clippedRect, sizeof(RECT)) != 0)
			UpdateClipCursor(g_gothicHWND);
	}
	else if(GetForegroundWindow() == g_gothicHWND)
	{
		// Just in case to check if somehow we didn't get informed the window got activated
		IsWindowActive = true;
		UpdateClipCursor(g_gothicHWND);
	}
}

int __fastcall FixSetViewPort_G2(DWORD zCRnd_D3D, DWORD _EDX, int x0, int y0, int width, int height)
{
	int gWidth = *reinterpret_cast<int*>(zCRnd_D3D + 0x98C);
	int gHeight = *reinterpret_cast<int*>(zCRnd_D3D + 0x990);
	int x1 = x0 + width;
	int y1 = y0 + height;
	if(x0 > gWidth || y0 > gHeight || x1 < 0 || y1 < 0)
	{
		x1 = 0;
		y1 = 0;
		x0 = 0;
		y0 = 0;
	}
	else
	{
		x1 = std::min<int>(x1, gWidth);
		y1 = std::min<int>(y1, gHeight);
		x0 = std::max<int>(x0, 0);
		y0 = std::max<int>(y0, 0);
	}

	DWORD viewPort[6] = {static_cast<DWORD>(x0), static_cast<DWORD>(y0), static_cast<DWORD>(x1 - x0), static_cast<DWORD>(y1 - y0), 0x00000000, 0x3F800000};
	DWORD device = *reinterpret_cast<DWORD*>(0x9FC9F4);
	if(reinterpret_cast<int(__stdcall*)(DWORD, DWORD*)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(device) + 0x34))(device, viewPort) < 0) return 0;
	return 1;
}

int __fastcall FixSetViewPort_G1(DWORD zCRnd_D3D, DWORD _EDX, int x0, int y0, int width, int height)
{
	int gWidth = *reinterpret_cast<int*>(zCRnd_D3D + 0x984);
	int gHeight = *reinterpret_cast<int*>(zCRnd_D3D + 0x988);
	int x1 = x0 + width;
	int y1 = y0 + height;
	if(x0 > gWidth || y0 > gHeight || x1 < 0 || y1 < 0)
	{
		x1 = 0;
		y1 = 0;
		x0 = 0;
		y0 = 0;
	}
	else
	{
		x1 = std::min<int>(x1, gWidth);
		y1 = std::min<int>(y1, gHeight);
		x0 = std::max<int>(x0, 0);
		y0 = std::max<int>(y0, 0);
	}

	DWORD viewPort[6] = {static_cast<DWORD>(x0), static_cast<DWORD>(y0), static_cast<DWORD>(x1 - x0), static_cast<DWORD>(y1 - y0), 0x00000000, 0x3F800000};
	DWORD device = *reinterpret_cast<DWORD*>(0x929D5C);
	if(reinterpret_cast<int(__stdcall*)(DWORD, DWORD*)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(device) + 0x34))(device, viewPort) < 0) return 0;
	return 1;
}

void __fastcall HookGetMousePos_G2(DWORD zCInput, DWORD _EDX, float& xpos, float& ypos, float& zpos)
{
	xpos = static_cast<float>(*reinterpret_cast<int*>(0x8D165C)) * (*reinterpret_cast<float*>(0x89A148)) * g_InternalMultiplierX;
	ypos = static_cast<float>(*reinterpret_cast<int*>(0x8D1660)) * (*reinterpret_cast<float*>(0x89A14C)) * g_InternalMultiplierY;
	zpos = static_cast<float>(*reinterpret_cast<int*>(0x8D1664));
	if(*reinterpret_cast<int*>(0x8D1D78)) xpos = -xpos;
	if(*reinterpret_cast<int*>(0x8D1D7C)) ypos = -ypos;
}

void __fastcall HookGetMousePos_G1(DWORD zCInput, DWORD _EDX, float& xpos, float& ypos, float& zpos)
{
	xpos = static_cast<float>(*reinterpret_cast<int*>(0x86CCAC)) * (*reinterpret_cast<float*>(0x836538)) * g_InternalMultiplierX;
	ypos = static_cast<float>(*reinterpret_cast<int*>(0x86CCB0)) * (*reinterpret_cast<float*>(0x83653C)) * g_InternalMultiplierY;
	zpos = static_cast<float>(*reinterpret_cast<int*>(0x86CCB4));
	if(*reinterpret_cast<int*>(0x86D304)) xpos = -xpos;
	if(*reinterpret_cast<int*>(0x86D308)) ypos = -ypos;
}

float __fastcall HookReadSmoothMouse(DWORD zCOptions, DWORD _EDX, DWORD section, DWORD option, float defValue)
{
	return 0.0f;
}

static int Init()
{
	DWORD baseAddr = reinterpret_cast<DWORD>(GetModuleHandleA(nullptr));

	// Check for gothic 2.6 fix
	if(*reinterpret_cast<DWORD*>(baseAddr + 0x168) == 0x3D4318 && *reinterpret_cast<DWORD*>(baseAddr + 0x3D43A0) == 0x82E108 && *reinterpret_cast<DWORD*>(baseAddr + 0x3D43CB) == 0x82E10C)
		IsG2A26F = true;

	// Check for gothic 1.12F
	if(*reinterpret_cast<DWORD*>(baseAddr + 0x140) == 0x3BE698 && *reinterpret_cast<DWORD*>(baseAddr + 0x3BE720) == 0x8131E4 && *reinterpret_cast<DWORD*>(baseAddr + 0x3BE74B) == 0x8131E8)
		IsG112F = true;

	// Check for gothic 1.08K
	if(*reinterpret_cast<DWORD*>(baseAddr + 0x160) == 0x37A8D8 && *reinterpret_cast<DWORD*>(baseAddr + 0x37A960) == 0x7D01E4 && *reinterpret_cast<DWORD*>(baseAddr + 0x37A98B) == 0x7D01E8)
		IsG108K = true;

	bool UseFixedStepCameraControl = true;
	{
		char executablePath[MAX_PATH];
		GetModuleFileNameA(GetModuleHandleA(nullptr), executablePath, sizeof(executablePath));
		PathRemoveFileSpecA(executablePath);
		SetCurrentDirectoryA(executablePath);
	}
	{
		std::string currentSector = "none";

		FILE* f;
		errno_t err = fopen_s(&f, "grawinput.ini", "r");
		if(err == 0)
		{
			char readedLine[1024];
			while(fgets(readedLine, sizeof(readedLine), f) != nullptr)
			{
				size_t len = strlen(readedLine);
				if(len > 0)
				{
					if(readedLine[len - 1] == '\n' || readedLine[len - 1] == '\r')
						len -= 1;
					if(len > 0)
					{
						if(readedLine[len - 1] == '\n' || readedLine[len - 1] == '\r')
							len -= 1;
					}
				}
				if(len == 0)
					continue;

				if(readedLine[0] == '[' && readedLine[len - 1] == ']')
				{
					currentSector = std::string(readedLine + 1, len - 2);
					std::transform(currentSector.begin(), currentSector.end(), currentSector.begin(), toupper);
				}
				else if(readedLine[0] != ';' && readedLine[0] != '/')
				{
					std::size_t eqpos;
					std::string rLine = std::string(readedLine, len);
					std::transform(rLine.begin(), rLine.end(), rLine.begin(), toupper);
					if((eqpos = rLine.find("=")) != std::string::npos)
					{
						std::string lhLine = rLine.substr(0, eqpos);
						std::string rhLine = rLine.substr(eqpos + 1);
						lhLine.erase(lhLine.find_last_not_of(' ') + 1);
						lhLine.erase(0, lhLine.find_first_not_of(' '));
						rhLine.erase(rhLine.find_last_not_of(' ') + 1);
						rhLine.erase(0, rhLine.find_first_not_of(' '));
						if(currentSector == "MOUSE")
						{
							if(lhLine == "USEACCUMULATION")
								g_UseAccumulation = (rhLine == "TRUE" || rhLine == "1");
							else if(lhLine == "USEFIXEDSTEPCAMERACONTROL")
								UseFixedStepCameraControl = (rhLine == "TRUE" || rhLine == "1");
							else if(lhLine == "SPEEDMULTIPLIERX")
							{
								try {g_SpeedMultiplierX = std::stof(rhLine);}
								catch(const std::exception&) {g_SpeedMultiplierX = 1.0f;}
							}
							else if(lhLine == "SPEEDMULTIPLIERY")
							{
								try {g_SpeedMultiplierY = std::stof(rhLine);}
								catch(const std::exception&) {g_SpeedMultiplierY = 1.0f;}
							}
						}
					}
				}
			}
			fclose(f);
		}
	}

	if(IsG2A26F && g_UseAccumulation)
	{
		WriteStack(0x4D3E50, "\x01");
		WriteStack(0x4D3E5C, "\x01");
	}
	else if(IsG108K && g_UseAccumulation)
	{
		WriteStack(0x4C7FEB, "\x01");
		WriteStack(0x4C7FF7, "\x01");
	}

	if(IsG2A26F && UseFixedStepCameraControl)
	{
		OverWriteFloat(0x57CA85, 16.f);
		OverWriteFloat(0x57CA89, 0.016f);
		WriteStack(0x699D97, "\xD9\x05\x85\xCA\x57\x00\x90\x90\x90\x90\x90\x90");
		WriteStack(0x6AE84B, "\x85\xCA\x57\x00");
		WriteStack(0x6AE86C, "\x90\x90\x90\x90");
		WriteStack(0x4A4D25, "\xD9\x05\x89\xCA\x57\x00\x90\x90\x90");
		WriteStack(0x4A4DDC, "\xD9\x05\x89\xCA\x57\x00\x90\x90\x90");
		WriteStack(0x4A4E61, "\xD9\x05\x89\xCA\x57\x00\x90\x90\x90");

		DWORD MouseRotationFunc = reinterpret_cast<DWORD>(VirtualAlloc(nullptr, 64, (MEM_RESERVE|MEM_COMMIT), PAGE_EXECUTE_READWRITE));
		if(MouseRotationFunc)
		{
			WriteStack(MouseRotationFunc, "\x55\x8B\xEC\xF3\x0F\x10\x05\xD8\xB3\x99\x00\xB8\x40\xE5\x6A\x00\xF3\x0F\x5E\x05\xD4\xB3\x99\x00\xFF\x75\x0C\xF3\x0F\x59\x45\x08\x51"
											"\xF3\x0F\x5E\x05\x85\xCA\x57\x00\xF3\x0F\x11\x04\x24\xFF\xD0\x5D\xC2\x08\x00");
			HookCall(0x69AAA5, MouseRotationFunc);
			HookCall(0x69AAE1, MouseRotationFunc);
			HookCall(0x69ABA3, MouseRotationFunc);
			HookCall(0x69ABDB, MouseRotationFunc);
		}
	}
	else if(IsG108K && UseFixedStepCameraControl)
	{
		OverWriteFloat(0x4266C4, 0.5f);
		OverWriteFloat(0x4266C8, 0.3f);
		OverWriteFloat(0x562CC5, 16.f);
		OverWriteFloat(0x562CC9, 0.016f);
		WriteStack(0x426664, "\xD8\x05\xC8\x66\x42\x00");
		WriteStack(0x426CBB, "\xD8\x05\xC8\x66\x42\x00");
		WriteStack(0x426656, "\xD8\x0D\xC4\x66\x42\x00");
		WriteStack(0x426CAD, "\xD8\x0D\xC4\x66\x42\x00");
		WriteStack(0x613711, "\xD9\x05\xC5\x2C\x56\x00");
		WriteStack(0x49D428, "\xD9\x05\xC9\x2C\x56\x00\x90\x90\x90");
		WriteStack(0x49D4E3, "\xD9\x05\xC9\x2C\x56\x00\x90\x90\x90");
		WriteStack(0x49D568, "\xD9\x05\xC9\x2C\x56\x00\x90\x90\x90");

		DWORD MouseRotationFunc = reinterpret_cast<DWORD>(VirtualAlloc(nullptr, 256, (MEM_RESERVE|MEM_COMMIT), PAGE_EXECUTE_READWRITE));
		if(MouseRotationFunc)
		{
			WriteStack(MouseRotationFunc, "\x55\x8B\xEC\x83\xEC\x08\x66\x0F\x6E\x0D\xAC\xCC\x86\x00\xA1\x00\x49\x61\x00\x0F\x5B\xC9\xF3\x0F\x10\x00\x33\xC0\xC7\x45\xFC\xFF\xFF"
											"\xFF\x3E\xF3\x0F\x59\x45\xFC\xF3\x0F\x59\x0D\x38\x65\x83\x00\xF3\x0F\x59\xC8\x0F\x28\xC1\xC7\x45\xFC\xFF\xFF\xFF\x7F\xC7\x45\xF8\x00"
											"\x00\x20\x40\xF3\x0F\x10\x7D\xFC\x0F\x54\xC7\x0F\x2F\x45\xF8\x0F\x97\xC0\xF3\x0F\x10\x05\xEC\xF1\x8C\x00\xF3\x0F\x11\x45\xFC\xF3\x0F"
											"\x10\x05\xE8\xF1\x8C\x00\xF3\x0F\x11\x45\xF8\xC7\x05\xEC\xF1\x8C\x00\x00\x00\x80\x41\xC7\x05\xE8\xF1\x8C\x00\x00\x00\x80\x3F\x50\x51"
											"\xF3\x0F\x11\x0C\x24\xB8\x30\x5A\x62\x00\xFF\xD0\xF3\x0F\x10\x45\xFC\xF3\x0F\x11\x05\xEC\xF1\x8C\x00\xF3\x0F\x10\x45\xF8\xF3\x0F\x11"
											"\x05\xE8\xF1\x8C\x00\x8B\xE5\x5D\xC2\x08\x00");
			HookCall(0x61490C, MouseRotationFunc);
			HookCall(0x614A17, MouseRotationFunc);
		}
	}

	if(IsG2A26F)
	{
		g_InternalMultiplierX = g_SpeedMultiplierX;
		g_InternalMultiplierY = g_SpeedMultiplierY;
		g_SpeedMultiplierX = 1.0f;
		g_SpeedMultiplierY = 1.0f;
		HookJMP(0x4D5730, reinterpret_cast<DWORD>(&HookGetMousePos_G2));
		HookCall(0x4D414F, reinterpret_cast<DWORD>(&HookReadSmoothMouse));
	}
	else if(IsG108K)
	{
		g_InternalMultiplierX = g_SpeedMultiplierX;
		g_InternalMultiplierY = g_SpeedMultiplierY;
		g_SpeedMultiplierX = 1.0f;
		g_SpeedMultiplierY = 1.0f;
		HookJMP(0x4C8BD0, reinterpret_cast<DWORD>(&HookGetMousePos_G1));
	}

	return 1;
}

extern "C"
{
	HRESULT WINAPI _DirectInputCreateEx(HINSTANCE, DWORD, REFIID, LPVOID* lplpDD, LPUNKNOWN)
	{
		g_directInput = new m_IDirectInput7A();
		*lplpDD = g_directInput;
		return DI_OK;
	}

	HRESULT WINAPI _DirectInputCreateA(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA* lplpDirectInput, LPUNKNOWN punkOuter)
	{
		return _DirectInputCreateEx(hinst, dwVersion, IID_IDirectInputA, (LPVOID*)lplpDirectInput, punkOuter);
	}

	HRESULT WINAPI _DirectInputCreateW(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTW* lplpDirectInput, LPUNKNOWN punkOuter)
	{
		return _DirectInputCreateEx(hinst, dwVersion, IID_IDirectInputW, (LPVOID*)lplpDirectInput, punkOuter);
	}

	HRESULT WINAPI _DllCanUnloadNow()
	{
		return DI_OK;
	}

	HRESULT WINAPI _DllGetClassObject(IN REFCLSID rclsid, IN REFIID, OUT LPVOID FAR* ppv)
	{
		HRESULT hr = DI_OK;
		if(rclsid == CLSID_DirectInput)
			*ppv = g_directInput;
		else
			hr = DIERR_GENERIC;

		return hr;
	}

	HRESULT WINAPI _DllRegisterServer()
	{
		return DI_OK;
	}

	HRESULT WINAPI _DllUnregisterServer()
	{
		return DI_OK;
	}
}

extern "C"
{
	BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
	{
		switch(dwReason)
		{
			case DLL_PROCESS_ATTACH:
				return Init();

			case DLL_THREAD_ATTACH:
			case DLL_THREAD_DETACH:
			case DLL_PROCESS_DETACH:
				break;
		}
		return 1;
	}
}

