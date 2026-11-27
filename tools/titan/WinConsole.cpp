// Aparte de main.cpp: <windows.h> define su propio UUID, que choca con titan::UUID.
#if defined(_WIN32)
#include <windows.h>
#endif

// true si el programa se abrió con doble clic desde el Explorador de Windows: entonces la consola es solo de este
// proceso (en una terminal la comparte con ella) y se cerraría en cuanto termine.
bool openedByDoubleClick() {
#if defined(_WIN32)
    DWORD ids[2];
    return GetConsoleProcessList(ids, 2) <= 1;
#else
    return false;
#endif
}
