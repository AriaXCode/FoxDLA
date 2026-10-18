#ifndef SYMBOLIC_LINK_MANAGER_MINE_HPP
#define SYMBOLIC_LINK_MANAGER_MINE_HPP

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
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <string>
#include <utility>

namespace slm {

// Shared operation result used by validation and link creation.
struct OpResult {
    bool ok = false;
    DWORD code = ERROR_SUCCESS;
    std::wstring message;

    static OpResult Success(std::wstring msg = L"") {
        OpResult result;
        result.ok = true;
        result.message = std::move(msg);
        return result;
    }

    static OpResult Failure(std::wstring msg, DWORD errorCode = ERROR_SUCCESS) {
        OpResult result;
        result.code = errorCode;
        result.message = std::move(msg);
        return result;
    }
};

enum class LinkKind { File, Directory };

struct LinkRequest {
    std::wstring sourcePath;  // Absolute, normalized, without trailing separator.
    std::wstring linkPath;    // Absolute, normalized, without trailing separator.
    bool isDirectory = false;
    bool linkInsideSource = false;
};

namespace StringUtil {
std::wstring Trim(const std::wstring& text);
std::wstring CleanPathInput(const std::wstring& text);
std::string ToUtf8(const std::wstring& text);
std::wstring ErrorCodeText(DWORD code);
}  // namespace StringUtil

namespace PathUtil {
bool IsSeparator(wchar_t c);
bool IsDriveRoot(const std::wstring& path);
std::wstring StripTrailingSeparators(std::wstring path);
bool MakeAbsolute(const std::wstring& input, std::wstring& output, DWORD& error);
std::wstring LeafName(const std::wstring& path);
std::wstring ParentOf(const std::wstring& path);
bool EqualsNoCase(const std::wstring& a, const std::wstring& b);
bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix);
bool LeafHasInvalidCharacters(const std::wstring& leaf);
bool GetAttributes(const std::wstring& path, DWORD& attributes, DWORD& error);
bool IsNotFoundError(DWORD error);
}  // namespace PathUtil

namespace Validation {
OpResult ValidateRequest(const std::wstring& rawSource, const std::wstring& rawLink,
                         LinkKind kind, LinkRequest& request);
}  // namespace Validation

namespace LinkCreator {
OpResult Create(const LinkRequest& request);
}  // namespace LinkCreator

namespace Logger {
void Init();
void Info(const std::wstring& message);
void Warn(const std::wstring& message);
void Error(const std::wstring& message);
const std::wstring& Path();
}  // namespace Logger

bool IsProcessElevated();

namespace Gui {
int Run(HINSTANCE instance, int showCommand);
}  // namespace Gui

}  // namespace slm

#endif  // SYMBOLIC_LINK_MANAGER_MINE_HPP
