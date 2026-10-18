// ============================================================================
// FoxDLA
// Native Win32 / C++17 / Unicode. No external dependencies.
//
// Implementation is exposed through include/mine.hpp; this file contains the
// Win32 implementation, validation, logging, dialogs, and GUI event handling.
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601  // Windows 7 or later
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <exception>
#include <string>

#include "mine.hpp"

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uuid.lib")
// Enable visual styles (Common Controls v6) without needing a .manifest file.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#endif
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

namespace slm {

constexpr wchar_t kAppTitle[] = L"Symbolic Link Manager";
constexpr wchar_t kWindowClass[] = L"SymbolicLinkManagerWindow";
constexpr wchar_t kLogFileName[] = L"app.log.txt";
constexpr wchar_t kLogFallbackFolder[] = L"SymbolicLinkManager";

// ----------------------------------------------------------------------------
// StringUtil
// ----------------------------------------------------------------------------
namespace StringUtil {

std::wstring Trim(const std::wstring& s) {
    const wchar_t* whitespace = L" \t\r\n";
    const size_t first = s.find_first_not_of(whitespace);
    if (first == std::wstring::npos) return std::wstring();
    const size_t last = s.find_last_not_of(whitespace);
    return s.substr(first, last - first + 1);
}

// Trims whitespace and one pair of surrounding quotes
// (Explorer's "Copy as path" adds quotes).
std::wstring CleanPathInput(const std::wstring& s) {
    const auto isWhitespace = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    };

    size_t first = 0;
    size_t last = s.size();
    while (first < last && isWhitespace(s[first])) ++first;
    while (last > first && isWhitespace(s[last - 1])) --last;

    // Explorer's "Copy as path" adds quotes. Trim inside them without making
    // intermediate std::wstring copies.
    if (last - first >= 2 && s[first] == L'"' && s[last - 1] == L'"') {
        ++first;
        --last;
        while (first < last && isWhitespace(s[first])) ++first;
        while (last > first && isWhitespace(s[last - 1])) --last;
    }
    return s.substr(first, last - first);
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        &out[0], needed, nullptr, nullptr);
    return out;
}

std::wstring ErrorCodeText(DWORD code) {
    wchar_t buf[48] = {};
    swprintf(buf, 48, L"%lu / 0x%08lX", static_cast<unsigned long>(code),
             static_cast<unsigned long>(code));
    return buf;
}

}  // namespace StringUtil

// ----------------------------------------------------------------------------
// ErrorReporting
// ----------------------------------------------------------------------------
namespace ErrorReporting {

std::wstring FormatSystemMessage(DWORD code) {
    LPWSTR buffer = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring msg;
    if (len != 0 && buffer != nullptr) {
        try {
            msg.assign(buffer, len);
        } catch (...) {
            msg.clear();
        }
    }
    if (buffer != nullptr) LocalFree(buffer);

    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' ||
                            msg.back() == L' ' || msg.back() == L'.')) {
        msg.pop_back();
    }
    if (msg.empty()) msg = L"Unknown error";
    return msg;
}

// "Access is denied (error 5 / 0x00000005)"
std::wstring Describe(DWORD code) {
    return FormatSystemMessage(code) + L" (error " + StringUtil::ErrorCodeText(code) + L")";
}

// Extra, human-oriented advice for failures of CreateSymbolicLinkW.
std::wstring CreateLinkHint(DWORD code) {
    switch (code) {
        case ERROR_PRIVILEGE_NOT_HELD:
            return L"Windows only lets standard users create symbolic links when Developer Mode "
                   L"is enabled. Either:\n"
                   L"  - turn on Developer Mode (Settings > For developers), then try again, or\n"
                   L"  - close this program and run it as administrator "
                   L"(right-click > Run as administrator).";
        case ERROR_ACCESS_DENIED:
            return L"You do not have permission to create items in the destination folder. "
                   L"Choose a folder you can write to, or run the program as administrator.";
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return L"Something already exists at the link location. Nothing was changed. "
                   L"Choose a name that does not exist yet.";
        case ERROR_INVALID_FUNCTION:
        case ERROR_NOT_SUPPORTED:
            return L"The destination drive probably does not support symbolic links "
                   L"(for example FAT32/exFAT volumes, or some network shares). "
                   L"Use an NTFS drive for the link location.";
        case ERROR_INVALID_NAME:
        case ERROR_BAD_PATHNAME:
            return L"The link location contains characters or a name that Windows does not allow.";
        case ERROR_FILENAME_EXCED_RANGE:
            return L"The path is too long. Use shorter folder or file names.";
        case ERROR_PATH_NOT_FOUND:
        case ERROR_FILE_NOT_FOUND:
            return L"A folder in the link location does not exist.";
        case ERROR_SHARING_VIOLATION:
            return L"The location is currently in use by another program.";
        case ERROR_INVALID_PARAMETER:
            return L"Windows rejected the request as invalid. Check both paths.";
        default:
            return std::wstring();
    }
}

}  // namespace ErrorReporting

