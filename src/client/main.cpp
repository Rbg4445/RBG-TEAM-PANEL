#include "App.h"

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    try {
        return RunClientApp();
    } catch (...) {
        return -1;
    }
}
#else
int main(int argc, char* argv[]) {
    try {
        return RunClientApp();
    } catch (...) {
        return -1;
    }
}
#endif
