// EvolveOS_MenuProxy - Windows 11 Modern Context Menu Handler
// Copyright (c) 2026 EvolveOS Software

#include <windows.h>
#include <shobjidl_core.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <string>
#include <vector>
#include <fstream>
#include <shlobj.h>
#include <shlwapi.h>
#include <sstream> 

#pragma comment(lib, "shlwapi.lib")

using namespace Microsoft::WRL;

// 🚀 UNIQUE CLSID
// {24E9458D-6C4C-44CD-A2F1-2AC32A7DEC73}
const GUID CLSID_EvolveOSMenu = { 0x24e9458d, 0x6c4c, 0x44cd, { 0xa2, 0xf1, 0x2a, 0xc3, 0x2a, 0x7d, 0xec, 0x73 } };

long g_cRef = 0;

struct MenuItem {
    std::wstring Title;
    std::wstring ExePath;
    std::wstring Arguments;
    std::wstring Icon;
    std::wstring Target;
};

std::wstring ExtractJsonValue(const std::wstring& json, const std::wstring& key, size_t startPos = 0) {
    std::wstring search = L"\"" + key + L"\"";
    size_t pos = json.find(search, startPos);
    if (pos == std::wstring::npos) return L"";
    pos = json.find(L":", pos);
    if (pos == std::wstring::npos) return L"";
    size_t startQuote = json.find(L"\"", pos);
    if (startQuote == std::wstring::npos) return L"";
    size_t endQuote = json.find(L"\"", startQuote + 1);
    if (endQuote == std::wstring::npos) return L"";

    std::wstring val = json.substr(startQuote + 1, endQuote - startQuote - 1);

    size_t replacePos = 0;
    while ((replacePos = val.find(L"\\\\", replacePos)) != std::wstring::npos) {
        val.replace(replacePos, 2, L"\\");
        replacePos += 1;
    }
    return val;
}

std::vector<MenuItem> LoadCustomItems() {
    std::vector<MenuItem> items;
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        std::wstring wPath = std::wstring(localAppData) + L"\\EvolveOS_Optimizer\\ModernContextMenu.json";

        std::ifstream file(wPath.c_str());

        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string utf8Content = buffer.str();

            if (!utf8Content.empty()) {
                int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8Content[0], (int)utf8Content.size(), NULL, 0);
                std::wstring content(size_needed, 0);
                MultiByteToWideChar(CP_UTF8, 0, &utf8Content[0], (int)utf8Content.size(), &content[0], size_needed);

                size_t pos = 0;
                while ((pos = content.find(L"{", pos)) != std::wstring::npos) {
                    MenuItem item;
                    item.Title = ExtractJsonValue(content, L"title", pos);
                    item.ExePath = ExtractJsonValue(content, L"exePath", pos);
                    item.Arguments = ExtractJsonValue(content, L"arguments", pos);
                    item.Icon = ExtractJsonValue(content, L"icon", pos);
                    item.Target = ExtractJsonValue(content, L"target", pos);

                    if (!item.Title.empty() && !item.ExePath.empty()) {
                        items.push_back(item);
                    }

                    size_t nextPos = content.find(L"}", pos);
                    if (nextPos == std::wstring::npos) break;
                    pos = nextPos + 1;
                }
            }
        }
    }

    if (items.empty()) {
        MenuItem fallback;
        fallback.Title = L"EvolveOS (No Actions Configured)";
        fallback.ExePath = L"cmd.exe";
        items.push_back(fallback);
    }

    return items;
}

class CEvolveOSCommand : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IExplorerCommand>
{
public:
    CEvolveOSCommand(bool isRoot, const MenuItem& item = MenuItem()) : m_isRoot(isRoot), m_item(item) {
        InterlockedIncrement(&g_cRef);
    }
    ~CEvolveOSCommand() { InterlockedDecrement(&g_cRef); }

    IFACEMETHODIMP GetTitle(IShellItemArray* psiItemArray, LPWSTR* ppszName) override {
        if (m_isRoot) return SHStrDupW(L"EvolveOS Custom Actions", ppszName);
        return SHStrDupW(m_item.Title.c_str(), ppszName);
    }