// ----------------------------------------------------------------------------
// Logger
// ----------------------------------------------------------------------------
namespace Logger {

enum class Level { Info, Warning, Error };

std::wstring g_logPath;  // empty => logging disabled (no writable location)

std::wstring GetExecutableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, &buffer[0], static_cast<DWORD>(buffer.size()));
        if (n == 0) return std::wstring();
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        if (buffer.size() > 65536) return std::wstring();
        buffer.resize(buffer.size() * 2);
    }
    const size_t slash = buffer.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    return buffer.substr(0, slash);
}

std::wstring GetFallbackDirectory() {
    std::wstring base;
    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &known)) &&
        known != nullptr) {
        base = known;
    }
    if (known != nullptr) CoTaskMemFree(known);

    if (base.empty()) {
        wchar_t temp[MAX_PATH + 2] = {};
        const DWORD n = GetTempPathW(MAX_PATH + 1, temp);
        if (n == 0 || n > MAX_PATH) return std::wstring();
        base.assign(temp, n);
        while (!base.empty() && (base.back() == L'\\' || base.back() == L'/')) base.pop_back();
    }

    std::wstring dir = base + L"\\" + kLogFallbackFolder;
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return std::wstring();
    }
    return dir;
}

// Opens the file for appending, creating it if needed. Caller closes the handle.
HANDLE OpenForAppend(const std::wstring& path) {
    return CreateFileW(path.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool IsAppendable(const std::wstring& path) {
    HANDLE h = OpenForAppend(path);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

bool WriteRaw(const std::string& bytes) {
    if (g_logPath.empty()) return false;
    HANDLE h = OpenForAppend(g_logPath);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(h, &size) && size.QuadPart == 0) {
        static const char bom[] = {'\xEF', '\xBB', '\xBF'};  // helps Notepad detect UTF-8
        DWORD bomWritten = 0;
        WriteFile(h, bom, sizeof(bom), &bomWritten, nullptr);
    }
    DWORD written = 0;
    if (!WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != bytes.size()) {
        ok = false;
    }
    CloseHandle(h);
    return ok;
}

// Chooses the log location. Never throws.
void Init() {
    try {
        const std::wstring exeDir = GetExecutableDirectory();
        if (!exeDir.empty()) {
            const std::wstring candidate = exeDir + L"\\" + kLogFileName;
            if (IsAppendable(candidate)) {
                g_logPath = candidate;
                return;
            }
        }
        const std::wstring fallbackDir = GetFallbackDirectory();
        if (!fallbackDir.empty()) {
            const std::wstring candidate = fallbackDir + L"\\" + kLogFileName;
            if (IsAppendable(candidate)) {
                g_logPath = candidate;
                return;
            }
        }
        g_logPath.clear();
    } catch (...) {
        g_logPath.clear();
    }
}

// Writes one line. Never throws; failures are silently ignored.
void Write(Level level, const std::wstring& message) {
    try {
        if (g_logPath.empty()) return;

        SYSTEMTIME st = {};
        GetLocalTime(&st);
        wchar_t stamp[64] = {};
        swprintf(stamp, 64, L"%04d-%02d-%02d %02d:%02d:%02d.%03d", st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        const wchar_t* tag = L"INFO ";
        if (level == Level::Warning) tag = L"WARN ";
        if (level == Level::Error) tag = L"ERROR";

        // Build one line in a single reserved buffer. This avoids allocating
        // an additional flattened copy and several temporary concatenations.
        std::wstring line;
        line.reserve(std::wcslen(stamp) + 11 + message.size());
        line.append(stamp).append(L" [").append(tag).append(L"] ");
        for (wchar_t c : message) {
            line.push_back((c == L'\r' || c == L'\n') ? L' ' : c);
        }
        line.append(L"\r\n");
        WriteRaw(StringUtil::ToUtf8(line));
    } catch (...) {
        // Logging must never crash the application.
    }
}

void Info(const std::wstring& m) { Write(Level::Info, m); }
void Warn(const std::wstring& m) { Write(Level::Warning, m); }
void Error(const std::wstring& m) { Write(Level::Error, m); }

const std::wstring& Path() { return g_logPath; }

}  // namespace Logger

// ----------------------------------------------------------------------------
// PathUtil
// ----------------------------------------------------------------------------
namespace PathUtil {

bool IsSeparator(wchar_t c) { return c == L'\\' || c == L'/'; }

bool IsDriveRoot(const std::wstring& p) {
    return p.size() == 3 && p[1] == L':' && IsSeparator(p[2]);
}

std::wstring StripTrailingSeparators(std::wstring p) {
    while (p.size() > 1 && IsSeparator(p.back()) && !IsDriveRoot(p)) p.pop_back();
    return p;
}

// Converts to an absolute, normalised path (resolves ".", "..", "/").
bool MakeAbsolute(const std::wstring& input, std::wstring& output, DWORD& error) {
    const DWORD needed = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        error = GetLastError();
        return false;
    }
    std::wstring buffer(needed, L'\0');
    const DWORD written = GetFullPathNameW(input.c_str(), needed, &buffer[0], nullptr);
    if (written == 0 || written >= needed) {
        error = (written == 0) ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        return false;
    }
    buffer.resize(written);
    output = std::move(buffer);
    return true;
}

std::wstring LeafName(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring ParentOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    std::wstring parent = path.substr(0, pos);
    if (parent.size() == 2 && parent[1] == L':') parent += L'\\';  // "C:" -> "C:\"
    if (parent.empty()) parent = L"\\";
    return parent;
}

bool EqualsNoCase(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix) {
    if (text.size() < prefix.size()) return false;
    return CompareStringOrdinal(text.c_str(), static_cast<int>(prefix.size()), prefix.c_str(),
                                static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

bool LeafHasInvalidCharacters(const std::wstring& leaf) {
    for (wchar_t c : leaf) {
        if (c < 32) return true;
        switch (c) {
            case L'<': case L'>': case L':': case L'"': case L'|': case L'?': case L'*':
                return true;
            default:
                break;
        }
    }
    return false;
}

// Wrapper around GetFileAttributesW. Does not follow the final symlink, so a
// dangling link is still reported as existing.
bool GetAttributes(const std::wstring& path, DWORD& attributes, DWORD& error) {
    const DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        error = GetLastError();
        return false;
    }
    attributes = a;
    error = ERROR_SUCCESS;
    return true;
}

bool IsNotFoundError(DWORD e) {
    return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
}

}  // namespace PathUtil

// ----------------------------------------------------------------------------
// Validation
// ----------------------------------------------------------------------------
namespace Validation {

OpResult ValidateRequest(const std::wstring& rawSource, const std::wstring& rawLink,
                         LinkKind kind, LinkRequest& request) {
    using namespace PathUtil;

    const std::wstring sourceInput = StringUtil::CleanPathInput(rawSource);
    const std::wstring linkInput = StringUtil::CleanPathInput(rawLink);

    if (sourceInput.empty()) {
        return OpResult::Failure(
            L"Please enter the Source Path: the existing file or folder the link will point to.");
    }
    if (linkInput.empty()) {
        return OpResult::Failure(
            L"Please enter the Link Location: the new path where the symbolic link will be created.");
    }

    DWORD err = ERROR_SUCCESS;

    // --- Source ------------------------------------------------------------
    std::wstring source;
    if (!MakeAbsolute(sourceInput, source, err)) {
        return OpResult::Failure(L"The Source Path is not a valid path.\n\n" +
                                     ErrorReporting::Describe(err),
                                 err);
    }
    source = StripTrailingSeparators(source);

    DWORD sourceAttrs = 0;
    if (!GetAttributes(source, sourceAttrs, err)) {
        if (IsNotFoundError(err)) {
            return OpResult::Failure(L"The source does not exist:\n" + source +
                                         L"\n\nThe link must point to an existing file or folder.",
                                     err);
        }
        return OpResult::Failure(L"The source cannot be accessed:\n" + source + L"\n\n" +
                                     ErrorReporting::Describe(err),
                                 err);
    }

    const bool sourceIsDirectory = (sourceAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (kind == LinkKind::File && sourceIsDirectory) {
        return OpResult::Failure(
            L"The source is a directory, but \"File Link\" is selected.\n\n"
            L"Select \"Directory Link\" or choose a file as the source.");
    }
    if (kind == LinkKind::Directory && !sourceIsDirectory) {
        return OpResult::Failure(
            L"The source is a file, but \"Directory Link\" is selected.\n\n"
            L"Select \"File Link\" or choose a directory as the source.");
    }

    // --- Link location -----------------------------------------------------
    std::wstring link;
    if (!MakeAbsolute(linkInput, link, err)) {
        return OpResult::Failure(L"The Link Location is not a valid path.\n\n" +
                                     ErrorReporting::Describe(err),
                                 err);
    }
    link = StripTrailingSeparators(link);

    const std::wstring leaf = LeafName(link);
    if (leaf.empty() || IsDriveRoot(link)) {
        return OpResult::Failure(
            L"The Link Location must end with a name for the new link (for example "
            L"C:\\Links\\MyLink), not just a drive or folder.");
    }
    if (LeafHasInvalidCharacters(leaf)) {
        return OpResult::Failure(
            L"The link name \"" + leaf +
            L"\" contains characters Windows does not allow (< > : \" | ? * or control characters).");
    }
    if (EqualsNoCase(source, link)) {
        return OpResult::Failure(L"The Link Location is the same as the Source Path. "
                                 L"A link cannot replace the item it points to.");
    }

    const std::wstring parent = ParentOf(link);
    DWORD parentAttrs = 0;
    if (parent.empty() || !GetAttributes(parent, parentAttrs, err)) {
        if (parent.empty() || IsNotFoundError(err)) {
            return OpResult::Failure(
                L"The folder that should contain the link does not exist:\n" + parent +
                    L"\n\nCreate the folder first, or choose another location. "
                    L"(This program never creates folders for you.)",
                ERROR_PATH_NOT_FOUND);
        }
        return OpResult::Failure(L"The folder that should contain the link cannot be accessed:\n" +
                                     parent + L"\n\n" + ErrorReporting::Describe(err),
                                 err);
    }
    if ((parentAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return OpResult::Failure(L"The parent of the link location is a file, not a folder:\n" +
                                 parent);
    }

    DWORD linkAttrs = 0;
    if (GetAttributes(link, linkAttrs, err)) {
        const wchar_t* what = (linkAttrs & FILE_ATTRIBUTE_REPARSE_POINT) ? L"a link or junction"
                              : (linkAttrs & FILE_ATTRIBUTE_DIRECTORY)   ? L"a folder"
                                                                         : L"a file";
        return OpResult::Failure(std::wstring(L"The destination already exists (it is ") + what +
                                     L"):\n" + link +
                                     L"\n\nExisting items are never overwritten. Nothing was changed. "
                                     L"Choose a different name.",
                                 ERROR_ALREADY_EXISTS);
    }
    if (!IsNotFoundError(err)) {
        return OpResult::Failure(L"The link location cannot be checked:\n" + link + L"\n\n" +
                                     ErrorReporting::Describe(err),
                                 err);
    }

    // A link inside the folder it points to creates a recursive loop.
    bool inside = false;
    if (sourceIsDirectory) {
        std::wstring prefix = source;
        if (prefix.empty() || prefix.back() != L'\\') prefix += L'\\';
        inside = StartsWithNoCase(link, prefix);
    }

    request.sourcePath = source;
    request.linkPath = link;
    request.isDirectory = sourceIsDirectory;
    request.linkInsideSource = inside;
    return OpResult::Success();
}

}  // namespace Validation

// ----------------------------------------------------------------------------
// LinkCreator
// ----------------------------------------------------------------------------
namespace LinkCreator {

// Creates the link. CreateSymbolicLinkW itself never overwrites an existing
// destination (it fails with ERROR_ALREADY_EXISTS), so a race between
// validation and creation is still safe.
OpResult Create(const LinkRequest& request) {
    const DWORD baseFlags = request.isDirectory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;

    SetLastError(ERROR_SUCCESS);
    BOOLEAN created = CreateSymbolicLinkW(request.linkPath.c_str(), request.sourcePath.c_str(),
                                          baseFlags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
    DWORD error = created ? ERROR_SUCCESS : GetLastError();

    if (!created && error == ERROR_INVALID_PARAMETER) {
        // Windows older than 10 (1703) does not know the "unprivileged" flag.
        Logger::Warn(L"CreateSymbolicLinkW rejected the unprivileged-create flag (error 87). "
                     L"Retrying without it.");
        SetLastError(ERROR_SUCCESS);
        created = CreateSymbolicLinkW(request.linkPath.c_str(), request.sourcePath.c_str(),
                                      baseFlags);
        error = created ? ERROR_SUCCESS : GetLastError();
    }

    if (!created) {
        if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
        std::wstring text = L"The symbolic link could not be created.\n\n" +
                            ErrorReporting::Describe(error);
        const std::wstring hint = ErrorReporting::CreateLinkHint(error);
        if (!hint.empty()) text += L"\n\n" + hint;
        return OpResult::Failure(text, error);
    }

    // Verify that what now exists really is a reparse point.
    DWORD attrs = 0, verifyError = 0;
    if (!PathUtil::GetAttributes(request.linkPath, attrs, verifyError)) {
        Logger::Warn(L"Link reported as created but could not be verified: " +
                     ErrorReporting::Describe(verifyError));
    } else if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        Logger::Warn(L"Link was created but the reparse-point attribute is missing.");
    }
    return OpResult::Success();
}

}  // namespace LinkCreator

// ----------------------------------------------------------------------------
// Dialogs (standard Windows pickers)
// ----------------------------------------------------------------------------
namespace Dialogs {

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { Reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* operator->() const { return ptr_; }
    T* Get() const { return ptr_; }
    T** Put() {
        Reset();
        return &ptr_;
    }
    void Reset() {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

enum class PickOutcome { Picked, Cancelled, Failed };

// Folder picker (modern Vista+ dialog).
PickOutcome PickFolder(HWND owner, const wchar_t* title, std::wstring& path, std::wstring& error) {
    ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(dialog.Put()));
    if (FAILED(hr)) {
        error = L"The folder dialog could not be created. " +
                ErrorReporting::Describe(static_cast<DWORD>(hr));
        return PickOutcome::Failed;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title);

    hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return PickOutcome::Cancelled;
    if (FAILED(hr)) {
        error = L"The folder dialog failed. " + ErrorReporting::Describe(static_cast<DWORD>(hr));
        return PickOutcome::Failed;
    }

    ComPtr<IShellItem> item;
    hr = dialog->GetResult(item.Put());
    if (FAILED(hr)) {
        error = L"The selected folder could not be read. " +
                ErrorReporting::Describe(static_cast<DWORD>(hr));
        return PickOutcome::Failed;
    }

    PWSTR raw = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &raw);
    if (FAILED(hr) || raw == nullptr) {
        error = L"The selected folder is not a regular file-system folder.";
        return PickOutcome::Failed;
    }
    path = raw;
    CoTaskMemFree(raw);
    return PickOutcome::Picked;
}

// Existing-file picker.
PickOutcome PickExistingFile(HWND owner, std::wstring& path, std::wstring& error) {
    std::wstring buffer(32768, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = &buffer[0];
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrTitle = L"Select the source file";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                OFN_NOCHANGEDIR | OFN_NODEREFERENCELINKS;

    if (!GetOpenFileNameW(&ofn)) {
        const DWORD code = CommDlgExtendedError();
        if (code == 0) return PickOutcome::Cancelled;
        error = L"The file dialog failed (common dialog error " + std::to_wstring(code) + L").";
        return PickOutcome::Failed;
    }
    path = buffer.c_str();
    return PickOutcome::Picked;
}

// "Save as" dialog used only to choose a NEW path. It does not create or test
// anything on disk (OFN_NOTESTFILECREATE) and never prompts to overwrite.
PickOutcome PickNewLinkPath(HWND owner, const std::wstring& suggestedName, std::wstring& path,
                            std::wstring& error) {
    std::wstring buffer(32768, L'\0');
    if (suggestedName.size() < MAX_PATH) {
        std::copy(suggestedName.begin(), suggestedName.end(), buffer.begin());
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = &buffer[0];
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrTitle = L"Choose a folder and type a NEW name for the link";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR |
                OFN_NOTESTFILECREATE;

    if (!GetSaveFileNameW(&ofn)) {
        const DWORD code = CommDlgExtendedError();
        if (code == 0) return PickOutcome::Cancelled;
        error = L"The file dialog failed (common dialog error " + std::to_wstring(code) + L").";
        return PickOutcome::Failed;
    }
    path = buffer.c_str();
    return PickOutcome::Picked;
}

}  // namespace Dialogs

// ----------------------------------------------------------------------------
// Process helpers
// ----------------------------------------------------------------------------
bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                    elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return ok;
}

// ----------------------------------------------------------------------------
// Gui
// ----------------------------------------------------------------------------
namespace Gui {

enum ControlId : int {
    IDC_SOURCE_LABEL = 100,
    IDC_SOURCE_EDIT,
    IDC_SOURCE_BROWSE,
    IDC_LINK_LABEL,
    IDC_LINK_EDIT,
    IDC_LINK_BROWSE,
    IDC_TYPE_LABEL,
    IDC_RADIO_FILE,
    IDC_RADIO_DIR,
    IDC_CREATE,
    IDC_STATUS
};

enum class StatusKind { Info, Success, Error };

int QuerySystemDpi() {
    HDC dc = GetDC(nullptr);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi > 0 ? dpi : 96;
}

std::wstring GetControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return std::wstring();
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, &text[0], length + 1);
    text.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
    return text;
}

class MainWindow {
public:
    MainWindow() : dpi_(QuerySystemDpi()) {}
    ~MainWindow() {
        if (font_ != nullptr) DeleteObject(font_);
    }
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool Create(HINSTANCE instance, int showCommand);
    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    int S(int value) const { return MulDiv(value, dpi_, 96); }

    bool OnCreate();
    void OnCommand(WORD id, WORD notification);
    void OnBrowseSource();
    void OnBrowseLink();
    void OnCreateLink();

    LinkKind SelectedKind() const {
        return SendMessageW(radioDirectory_, BM_GETCHECK, 0, 0) == BST_CHECKED ? LinkKind::Directory
                                                                               : LinkKind::File;
    }
    void SetStatus(StatusKind kind, const std::wstring& text);
    void ShowMessage(const std::wstring& text, UINT icon) const {
        MessageBoxW(hwnd_, text.c_str(), kAppTitle, MB_OK | icon);
    }

    HWND hwnd_ = nullptr;
    HWND sourceEdit_ = nullptr;
    HWND linkEdit_ = nullptr;
    HWND radioFile_ = nullptr;
    HWND radioDirectory_ = nullptr;
    HWND createButton_ = nullptr;
    HWND statusLabel_ = nullptr;
    HWND lastFocus_ = nullptr;
    HFONT font_ = nullptr;
    StatusKind statusKind_ = StatusKind::Info;
    int dpi_ = 96;
    bool busy_ = false;
};

bool MainWindow::Create(HINSTANCE instance, int showCommand) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::StaticWndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1));
    wc.lpszClassName = kWindowClass;

    if (RegisterClassExW(&wc) == 0) {
        const DWORD e = GetLastError();
        Logger::Error(L"RegisterClassExW failed: " + ErrorReporting::Describe(e));
        MessageBoxW(nullptr, (L"The main window class could not be registered.\n\n" +
                              ErrorReporting::Describe(e)).c_str(),
                    kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, S(560), S(276)};
    AdjustWindowRect(&rc, style, FALSE);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;

    RECT work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;

    hwnd_ = CreateWindowExW(0, kWindowClass, kAppTitle, style, x, y, width, height, nullptr,
                            nullptr, instance, this);
    if (hwnd_ == nullptr) {
        const DWORD e = GetLastError();
        Logger::Error(L"CreateWindowExW failed: " + ErrorReporting::Describe(e));
        MessageBoxW(nullptr, (L"The main window could not be created.\n\n" +
                              ErrorReporting::Describe(e)).c_str(),
                    kAppTitle, MB_OK | MB_ICONERROR);
        return false;
    }

    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
}

LRESULT CALLBACK MainWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self == nullptr) return DefWindowProcW(hwnd, msg, wp, lp);

    const LRESULT result = self->HandleMessage(msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
    }
    return result;
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            return OnCreate() ? 0 : -1;

        case WM_COMMAND:
            OnCommand(LOWORD(wp), HIWORD(wp));
            return 0;

        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lp) == statusLabel_) {
                HDC hdc = reinterpret_cast<HDC>(wp);
                COLORREF color = GetSysColor(COLOR_WINDOWTEXT);
                if (statusKind_ == StatusKind::Success) color = RGB(0, 128, 0);
                if (statusKind_ == StatusKind::Error) color = RGB(192, 0, 0);
                SetTextColor(hdc, color);
                SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            break;

        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                lastFocus_ = GetFocus();
            } else if (lastFocus_ != nullptr && IsWindow(lastFocus_)) {
                SetFocus(lastFocus_);
            } else if (sourceEdit_ != nullptr) {
                SetFocus(sourceEdit_);
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

bool MainWindow::OnCreate() {
    HINSTANCE instance = GetModuleHandleW(nullptr);

    auto make = [&](DWORD exStyle, const wchar_t* cls, const wchar_t* text, DWORD style, int x,
                    int y, int cx, int cy, int id) -> HWND {
        return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y),
                               S(cx), S(cy), hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    };

    HWND sourceLabel = make(0, L"STATIC", L"Source Path (existing file or folder the link points to):",
                            SS_LEFT | SS_NOPREFIX, 16, 14, 528, 18, IDC_SOURCE_LABEL);
    sourceEdit_ = make(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 16, 34, 430, 24,
                       IDC_SOURCE_EDIT);
    HWND sourceBrowse = make(0, L"BUTTON", L"Browse...", WS_TABSTOP | BS_PUSHBUTTON, 456, 33, 88, 26,
                             IDC_SOURCE_BROWSE);

    HWND linkLabel = make(0, L"STATIC", L"Link Location (new link to create):",
                          SS_LEFT | SS_NOPREFIX, 16, 72, 528, 18, IDC_LINK_LABEL);
    linkEdit_ = make(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 16, 92, 430, 24,
                     IDC_LINK_EDIT);
    HWND linkBrowse = make(0, L"BUTTON", L"Browse...", WS_TABSTOP | BS_PUSHBUTTON, 456, 91, 88, 26,
                           IDC_LINK_BROWSE);

    HWND typeLabel = make(0, L"STATIC", L"Link type:", SS_LEFT, 16, 134, 70, 20, IDC_TYPE_LABEL);
    radioFile_ = make(0, L"BUTTON", L"File Link", WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON, 92, 132,
                      110, 22, IDC_RADIO_FILE);
    radioDirectory_ = make(0, L"BUTTON", L"Directory Link", WS_TABSTOP | BS_AUTORADIOBUTTON, 212, 132,
                           150, 22, IDC_RADIO_DIR);

    createButton_ = make(0, L"BUTTON", L"Create Symbolic Link",
                         WS_GROUP | WS_TABSTOP | BS_DEFPUSHBUTTON, 16, 172, 190, 34, IDC_CREATE);

    statusLabel_ = make(0, L"STATIC", L"Ready.", SS_LEFT | SS_NOPREFIX, 16, 222, 528, 40, IDC_STATUS);

    HWND all[] = {sourceLabel, sourceEdit_,      sourceBrowse, linkLabel,     linkEdit_,
                  linkBrowse,  typeLabel,        radioFile_,   radioDirectory_, createButton_,
                  statusLabel_};
    for (HWND h : all) {
        if (h == nullptr) {
            Logger::Error(L"A child control could not be created: " +
                          ErrorReporting::Describe(GetLastError()));
            return false;
        }
    }

    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    }
    HFONT font = font_ != nullptr ? font_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for (HWND h : all) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    SendMessageW(sourceEdit_, EM_SETLIMITTEXT, 32767, 0);
    SendMessageW(linkEdit_, EM_SETLIMITTEXT, 32767, 0);
    SendMessageW(radioFile_, BM_SETCHECK, BST_CHECKED, 0);
    return true;
}

