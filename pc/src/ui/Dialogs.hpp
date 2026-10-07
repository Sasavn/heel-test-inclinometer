#pragma once
// Окна Windows: выбор папки (IFileOpenDialog, Vista+), выбор файла (GetOpenFileNameW), открыть папку в Проводнике.
// Строки — UTF-8. Вызываются только по нажатию кнопки (стенд снимков их не вызывает).
#include <string>

#include "../core/TextUtil.hpp"

#ifdef _WIN32
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#endif

namespace dialogs
{

inline bool PickFolder(std::string& path)
{
#ifdef _WIN32
    bool ok = false;
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
    {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dlg->SetTitle(L"Папка для файлов замеров");
        if (!path.empty())
        {
            IShellItem* start = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(text::Widen(path).c_str(), nullptr, IID_PPV_ARGS(&start))))
            {
                dlg->SetFolder(start);
                start->Release();
            }
        }
        if (SUCCEEDED(dlg->Show(GetActiveWindow())))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)))
            {
                PWSTR p = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)))
                {
                    path = text::Narrow(p);
                    CoTaskMemFree(p);
                    ok = true;
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    return ok;
#else
    (void) path;
    return false;
#endif
}

// filter — пары «описание\0маска\0», в конце \0
inline bool PickFile(std::string& path, const wchar_t* filter, const wchar_t* title)
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {};
    const std::wstring init = text::Widen(path);
    if (init.size() < MAX_PATH)
        wcscpy_s(buf, init.c_str());
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn))
        return false;
    path = text::Narrow(buf);
    return true;
#else
    (void) path;
    (void) filter;
    (void) title;
    return false;
#endif
}

inline void OpenFolder(const std::string& path)
{
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", text::Widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void) path;
#endif
}

} // namespace dialogs
