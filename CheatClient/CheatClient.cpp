#include <iostream>
// ZeroBoi.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
//

#include <Windows.h>
#include <concepts>
#include <memory>
#include <stdio.h>
#include <type_traits>
#include <winnt.h>

int reportError(const char *message);

int main() {
  HANDLE device = ::CreateFile(L"\\\\.\\Cheat", GENERIC_READ | GENERIC_WRITE, 0,
                           nullptr, OPEN_EXISTING, 0, nullptr);
  if (device == INVALID_HANDLE_VALUE) {
    return reportError("Failed to open device");
  }

  /* Read Test */
  BYTE buf[8] = { 1 };

  DWORD bytes;
  BOOL ret = ::ReadFile(device, buf, sizeof(buf), &bytes, nullptr);
  if (!ret) {
    return reportError("Failed to read");
  }
  if (bytes != sizeof(buf)) {
    printf("Wrong number of bytes read\n");
  }

  printf("Number of calls: %lld", buf);

  // Try unique_ptr?
  ::CloseHandle(device);
}

int reportError(const char *message) {
  printf("%s (error=%d)\n", message, GetLastError());
  return 1;
}