void MainWindow::SetStatus(StatusKind kind, const std::wstring& text) {
    statusKind_ = kind;
    SetWindowTextW(statusLabel_, text.c_str());
    InvalidateRect(statusLabel_, nullptr, TRUE);
    UpdateWindow(statusLabel_);
}

void MainWindow::OnCommand(WORD id, WORD notification) {
    switch (id) {
        case IDC_SOURCE_BROWSE:
            if (notification == BN_CLICKED) OnBrowseSource();
            break;
        case IDC_LINK_BROWSE:
            if (notification == BN_CLICKED) OnBrowseLink();
            break;
        case IDC_CREATE:
            if (notification == BN_CLICKED) OnCreateLink();
            break;
        case IDOK:  // Enter key pressed inside a text box
            OnCreateLink();
            break;
        default:
            break;
    }
}

void MainWindow::OnBrowseSource() {
    std::wstring path, error;
    Dialogs::PickOutcome outcome;
    if (SelectedKind() == LinkKind::Directory) {
        outcome = Dialogs::PickFolder(hwnd_, L"Select the source folder", path, error);
    } else {
        outcome = Dialogs::PickExistingFile(hwnd_, path, error);
    }

    if (outcome == Dialogs::PickOutcome::Picked) {
        SetWindowTextW(sourceEdit_, path.c_str());
        SetStatus(StatusKind::Info, L"Source selected.");
    } else if (outcome == Dialogs::PickOutcome::Failed) {
        Logger::Error(L"Source browse failed: " + error);
        SetStatus(StatusKind::Error, L"Could not open the dialog.");
        ShowMessage(error, MB_ICONERROR);
    }
}

