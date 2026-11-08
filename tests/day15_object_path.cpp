#include "object_path.h"

#include <cstdlib>
#include <iostream>

void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    std::string path;
    require(make_object_path("upload-Abc123", path) &&
            path == "data/objects/upload-Abc123", "valid object path");
    require(is_valid_file_id("upload-aZ09bY"), "ASCII ID alphabet");
    const std::string invalid[] = {
        "", "upload-", "upload-Abc12", "upload-Abc1234",
        "UPLOAD-Abc123", "../upload-Abc123", "/tmp/upload-Abc123",
        "C:\\upload-Abc123", "upload-../ab", "upload-abc/de",
        "upload-abc\\de", "upload-abc.de", "upload-abc de",
        "upload-%2e%2e", "upload-Abc123?name=x",
        std::string("upload-Ab\0xyz", 13), "upload-中文"
    };
    for (const auto& id : invalid)
    {
        path = "unchanged";
        require(!is_valid_file_id(id) && !make_object_path(id, path),
                "invalid ID accepted");
        require(path == "unchanged", "failure modified output");
    }
    std::cout << "PASS: object path isolation, 17 invalid IDs rejected\n";
}
