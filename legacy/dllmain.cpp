#include "MusicLoungeBand.h"
#include <shlobj.h>
#include <strsafe.h>

HINSTANCE g_hInst = nullptr;
long      g_cDllRef = 0;

// {C7E3A129-4F8B-4D52-B6A0-3E9F1C245D87}
extern const CLSID CLSID_MusicLoungeBand =
    { 0xc7e3a129, 0x4f8b, 0x4d52, { 0xb6, 0xa0, 0x3e, 0x9f, 0x1c, 0x24, 0x5d, 0x87 } };

static const wchar_t* CLSID_STR    = L"{C7E3A129-4F8B-4D52-B6A0-3E9F1C245D87}";
static const wchar_t* BAND_NAME    = L"Music Lounge";

// ---------------------------------------------------------------------------
// Class factory
// ---------------------------------------------------------------------------
class MusicLoungeClassFactory : public IClassFactory {
    long m_ref = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        long r = InterlockedDecrement(&m_ref); if (!r) delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        MusicLoungeBand* band = new (std::nothrow) MusicLoungeBand();
        if (!band) return E_OUTOFMEMORY;
        HRESULT hr = band->QueryInterface(riid, ppv);
        band->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override {
        if (fLock) InterlockedIncrement(&g_cDllRef);
        else       InterlockedDecrement(&g_cDllRef);
        return S_OK;
    }
};

// ---------------------------------------------------------------------------
// DLL entry points
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    *ppv = nullptr;
    if (rclsid != CLSID_MusicLoungeBand) return CLASS_E_CLASSNOTAVAILABLE;
    MusicLoungeClassFactory* factory = new (std::nothrow) MusicLoungeClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return (g_cDllRef == 0) ? S_OK : S_FALSE;
}

// ---------------------------------------------------------------------------
// Registration helpers
// ---------------------------------------------------------------------------
static HRESULT SetRegStr(HKEY hRoot, const wchar_t* subKey, const wchar_t* name, const wchar_t* val) {
    HKEY hk;
    LONG r = RegCreateKeyExW(hRoot, subKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hk, nullptr);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    r = RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val)+1)*sizeof(wchar_t)));
    RegCloseKey(hk);
    return HRESULT_FROM_WIN32(r);
}

STDAPI DllRegisterServer() {
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(g_hInst, dllPath, MAX_PATH);

    wchar_t clsidKey[128];
    StringCchPrintfW(clsidKey, ARRAYSIZE(clsidKey),
        L"Software\\Classes\\CLSID\\%s", CLSID_STR);
    wchar_t inprocKey[192];
    StringCchPrintfW(inprocKey, ARRAYSIZE(inprocKey),
        L"Software\\Classes\\CLSID\\%s\\InprocServer32", CLSID_STR);
    // CATID_DeskBand — this is what Explorer reads to populate the Toolbars menu
    wchar_t catKey[256];
    StringCchPrintfW(catKey, ARRAYSIZE(catKey),
        L"Software\\Classes\\CLSID\\%s\\Implemented Categories\\{00021492-0000-0000-C000-000000000046}",
        CLSID_STR);

    HRESULT hr = SetRegStr(HKEY_LOCAL_MACHINE, clsidKey,  nullptr, BAND_NAME);
    if (FAILED(hr)) return hr;

    hr = SetRegStr(HKEY_LOCAL_MACHINE, inprocKey, nullptr, dllPath);
    if (FAILED(hr)) return hr;

    hr = SetRegStr(HKEY_LOCAL_MACHINE, inprocKey, L"ThreadingModel", L"Apartment");
    if (FAILED(hr)) return hr;

    hr = SetRegStr(HKEY_LOCAL_MACHINE, catKey, nullptr, L"");
    if (FAILED(hr)) return hr;

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    wchar_t clsidKey[128];
    StringCchPrintfW(clsidKey, ARRAYSIZE(clsidKey), L"Software\\Classes\\CLSID\\%s", CLSID_STR);

    RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKey);
    RegDeleteTreeW(HKEY_CURRENT_USER,  clsidKey);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
