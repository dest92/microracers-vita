// Small PE32 guest for the dx library template. It exercises real imports;
// the template host maps these imports to the portable core APIs.
#define DIRECTINPUT_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <dsound.h>
#include <dinput.h>

int WINAPI hello_entry(void) {
    HRESULT ddraw = DirectDrawCreate(NULL, NULL, NULL);
    HRESULT dsound = DirectSoundCreate(NULL, NULL, NULL);
    HRESULT dinput = DirectInputCreateA(NULL, DIRECTINPUT_VERSION, NULL, NULL);
    return FAILED(ddraw) || FAILED(dsound) || FAILED(dinput) ? 1 : 0;
}