void MainWindow::OnBrowseLink() {
    const std::wstring source = StringUtil::CleanPathInput(GetControlText(sourceEdit_));
    const std::wstring suggestion =
        PathUtil::LeafName(PathUtil::StripTrailingSeparators(source));

    std::wstring path, error;
    const Dialogs::PickOutcome outcome =
        Dialogs::PickNewLinkPath(hwnd_, suggestion, path, error);

    if (outcome == Dialogs::PickOutcome::Picked) {
        SetWindowTextW(linkEdit_, path.c_str());
        SetStatus(StatusKind::Info, L"Link location selected.");
    } else if (outcome == Dialogs::PickOutcome::Failed) {
        Logger::Error(L"Link location browse failed: " + error);
        SetStatus(StatusKind::Error, L"Could not open the dialog.");
        ShowMessage(error, MB_ICONERROR);
    }
}

void MainWindow::OnCreateLink() {
    if (busy_) return;
    busy_ = true;

    const LinkKind kind = SelectedKind();
    const wchar_t* kindText = kind == LinkKind::Directory ? L"Directory link" : L"File link";
    const std::wstring rawSource = GetControlText(sourceEdit_);
    const std::wstring rawLink = GetControlText(linkEdit_);

    Logger::Info(std::wstring(L"Link creation requested. Type=") + kindText + L" Source=\"" +
                 StringUtil::CleanPathInput(rawSource) + L"\" Link=\"" +
                 StringUtil::CleanPathInput(rawLink) + L"\"");

    SetStatus(StatusKind::Info, L"Validating paths...");

    LinkRequest request;
    const OpResult validation = Validation::ValidateRequest(rawSource, rawLink, kind, request);
    if (!validation.ok) {
        Logger::Error(L"Validation failed (error " + StringUtil::ErrorCodeText(validation.code) +
                      L"): " + validation.message);
        SetStatus(StatusKind::Error, L"Failed: check the paths and try again.");
        ShowMessage(validation.message, MB_ICONERROR);
        busy_ = false;
        return;
    }

    if (request.linkInsideSource) {
        const int answer = MessageBoxW(
            hwnd_,
            L"The link location is inside the folder it points to. This creates a loop "
            L"(the folder will contain a link to itself), which can confuse backup, search and "
            L"copy tools.\n\nCreate it anyway?",
            kAppTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (answer != IDYES) {
            Logger::Info(L"Link creation cancelled by user (link inside source).");
            SetStatus(StatusKind::Info, L"Cancelled.");
            busy_ = false;
            return;
        }
    }

    SetStatus(StatusKind::Info, L"Creating symbolic link...");
    const OpResult result = LinkCreator::Create(request);

    if (result.ok) {
        Logger::Info(std::wstring(L"SUCCESS: ") + kindText + L" created. Link=\"" +
                     request.linkPath + L"\" -> Source=\"" + request.sourcePath + L"\"");
        SetStatus(StatusKind::Success, L"Success: symbolic link created.");
        ShowMessage(std::wstring(L"The symbolic link was created.\n\nLink:\n") + request.linkPath +
                        L"\n\nPoints to:\n" + request.sourcePath,
                    MB_ICONINFORMATION);
    } else {
        Logger::Error(L"FAILED to create " + std::wstring(kindText) + L". Error " +
                      StringUtil::ErrorCodeText(result.code) + L" (" +
                      ErrorReporting::FormatSystemMessage(result.code) + L"). Link=\"" +
                      request.linkPath + L"\" Source=\"" + request.sourcePath + L"\"");
        SetStatus(StatusKind::Error, L"Failed: " + ErrorReporting::FormatSystemMessage(result.code));
        ShowMessage(result.message, MB_ICONERROR);
    }
    busy_ = false;
}

int Run(HINSTANCE instance, int showCommand) {
    MainWindow window;
    if (!window.Create(instance, showCommand)) return 1;

    MSG msg = {};
    for (;;) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) break;
        if (r == -1) {
            Logger::Error(L"GetMessageW failed: " + ErrorReporting::Describe(GetLastError()));
            return 1;
        }
        if (!IsDialogMessageW(window.Handle(), &msg)) {  // Tab / Enter / arrow-key navigation
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

}  // namespace Gui

}  // namespace slm

