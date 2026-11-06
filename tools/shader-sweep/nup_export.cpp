// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include "core/file_sys/nup_container.h"
#include "core/file_sys/vfs/vfs_real.h"

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr std::size_t ChunkSize = 1024 * 1024;
constexpr std::size_t MetadataLimit = 256 * ChunkSize;

std::string Utf8(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

fs::path NativePath(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

class ReadOnlyInput final : public FileSys::VfsFile {
public:
    explicit ReadOnlyInput(FileSys::VirtualFile source_) : source(std::move(source_)) {}

    std::string GetName() const override { return source->GetName(); }
    std::size_t GetSize() const override { return source->GetSize(); }
    bool Resize(std::size_t) override { return false; }
    FileSys::VirtualDir GetContainingDirectory() const override { return nullptr; }
    bool IsWritable() const override { return false; }
    bool IsReadable() const override { return true; }
    std::size_t Write(const u8*, std::size_t, std::size_t) override { return 0; }
    bool Rename(std::string_view) override { return false; }

    std::size_t Read(u8* data, std::size_t size, std::size_t offset) const override {
        if (offset > GetSize() || size > GetSize() - offset) {
            throw std::runtime_error("NUP reader requested an out-of-bounds read");
        }
        std::size_t done{};
        while (done < size) {
            const auto chunk = std::min(ChunkSize, size - done);
            const auto count = source->Read(data + done, chunk, offset + done);
            bytes_read += count;
            if (count != chunk) {
                throw std::runtime_error("Short read from NUP input");
            }
            done += count;
        }
        return done;
    }

    std::vector<u8> ReadBytes(std::size_t size, std::size_t offset) const override {
        if (size > MetadataLimit || metadata_bytes > 2 * MetadataLimit - size) {
            throw std::runtime_error(
                "NUP metadata exceeds the 256 MiB block / 512 MiB total limit");
        }
        metadata_bytes += size;
        std::vector<u8> bytes(size);
        Read(bytes.data(), size, offset);
        return bytes;
    }

    mutable u64 bytes_read{};

private:
    FileSys::VirtualFile source;
    mutable std::size_t metadata_bytes{};
};

struct Options {
    fs::path input;
    fs::path root;
    fs::path output;
    std::set<std::string> paths;
    bool candidates{};
    u64 max_file_bytes = 512ULL * ChunkSize;
    u64 max_total_bytes = 2048ULL * ChunkSize;
};

u64 Number(std::string_view text) {
    u64 value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
        throw std::runtime_error("Expected a positive byte count: " + std::string(text));
    }
    return value;
}

Options Parse(const std::vector<std::string>& args) {
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--list") {
            continue;
        }
        if (arg == "--candidates") {
            options.candidates = true;
            continue;
        }
        if (i + 1 == args.size()) {
            throw std::runtime_error("Missing value for " + arg);
        }
        const auto& value = args[++i];
        if (arg == "--input") {
            options.input = NativePath(value);
        } else if (arg == "--read-only-root") {
            options.root = NativePath(value);
        } else if (arg == "--output") {
            options.output = NativePath(value);
        } else if (arg == "--path") {
            options.paths.insert(value);
        } else if (arg == "--max-file-bytes") {
            options.max_file_bytes = Number(value);
        } else if (arg == "--max-total-bytes") {
            options.max_total_bytes = Number(value);
        } else {
            throw std::runtime_error("Unknown option: " + arg);
        }
    }
    if (options.input.empty() || options.root.empty() || options.output.empty()) {
        throw std::runtime_error("--input, --read-only-root and --output are required");
    }
    return options;
}

bool IsWithin(fs::path path, const fs::path& root) {
    for (;;) {
        if (fs::exists(path) && fs::equivalent(path, root)) {
            return true;
        }
        const auto parent = path.parent_path();
        if (parent == path || parent.empty()) {
            return false;
        }
        path = parent;
    }
}

void Prepare(Options& options) {
    options.root = fs::canonical(options.root);
    options.input = fs::canonical(options.input);
    options.output = fs::weakly_canonical(fs::absolute(options.output));
    if (!fs::is_directory(options.root) || !fs::is_regular_file(options.input) ||
        !IsWithin(options.input, options.root)) {
        throw std::runtime_error("Input must be a regular file within --read-only-root");
    }
    if (IsWithin(options.output, options.root)) {
        throw std::runtime_error("Output must be outside --read-only-root");
    }
    if (fs::exists(options.output) || fs::is_symlink(fs::symlink_status(options.output))) {
        throw std::runtime_error(
            "Output must be a new directory; existing outputs are never reused");
    }
    if (!fs::is_directory(options.output.parent_path())) {
        throw std::runtime_error("Output parent directory must already exist");
    }
    if (!fs::create_directory(options.output)) {
        throw std::runtime_error("Cannot create fresh output directory");
    }
    fs::permissions(options.output, fs::perms::owner_all, fs::perm_options::replace);
}

