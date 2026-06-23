#include "App.h"
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    try {
        return RunClientApp();
    } catch (...) {
        return -1;
    }
}
