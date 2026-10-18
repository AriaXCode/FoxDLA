#include "mine.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
struct TestCase {
    const char* name;
    bool (*run)();
};

bool TestTrimEmpty() {
    return slm::StringUtil::Trim(L" \t\r\n ").empty();
}

bool TestTrimKeepsInnerWhitespace() {
    return slm::StringUtil::Trim(L"  hello world  ") == L"hello world";
}

bool TestCleanQuotedPath() {
    return slm::StringUtil::CleanPathInput(L"  \"C:\\Program Files\\App\"  ") ==
           L"C:\\Program Files\\App";
}

bool TestCleanUnquotedPath() {
    return slm::StringUtil::CleanPathInput(L"  C:\\Data\\folder  ") == L"C:\\Data\\folder";
}

bool TestDriveRootDetection() {
    return slm::PathUtil::IsDriveRoot(L"C:\\") && slm::PathUtil::IsDriveRoot(L"D:/") &&
           !slm::PathUtil::IsDriveRoot(L"C:\\Folder");
}

bool TestStripTrailingSeparators() {
    return slm::PathUtil::StripTrailingSeparators(L"C:\\") == L"C:\\" &&
           slm::PathUtil::StripTrailingSeparators(L"C:\\Folder\\\\") == L"C:\\Folder";
}

bool TestLeafName() {
    return slm::PathUtil::LeafName(L"C:\\Folder\\file.txt") == L"file.txt" &&
           slm::PathUtil::LeafName(L"C:/Folder") == L"Folder" &&
           slm::PathUtil::LeafName(L"plain-name") == L"plain-name";
}

bool TestInvalidLeafCharacters() {
    return !slm::PathUtil::LeafHasInvalidCharacters(L"report_final-1.txt") &&
           slm::PathUtil::LeafHasInvalidCharacters(L"bad:name") &&
           slm::PathUtil::LeafHasInvalidCharacters(std::wstring(L"bad") + wchar_t(1));
}

bool TestSeparatorRecognition() {
    return slm::PathUtil::IsSeparator(L'\\') && slm::PathUtil::IsSeparator(L'/') &&
           !slm::PathUtil::IsSeparator(L':');
}

bool TestResultFactories() {
    const auto success = slm::OpResult::Success(L"ok");
    const auto failure = slm::OpResult::Failure(L"failed", ERROR_ACCESS_DENIED);
    return success.ok && success.code == ERROR_SUCCESS && success.message == L"ok" &&
           !failure.ok && failure.code == ERROR_ACCESS_DENIED && failure.message == L"failed";
}
}  // namespace

int main() {
    const std::vector<TestCase> tests = {
        {"Trim handles empty whitespace", TestTrimEmpty},
        {"Trim preserves inner whitespace", TestTrimKeepsInnerWhitespace},
        {"CleanPathInput strips surrounding quotes", TestCleanQuotedPath},
        {"CleanPathInput handles unquoted input", TestCleanUnquotedPath},
        {"Drive root detection", TestDriveRootDetection},
        {"Trailing separator normalization", TestStripTrailingSeparators},
        {"Leaf name extraction", TestLeafName},
        {"Invalid leaf character validation", TestInvalidLeafCharacters},
        {"Separator recognition", TestSeparatorRecognition},
        {"Operation result factories", TestResultFactories},
    };

    int failed = 0;
    for (const auto& test : tests) {
        const bool passed = test.run();
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << test.name << '\n';
        if (!passed) ++failed;
    }

    std::cout << '\n' << (tests.size() - static_cast<std::size_t>(failed)) << '/' << tests.size()
              << " tests passed.\n";
    return failed == 0 ? 0 : 1;
}