    IFACEMETHODIMP GetIcon(IShellItemArray* psiItemArray, LPWSTR* ppszIcon) override {
        if (m_isRoot) {

            return SHStrDupW(L"imageres.dll,-114", ppszIcon);
        }
        return SHStrDupW(m_item.Icon.c_str(), ppszIcon);
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray* psiItemArray, LPWSTR* ppszInfotip) override { return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* pguidCommandName) override { return E_NOTIMPL; }

    IFACEMETHODIMP GetState(IShellItemArray* psiItemArray, BOOL fOkToBeSlow, EXPCMDSTATE* pCmdState) override {
        *pCmdState = ECS_ENABLED;
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) override {
        *pFlags = m_isRoot ? ECF_HASSUBCOMMANDS : ECF_DEFAULT;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) override;

    IFACEMETHODIMP Invoke(IShellItemArray* psiItemArray, IBindCtx* pbc) override {
        if (m_isRoot) return S_OK;

        std::wstring finalArgs = m_item.Arguments;
        std::wstring targetPath = L"";

        if (psiItemArray) {
            DWORD count = 0;
            psiItemArray->GetCount(&count);
            if (count > 0) {
                ComPtr<IShellItem> item;
                if (SUCCEEDED(psiItemArray->GetItemAt(0, &item))) {
                    LPWSTR pszName = NULL;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pszName))) {
                        targetPath = pszName;
                        CoTaskMemFree(pszName);
                    }
                }
            }
        }

        size_t paramPos = finalArgs.find(L"%1");
        if (paramPos != std::wstring::npos) {
            finalArgs.replace(paramPos, 2, targetPath);
        }

        ShellExecuteW(NULL, L"open", m_item.ExePath.c_str(), finalArgs.c_str(), NULL, SW_SHOWNORMAL);
        return S_OK;
    }

private:
    bool m_isRoot;
    MenuItem m_item;
};

class CEnumCommands : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IEnumExplorerCommand>
{
public:
    CEnumCommands() : m_current(0) {
        auto jsonItems = LoadCustomItems();
        for (const auto& i : jsonItems) {
            m_items.push_back(Make<CEvolveOSCommand>(false, i));
        }
        InterlockedIncrement(&g_cRef);
    }
    ~CEnumCommands() { InterlockedDecrement(&g_cRef); }

    IFACEMETHODIMP Next(ULONG celt, IExplorerCommand** pUICommand, ULONG* pceltFetched) override {
        ULONG fetched = 0;
        for (ULONG i = 0; i < celt && m_current < m_items.size(); ++i) {
            m_items[m_current].CopyTo(&pUICommand[i]);
            m_current++;
            fetched++;
        }
        if (pceltFetched) *pceltFetched = fetched;
        return (fetched == celt) ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Skip(ULONG celt) override { m_current += celt; return S_OK; }
    IFACEMETHODIMP Reset() override { m_current = 0; return S_OK; }
    IFACEMETHODIMP Clone(IEnumExplorerCommand** ppenum) override { return E_NOTIMPL; }

private:
    std::vector<ComPtr<IExplorerCommand>> m_items;
    size_t m_current;
};

IFACEMETHODIMP CEvolveOSCommand::EnumSubCommands(IEnumExplorerCommand** ppEnum) {
    if (m_isRoot) {
        *ppEnum = Make<CEnumCommands>().Detach();
        return S_OK;
    }
    *ppEnum = nullptr;
    return E_NOTIMPL;
}

class CEvolveOSClassFactory : public IClassFactory
{
    long m_refCount = 1;
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_refCount);
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        long ref = InterlockedDecrement(&m_refCount);
        if (ref == 0) delete this;
        return ref;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (pUnkOuter != NULL) return CLASS_E_NOAGGREGATION;
        auto command = Make<CEvolveOSCommand>(true);
        return command->QueryInterface(riid, ppv);
    }

    IFACEMETHODIMP LockServer(BOOL fLock) override { return S_OK; }
};

#pragma comment(linker, "/EXPORT:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow,PRIVATE")

STDAPI DllGetClassObject(REFIID rclsid, REFIID riid, void** ppv) {
    if (rclsid == CLSID_EvolveOSMenu) {
        CEvolveOSClassFactory* pFactory = new CEvolveOSClassFactory();
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return g_cRef == 0 ? S_OK : S_FALSE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}