// ----------------------------------------------------------------------------
// Entry point (GUI subsystem, no console window)
// ----------------------------------------------------------------------------
#ifndef SYMBOLIC_LINK_MANAGER_NO_ENTRY_POINT
using namespace slm;
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();  // crisp text on high-DPI displays

    Logger::Init();
    Logger::Info(L"==== Application started ====");
    Logger::Info(std::wstring(L"Log file: ") +
                 (Logger::Path().empty() ? L"(none available)" : Logger::Path()));
    Logger::Info(std::wstring(L"Elevated (administrator): ") +
                 (IsProcessElevated() ? L"yes" : L"no"));

    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool comInitialised = SUCCEEDED(comResult);
    if (!comInitialised) {
        Logger::Warn(L"CoInitializeEx failed: " +
                     ErrorReporting::Describe(static_cast<DWORD>(comResult)) +
                     L". The folder picker may be unavailable.");
    }

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    if (!InitCommonControlsEx(&icc)) {
        Logger::Warn(L"InitCommonControlsEx failed; continuing with basic controls.");
    }

    int exitCode = 1;
    try {
        exitCode = Gui::Run(instance, showCommand);
    } catch (const std::exception& ex) {
        const char* what = ex.what();
        Logger::Error(L"Unhandled C++ exception: " + std::wstring(what, what + std::strlen(what)));
        MessageBoxW(nullptr, L"An unexpected error occurred and the program must close.", kAppTitle,
                    MB_OK | MB_ICONERROR);
    } catch (...) {
        Logger::Error(L"Unhandled unknown exception.");
        MessageBoxW(nullptr, L"An unexpected error occurred and the program must close.", kAppTitle,
                    MB_OK | MB_ICONERROR);
    }

    if (comInitialised) CoUninitialize();
    Logger::Info(L"==== Application shutdown (exit code " + std::to_wstring(exitCode) + L") ====");
    return exitCode;
}
#endif  // SYMBOLIC_LINK_MANAGER_NO_ENTRY_POINT
