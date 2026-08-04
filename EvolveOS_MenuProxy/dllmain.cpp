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
#include <algorithm>

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

    bool Extended;
    std::wstring SpecificExtension;
    std::wstring Position;

    bool IsSubMenu;
    std::vector<MenuItem> SubItems;
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

bool ExtractJsonBool(const std::wstring& json, const std::wstring& key, size_t startPos = 0) {
    std::wstring search = L"\"" + key + L"\":";
    size_t pos = json.find(search, startPos);
    if (pos == std::wstring::npos) {
        search = L"\"" + key + L"\" :";
        pos = json.find(search, startPos);
        if (pos == std::wstring::npos) return false;
    }
    size_t valPos = pos + search.length();
    while (valPos < json.length() && (json[valPos] == L' ' || json[valPos] == L'\t')) valPos++;
    if (valPos + 4 <= json.length() && json.substr(valPos, 4) == L"true") return true;
    return false;
}

std::wstring ExtractJsonArray(const std::wstring& json, const std::wstring& key, size_t startPos = 0) {
    std::wstring search = L"\"" + key + L"\":";
    size_t pos = json.find(search, startPos);
    if (pos == std::wstring::npos) {
        search = L"\"" + key + L"\" :";
        pos = json.find(search, startPos);
        if (pos == std::wstring::npos) return L"";
    }

    size_t bracketPos = json.find(L"[", pos);
    if (bracketPos == std::wstring::npos) return L"";

    int depth = 0;
    size_t endPos = std::wstring::npos;
    for (size_t i = bracketPos; i < json.length(); ++i) {
        if (json[i] == L'[') depth++;
        else if (json[i] == L']') {
            depth--;
            if (depth == 0) {
                endPos = i;
                break;
            }
        }
    }

    if (endPos != std::wstring::npos) {
        return json.substr(bracketPos, endPos - bracketPos + 1);
    }
    return L"";
}

std::vector<MenuItem> ParseItems(const std::wstring& jsonArray) {
    std::vector<MenuItem> items;
    size_t pos = 0;

    while (pos < jsonArray.length()) {
        size_t objStart = jsonArray.find(L"{", pos);
        if (objStart == std::wstring::npos) break;

        int depth = 0;
        size_t objEnd = std::wstring::npos;
        for (size_t i = objStart; i < jsonArray.length(); ++i) {
            if (jsonArray[i] == L'{') depth++;
            else if (jsonArray[i] == L'}') {
                depth--;
                if (depth == 0) {
                    objEnd = i;
                    break;
                }
            }
        }

        if (objEnd == std::wstring::npos) break;

        std::wstring objJson = jsonArray.substr(objStart, objEnd - objStart + 1);

        MenuItem item;
        item.Title = ExtractJsonValue(objJson, L"title");
        item.ExePath = ExtractJsonValue(objJson, L"exePath");
        item.Arguments = ExtractJsonValue(objJson, L"arguments");
        item.Icon = ExtractJsonValue(objJson, L"icon");
        item.Target = ExtractJsonValue(objJson, L"target");

        item.Extended = ExtractJsonBool(objJson, L"extended");
        item.SpecificExtension = ExtractJsonValue(objJson, L"specificExtension");
        item.Position = ExtractJsonValue(objJson, L"position");

        item.IsSubMenu = ExtractJsonBool(objJson, L"isSubMenu");

        if (item.IsSubMenu) {
            std::wstring subArr = ExtractJsonArray(objJson, L"subItems");
            if (!subArr.empty()) {
                item.SubItems = ParseItems(subArr);
            }
        }

        if (!item.Title.empty() && (!item.ExePath.empty() || item.IsSubMenu)) {
            items.push_back(item);
        }

        pos = objEnd + 1;
    }
    return items;
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

                std::wstring itemsArray = ExtractJsonArray(content, L"items");
                if (!itemsArray.empty()) {
                    items = ParseItems(itemsArray);
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

class CEnumCommands;

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

        if (!m_isRoot) {

            if (m_item.Extended) {
                if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0) {
                    *pCmdState = ECS_HIDDEN;
                    return S_OK;
                }
            }

            if (psiItemArray) {
                DWORD count = 0;
                psiItemArray->GetCount(&count);

                if (count > 0) {
                    ComPtr<IShellItem> shellItem;
                    if (SUCCEEDED(psiItemArray->GetItemAt(0, &shellItem))) {

                        SFGAOF attribs;
                        if (SUCCEEDED(shellItem->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attribs))) {
                            bool isFolder = (attribs & SFGAO_FOLDER) != 0;

                            if (m_item.Target == L"Files" && isFolder) {
                                *pCmdState = ECS_HIDDEN;
                                return S_OK;
                            }
                            if (m_item.Target == L"Folders" && !isFolder) {
                                *pCmdState = ECS_HIDDEN;
                                return S_OK;
                            }
                        }

                        if (!m_item.SpecificExtension.empty() && m_item.SpecificExtension != L"*") {
                            LPWSTR pszName = NULL;
                            if (SUCCEEDED(shellItem->GetDisplayName(SIGDN_NORMALDISPLAY, &pszName))) {
                                std::wstring fileName = pszName;
                                CoTaskMemFree(pszName);

                                size_t dotPos = fileName.find_last_of(L".");
                                if (dotPos != std::wstring::npos) {
                                    std::wstring ext = fileName.substr(dotPos);
                                    if (StrStrIW(m_item.SpecificExtension.c_str(), ext.c_str()) == NULL) {
                                        *pCmdState = ECS_HIDDEN;
                                        return S_OK;
                                    }
                                }
                                else {
                                    *pCmdState = ECS_HIDDEN;
                                    return S_OK;
                                }
                            }
                        }
                    }
                }
            }
        }

        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) override {
        *pFlags = (m_isRoot || m_item.IsSubMenu) ? ECF_HASSUBCOMMANDS : ECF_DEFAULT;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) override;

    IFACEMETHODIMP Invoke(IShellItemArray* psiItemArray, IBindCtx* pbc) override {
        if (m_isRoot || m_item.IsSubMenu) return S_OK;

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
    CEnumCommands(const std::vector<MenuItem>& jsonItems) : m_current(0) {

        std::vector<MenuItem> sortedItems = jsonItems;
        std::sort(sortedItems.begin(), sortedItems.end(), [](const MenuItem& a, const MenuItem& b) {
            auto getSortValue = [](const std::wstring& pos) {
                if (pos == L"Top") return 0;
                if (pos == L"Bottom") return 2;
                return 1;
                };
            return getSortValue(a.Position) < getSortValue(b.Position);
            });

        for (const auto& i : sortedItems) {
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
        *ppEnum = Make<CEnumCommands>(LoadCustomItems()).Detach();
        return S_OK;
    }
    if (m_item.IsSubMenu) {
        *ppEnum = Make<CEnumCommands>(m_item.SubItems).Detach();
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