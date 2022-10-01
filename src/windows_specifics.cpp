#include <windows.h>
#include <string>

namespace korrelator {
  void ShowNonQtMessageBox(char const *title, char const *message) {
    MessageBoxA(nullptr, message, title, MB_OK | MB_ICONEXCLAMATION);
  }

  bool addKeyToRegistryPath(char const * const parentPath, char const * const newPath) {
    std::string const finalPath = std::string(parentPath) + "\\" + newPath;

    HKEY hKey = HKEY_LOCAL_MACHINE;
    char const * const subKey = finalPath.c_str();
    DWORD reservedWord = 0;
    char* lpClass = nullptr;
    DWORD dwOptions = REG_OPTION_NON_VOLATILE;
    REGSAM samRequired = KEY_WRITE;
    LPSECURITY_ATTRIBUTES const securityAttributes = nullptr;

    HKEY outResult;
    DWORD dwDisposition = 0;

    auto const result =
        RegCreateKeyExA(hKey, subKey, reservedWord, lpClass, dwOptions, samRequired,
                        securityAttributes, &outResult, &dwDisposition);
    return result == ERROR_SUCCESS;
  }
}
