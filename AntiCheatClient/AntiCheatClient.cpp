#include <Windows.h>
#include <format>
#include <stdio.h>
#include <winnt.h>
// 20 minutes wasted cause this was placed above windows.h. lame
#include "../AntiCheatDriver/Common.h"

int reportError(const char *message);

int main() {
  HANDLE device =
      ::CreateFile(L"\\\\.\\AntiCheat", GENERIC_READ | GENERIC_WRITE, 0,
                   nullptr, OPEN_EXISTING, 0, nullptr);
  if (device == INVALID_HANDLE_VALUE) {
    return reportError("Failed to open device");
  }

  /* IoControl */
  struct MyInfo payload = {0};
  DWORD bytesReturned;
  auto status =
      DeviceIoControl(device, IOCTL_ANTI_CHEAT_SCAN_HOOK, &payload, sizeof(payload), nullptr,
                      0, &bytesReturned, nullptr);
  if (status)
    reportError("Got info");
  else
    return reportError("Failed to communicate with driver");

  std::string msg = std::format("kbdclass .text starts at: {}, ends at: {}. "
                                "Dvice control dispatch routine at: {}",
                                payload.start, payload.end, payload.deviceControl);
  MessageBoxA(0, msg.c_str(), "Anti Cheat", 0);

  ::CloseHandle(device);
}

int reportError(const char *message) {
  printf("%s (error=%d)\n", message, GetLastError());
  return 1;
}