std::string Lower(std::string text) {
    for (auto& ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            ch += 'a' - 'A';
        }
    }
    return text;
}

void CheckName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' ||
        name.find_first_of("/\\:<>\"|?*") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32; })) {
        throw std::runtime_error("Unsafe virtual filename: " + Json(name).dump());
    }
    const auto base = Lower(name.substr(0, name.find('.')));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul" || base == "conin$" ||
        base == "conout$" ||
        (base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) &&
         base[3] >= '0' && base[3] <= '9') ||
        ((base.starts_with("com") || base.starts_with("lpt")) &&
         (base.substr(3) == "\xc2\xb9" || base.substr(3) == "\xc2\xb2" ||
          base.substr(3) == "\xc2\xb3"))) {
        throw std::runtime_error("Windows device filename is not exportable: " + name);
    }
}

bool IsCandidate(const std::string& path) {
    const auto lower = Lower(path);
    constexpr std::array extensions{".bfsha", ".bnsh", ".bsh",  ".sharc", ".sharcb",
                                    ".shbin", ".spv",  ".sarc", ".szs",   ".pack",
                                    ".narc",  ".arc",  ".zs",   ".zst",   ".zstd"};
    return lower.find("shader") != std::string::npos ||
           std::any_of(extensions.begin(), extensions.end(),
                       [&](const char* extension) { return lower.ends_with(extension); });
}

struct Entry {
    std::size_t package;
    std::string path;
    FileSys::VirtualFile file;
};

void List(const FileSys::VirtualDir& dir, std::size_t package, const std::string& prefix,
          std::vector<Entry>& entries, Json& directories) {
    std::set<std::string> names;
    for (const auto& file : dir->GetFiles()) {
        CheckName(file->GetName());
        if (!names.insert(Lower(file->GetName())).second) {
            throw std::runtime_error("Duplicate or case-colliding RomFS path under " + prefix);
        }
        entries.push_back({package, prefix + file->GetName(), file});
    }
    for (const auto& sub : dir->GetSubdirectories()) {
        CheckName(sub->GetName());
        if (!names.insert(Lower(sub->GetName())).second) {
            throw std::runtime_error("Duplicate or case-colliding RomFS path under " + prefix);
        }
        const auto path = prefix + sub->GetName();
        directories.push_back({{"package", package}, {"path", path}});
        List(sub, package, path + "/", entries, directories);
    }
}

std::string Export(const Entry& entry, const fs::path& output) {
    fs::create_directories(output.parent_path());
    std::ofstream stream(output, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot create " + Utf8(output));
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> hash{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!hash || EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("Cannot initialize SHA-256");
    }
    std::vector<u8> buffer(ChunkSize);
    for (std::size_t offset = 0; offset < entry.file->GetSize();) {
        const auto size = std::min(buffer.size(), entry.file->GetSize() - offset);
        if (entry.file->Read(buffer.data(), size, offset) != size) {
            throw std::runtime_error("Short read or decompression failure: " + entry.path);
        }
        if (!stream.write(reinterpret_cast<const char*>(buffer.data()), size)) {
            throw std::runtime_error("Cannot write " + Utf8(output));
        }
        if (EVP_DigestUpdate(hash.get(), buffer.data(), size) != 1) {
            throw std::runtime_error("Cannot update SHA-256");
        }
        offset += size;
    }
    stream.close();
    if (!stream) {
        throw std::runtime_error("Cannot finish " + Utf8(output));
    }
    std::array<u8, 32> digest{};
    unsigned int length{};
    if (EVP_DigestFinal_ex(hash.get(), digest.data(), &length) != 1 || length != digest.size()) {
        throw std::runtime_error("Cannot finish SHA-256");
    }
    std::string result;
    for (const auto byte : digest) {
        result += fmt::format("{:02x}", byte);
    }
    return result;
}

void Run(const Options& options, Json& manifest) {
    FileSys::RealVfsFilesystem filesystem;
    const auto source = filesystem.OpenFile(Utf8(options.input), FileSys::OpenMode::Read);
    if (!source || !source->IsReadable() || source->IsWritable()) {
        throw std::runtime_error("Cannot open NUP read-only");
    }
    const auto input = std::make_shared<ReadOnlyInput>(source);
    FileSys::NupContainer container(input, 0, options.output / "spill");
    if (container.GetStatus() != Loader::ResultStatus::Success) {
        throw std::runtime_error(fmt::format("NUP parse failed (status {})",
                                             static_cast<unsigned>(container.GetStatus())));
    }
    manifest["source_size"] = input->GetSize();
    manifest["packages"] = Json::array();
    manifest["directories"] = Json::array();
    manifest["files"] = Json::array();
    std::vector<Entry> entries;
    for (std::size_t i = 0; i < container.GetPackages().size(); ++i) {
        const auto& package = container.GetPackages()[i];
        constexpr std::array kinds{"base", "update", "addon"};
        const auto romfs = container.GetRomFSDir(i);
        manifest["packages"].push_back({{"index", i},
                                        {"title_id", fmt::format("{:016X}", package.title_id)},
                                        {"title_name", package.title_name},
                                        {"kind", kinds[static_cast<unsigned>(package.kind)]},
                                        {"version", package.version},
                                        {"display_version", package.display_version},
                                        {"standalone_update", package.standalone_update},
                                        {"source_dump", package.source_dump},
                                        {"has_romfs", romfs != nullptr}});
        if (romfs) {
            List(romfs, i, "/", entries, manifest["directories"]);
        }
    }
    manifest["catalog_source_bytes_read"] = input->bytes_read;
    std::set<std::string> unmatched = options.paths;
    std::vector<std::size_t> selected;
    u64 total{};
    for (const auto& entry : entries) {
        const auto selector = fmt::format("{}:{}", entry.package, entry.path);
        const bool candidate = IsCandidate(entry.path);
        const bool explicit_path = unmatched.erase(selector) != 0;
        const bool select = explicit_path || (options.candidates && candidate);
        const auto relative = fmt::format("packages/{}/romfs{}", entry.package, entry.path);
        manifest["files"].push_back({{"package", entry.package},
                                     {"path", entry.path},
                                     {"selector", selector},
                                     {"size", entry.file->GetSize()},
                                     {"candidate", candidate},
                                     {"selected", select}});
        if (select) {
            const auto size = entry.file->GetSize();
            if (size > options.max_file_bytes || size > options.max_total_bytes - total) {
                throw std::runtime_error("Export byte limit exceeded by " + selector +
                                         "; select fewer paths or raise the explicit byte limits");
            }
            total += size;
            selected.push_back(manifest["files"].size() - 1);
            manifest["files"].back()["output"] = relative;
        }
    }
    if (!unmatched.empty()) {
        throw std::runtime_error("Requested path was not listed: " + *unmatched.begin());
    }
    if ((options.candidates || !options.paths.empty()) && selected.empty()) {
        throw std::runtime_error("No files matched the export selection");
    }
    for (const auto index : selected) {
        auto& record = manifest["files"][index];
        const auto output = options.output / NativePath(record["output"].get<std::string>());
        if (fs::exists(output)) {
            throw std::runtime_error("Refusing to overwrite " + Utf8(output));
        }
        try {
            record["sha256"] = Export(entries[index], output);
        } catch (...) {
            std::error_code error;
            fs::remove(output, error);
            throw;
        }
        record["exported"] = true;
    }
    manifest["source_bytes_read"] = input->bytes_read;
    manifest["exported_files"] = selected.size();
    manifest["exported_bytes"] = total;
    manifest["status"] = "success";
}

void SaveManifest(const fs::path& output, const Json& manifest) {
    std::ofstream stream(output / "manifest.json", std::ios::binary);
    stream << manifest.dump(2) << '\n';
    stream.close();
    if (!stream) {
        throw std::runtime_error("Cannot write manifest.json");
    }
}

int Main(const std::vector<std::string>& args) {
    if (args.size() == 2 && args[1] == "--help") {
        std::cout << "nup-shader-export --input GAME.nup --read-only-root LIBRARY "
                     "--output NEW_DIR\n"
                     "  [--list] [--candidates] [--path "
                     "'PACKAGE_INDEX:/romfs/path' ...]\n"
                     "  [--max-file-bytes N] [--max-total-bytes N]\n"
                     "Default: catalog all packages and RomFS paths in "
                     "manifest.json, no payload reads.\n"
                     "--candidates selects shader/archive filenames heuristically, "
                     "not by content.\n"
                     "Export defaults: 512 MiB/file, 2 GiB total; all zstd spills "
                     "stay under NEW_DIR.\n"
                     "Output must not exist, and its parent must exist outside the "
                     "library.\n";
        return 0;
    }
    try {
        auto options = Parse(args);
        Prepare(options);
        Json manifest{{"schema_version", 1},
                      {"source", Utf8(options.input)},
                      {"read_only_root", Utf8(options.root)},
                      {"status", "error"},
                      {"max_file_bytes", options.max_file_bytes},
                      {"max_total_bytes", options.max_total_bytes}};
        try {
            Run(options, manifest);
        } catch (const std::exception& error) {
            manifest["error"] = error.what();
            SaveManifest(options.output, manifest);
            throw;
        }
        SaveManifest(options.output, manifest);
        std::cout << Utf8(options.output / "manifest.json") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "nup-shader-export: " << error.what() << '\n';
        return 1;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(Utf8(fs::path(argv[i])));
    }
    return Main(args);
}
#else
int main(int argc, char** argv) { return Main({argv, argv + argc}); }
#endif
