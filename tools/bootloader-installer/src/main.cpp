#include <windows.h>
#include <process.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

#ifndef P2000T_VERSION
#error P2000T_VERSION must be supplied by CMake
#endif

namespace {

constexpr std::array<std::uint8_t, 3> ExpectedSignature{0x1e, 0x95, 0x87};
constexpr std::uint8_t ExpectedLowFuse = 0x5e;
constexpr std::uint8_t InstalledHighFuse = 0x98;
constexpr std::uint8_t InstalledExtendedFuse = 0xfb;

struct Options {
    fs::path avrdude;
    fs::path config;
    fs::path image;
    std::string action;
    unsigned probes = 5;
    double bitclock_us = 20.0;
};

struct Probe {
    std::array<std::uint8_t, 3> signature{};
    std::uint8_t low = 0;
    std::uint8_t high = 0;
    std::uint8_t extended = 0;
    std::uint8_t lock = 0;

    bool operator==(const Probe& other) const
    {
        return signature == other.signature && low == other.low &&
               high == other.high && extended == other.extended &&
               lock == other.lock;
    }
};

std::string hex_byte(std::uint8_t value)
{
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(value);
    return out.str();
}

std::string describe(const Probe& probe)
{
    std::ostringstream out;
    out << "signature=";
    for (auto byte : probe.signature) out << hex_byte(byte);
    out << " lfuse=" << hex_byte(probe.low)
        << " hfuse=" << hex_byte(probe.high)
        << " efuse=" << hex_byte(probe.extended)
        << " lock=" << hex_byte(probe.lock);
    return out.str();
}

fs::path executable_directory()
{
    std::vector<char> buffer(32768);
    DWORD size = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size == buffer.size()) throw std::runtime_error("Cannot locate this executable");
    return fs::path(std::string(buffer.data(), size)).parent_path();
}

std::optional<fs::path> search_path(const std::string& name)
{
    std::vector<char> buffer(32768);
    DWORD size = SearchPathA(nullptr, name.c_str(), nullptr,
                             static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (!size || size >= buffer.size()) return std::nullopt;
    return fs::path(std::string(buffer.data(), size));
}

std::optional<fs::path> newest_arduino_avrdude()
{
    const char* local = std::getenv("LOCALAPPDATA");
    if (!local) return std::nullopt;
    fs::path root = fs::path(local) / "Arduino15" / "packages" / "arduino" / "tools" / "avrdude";
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return std::nullopt;

    std::vector<fs::path> matches;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        fs::path candidate = entry.path() / "bin" / "avrdude.exe";
        if (fs::is_regular_file(candidate, ec)) matches.push_back(candidate);
    }
    if (matches.empty()) return std::nullopt;
    std::sort(matches.begin(), matches.end());
    return matches.back();
}

fs::path find_avrdude(const fs::path& requested)
{
    if (!requested.empty()) return requested;
    if (const char* env = std::getenv("AVRDUDE")) return fs::path(env);
    fs::path local = executable_directory() / "avrdude.exe";
    if (fs::is_regular_file(local)) return local;
    if (auto found = search_path("avrdude.exe")) return *found;
    if (auto found = newest_arduino_avrdude()) return *found;
    throw std::runtime_error("Cannot find avrdude.exe; use --avrdude PATH");
}

fs::path find_config(const fs::path& avrdude, const fs::path& requested)
{
    if (!requested.empty()) return requested;
    fs::path candidate = avrdude.parent_path().parent_path() / "etc" / "avrdude.conf";
    if (fs::is_regular_file(candidate)) return candidate;
    candidate = avrdude.parent_path() / "avrdude.conf";
    if (fs::is_regular_file(candidate)) return candidate;
    return {};
}

void require_file(const fs::path& path, const std::string& description)
{
    if (!fs::is_regular_file(path))
        throw std::runtime_error(description + " does not exist: " + path.string());
}

int run_process(const fs::path& executable, const std::vector<std::string>& arguments)
{
    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 1);
    storage.push_back(executable.string());
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<const char*> argv;
    argv.reserve(storage.size() + 1);
    for (const auto& item : storage) argv.push_back(item.c_str());
    argv.push_back(nullptr);
    return static_cast<int>(_spawnv(_P_WAIT, executable.string().c_str(), argv.data()));
}

std::vector<std::uint8_t> read_binary(const fs::path& path, std::size_t expected)
{
    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)), {});
    if (!input && !input.eof()) throw std::runtime_error("Cannot read " + path.string());
    if (data.size() != expected)
        throw std::runtime_error("Unexpected data length in " + path.string());
    return data;
}

fs::path make_work_directory()
{
    fs::path base = fs::temp_directory_path();
    auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path result = base / ("p2000t-isp-" + std::to_string(GetCurrentProcessId()) +
                              "-" + std::to_string(stamp));
    if (!fs::create_directory(result))
        throw std::runtime_error("Cannot create temporary directory: " + result.string());
    return result;
}

std::vector<std::string> common_arguments(const Options& options)
{
    std::vector<std::string> args;
    if (!options.config.empty()) {
        args.push_back("-C");
        args.push_back(options.config.string());
    }
    args.insert(args.end(), {"-c", "usbasp", "-p", "atmega32u4", "-B",
                             std::to_string(options.bitclock_us)});
    return args;
}

Probe probe_once(const Options& options, const fs::path& directory, unsigned index)
{
    fs::path prefix = directory / ("probe-" + std::to_string(index));
    fs::path signature = prefix.string() + "-signature.bin";
    fs::path low = prefix.string() + "-lfuse.bin";
    fs::path high = prefix.string() + "-hfuse.bin";
    fs::path extended = prefix.string() + "-efuse.bin";
    fs::path lock = prefix.string() + "-lock.bin";

    auto args = common_arguments(options);
    args.insert(args.end(), {"-n",
        "-U", "signature:r:" + signature.string() + ":r",
        "-U", "lfuse:r:" + low.string() + ":r",
        "-U", "hfuse:r:" + high.string() + ":r",
        "-U", "efuse:r:" + extended.string() + ":r",
        "-U", "lock:r:" + lock.string() + ":r"});
    if (run_process(options.avrdude, args) != 0)
        throw std::runtime_error("AVRDUDE connection probe failed");

    Probe result;
    auto sig = read_binary(signature, 3);
    std::copy(sig.begin(), sig.end(), result.signature.begin());
    result.low = read_binary(low, 1)[0];
    result.high = read_binary(high, 1)[0];
    result.extended = read_binary(extended, 1)[0];
    result.lock = read_binary(lock, 1)[0];
    return result;
}

void validate_preinstall(const Probe& probe)
{
    if (probe.signature != ExpectedSignature)
        throw std::runtime_error("Expected ATmega32U4 signature 1E9587, received " + describe(probe));
    if (probe.low != ExpectedLowFuse)
        throw std::runtime_error("Unexpected low fuse; expected 5E, received " + hex_byte(probe.low));
    if (probe.high != 0x99 && probe.high != InstalledHighFuse)
        throw std::runtime_error("Unexpected high fuse; expected 99 or 98, received " + hex_byte(probe.high));
    if ((probe.extended & 0x0f) != 0x03 && (probe.extended & 0x0f) != 0x0b)
        throw std::runtime_error("Unexpected extended fuse; expected F3 or FB, received " + hex_byte(probe.extended));
}

Probe check_connection(const Options& options, const fs::path& directory, unsigned count,
                       bool postinstall)
{
    std::optional<Probe> reference;
    for (unsigned i = 1; i <= count; ++i) {
        std::cout << "Connection probe " << i << '/' << count << "..." << std::endl;
        Probe current = probe_once(options, directory, i);
        if (current.signature != ExpectedSignature)
            throw std::runtime_error("Wrong or unstable target: " + describe(current));
        if (reference && !(current == *reference))
            throw std::runtime_error("Connection readings changed between probes");
        reference = current;
    }

    if (!reference) throw std::runtime_error("No connection probes requested");
    if (postinstall) {
        if (reference->low != ExpectedLowFuse || reference->high != InstalledHighFuse ||
            (reference->extended & 0x0f) != 0x0b || (reference->lock & 0x3f) != 0x3f)
            throw std::runtime_error("Post-install fuse or lock verification failed: " + describe(*reference));
    } else {
        validate_preinstall(*reference);
    }
    std::cout << "Stable connection: " << describe(*reference) << '\n';
    return *reference;
}

unsigned hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
    value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    if (value >= 'A' && value <= 'F') return static_cast<unsigned>(value - 'A' + 10);
    throw std::runtime_error("Invalid character in Intel HEX file");
}

std::uint8_t hex_value(const std::string& line, std::size_t offset)
{
    if (offset + 2 > line.size()) throw std::runtime_error("Truncated Intel HEX record");
    return static_cast<std::uint8_t>((hex_nibble(line[offset]) << 4) | hex_nibble(line[offset + 1]));
}

void validate_combined_image(const fs::path& image)
{
    std::ifstream input(image);
    if (!input) throw std::runtime_error("Cannot read image: " + image.string());
    std::string line;
    std::uint32_t base = 0;
    bool eof = false, application = false, bootloader = false;
    unsigned line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] != ':' || line.size() < 11 || ((line.size() - 1) & 1))
            throw std::runtime_error("Malformed Intel HEX record at line " + std::to_string(line_number));
        std::vector<std::uint8_t> record;
        for (std::size_t i = 1; i < line.size(); i += 2) record.push_back(hex_value(line, i));
        std::uint8_t length = record[0];
        if (record.size() != static_cast<std::size_t>(length) + 5)
            throw std::runtime_error("Wrong Intel HEX length at line " + std::to_string(line_number));
        unsigned checksum = 0;
        for (auto byte : record) checksum += byte;
        if (checksum & 0xff)
            throw std::runtime_error("Wrong Intel HEX checksum at line " + std::to_string(line_number));
        std::uint16_t address = static_cast<std::uint16_t>((record[1] << 8) | record[2]);
        std::uint8_t type = record[3];
        if (type == 0x00) {
            std::uint32_t start = base + address;
            std::uint32_t end = start + length;
            if (end > 0x8000) throw std::runtime_error("Image contains data beyond ATmega32U4 flash");
            if (start == 0 && length) application = true;
            if (start < 0x8000 && end > 0x7000) bootloader = true;
        } else if (type == 0x01) {
            eof = true;
        } else if (type == 0x02 && length == 2) {
            base = static_cast<std::uint32_t>((record[4] << 8) | record[5]) << 4;
        } else if (type == 0x04 && length == 2) {
            base = static_cast<std::uint32_t>((record[4] << 8) | record[5]) << 16;
        }
    }
    if (!eof || !application || !bootloader)
        throw std::runtime_error("Image must contain an application at 0000 and bootloader at 7000");
}

void install(const Options& options)
{
    validate_combined_image(options.image);
    auto args = common_arguments(options);
    args.insert(args.end(), {"-e",
        "-U", "flash:w:" + options.image.string() + ":i",
        "-U", "flash:v:" + options.image.string() + ":i",
        "-U", "efuse:w:0xfb:m",
        "-U", "hfuse:w:0x98:m"});
    std::cout << "Installing and verifying " << options.image << "..." << std::endl;
    if (run_process(options.avrdude, args) != 0)
        throw std::runtime_error("AVRDUDE flash or verification failed");
}

void usage()
{
    std::cout <<
        "P2000T Programmable Cartridge Preparation Tool v" P2000T_VERSION "\n\n"
        "Usage:\n"
        "  p2000t-programmable-cartridge-preparation-tool.exe [options] check\n"
        "  p2000t-programmable-cartridge-preparation-tool.exe [options] flash [image.hex]\n"
        "  p2000t-programmable-cartridge-preparation-tool.exe\n\n"
        "Options:\n"
        "  --avrdude PATH      Path to avrdude.exe\n"
        "  --config PATH       Path to avrdude.conf\n"
        "  --probes COUNT      Pre-flash connection reads (default: 5)\n"
        "  --bitclock-us VALUE USBasp ISP clock period (default: 20)\n"
        "  -V, --version       Show the software version\n"
        "  -h, --help          Show this help\n";
}

void wait_for_enter(const std::string& message)
{
    std::cout << message << std::flush;
    std::string ignored;
    std::getline(std::cin, ignored);
}

Options interactive_options()
{
    Options options;
    std::cout <<
        "P2000T Programmable Cartridge Preparation Tool v" P2000T_VERSION "\n"
        "==================================================\n\n"
        "Keep the cartridge out of the P2000T.\n"
        "Connect only the USBasp and hold all ISP contacts steady.\n\n"
        "  1. Prepare cartridge (check, install and verify)\n"
        "  2. Check ISP connection only\n"
        "  3. Exit\n\n"
        "Select an operation: " << std::flush;
    std::string choice;
    if (!std::getline(std::cin, choice)) {
        options.action = "exit";
        return options;
    }
    if (choice == "1") {
        options.action = "flash";
        options.image = executable_directory() / P2000T_FACTORY_IMAGE_FILENAME;
        if (!fs::is_regular_file(options.image)) {
            std::cout << "Path to factory HEX image: " << std::flush;
            std::string image;
            std::getline(std::cin, image);
            options.image = image;
        }
        std::cout << "\nThe ATmega32U4 will be erased and programmed.\n";
        wait_for_enter("Press Enter when the ISP contacts are firmly in place, or close this window to cancel...");
    } else if (choice == "2") {
        options.action = "check";
    } else if (choice == "3") {
        options.action = "exit";
    } else {
        throw std::runtime_error("Invalid menu selection");
    }
    std::cout << '\n';
    return options;
}

unsigned parse_unsigned(const std::string& value, const std::string& option)
{
    std::size_t used = 0;
    unsigned long parsed = std::stoul(value, &used);
    if (used != value.size() || parsed < 2 || parsed > 20)
        throw std::runtime_error(option + " must be between 2 and 20");
    return static_cast<unsigned>(parsed);
}

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&](const std::string& name) -> std::string {
            if (++i >= argc) throw std::runtime_error(name + " requires a value");
            return argv[i];
        };
        if (arg == "-h" || arg == "--help") {
            usage();
            std::exit(0);
        } else if (arg == "-V" || arg == "--version") {
            std::cout << P2000T_VERSION << '\n';
            std::exit(0);
        } else if (arg == "--avrdude") options.avrdude = value(arg);
        else if (arg == "--config") options.config = value(arg);
        else if (arg == "--probes") options.probes = parse_unsigned(value(arg), arg);
        else if (arg == "--bitclock-us") options.bitclock_us = std::stod(value(arg));
        else if (options.action.empty()) options.action = arg;
        else if (options.action == "flash" && options.image.empty()) options.image = arg;
        else throw std::runtime_error("Unexpected argument: " + arg);
    }
    if (options.action != "check" && options.action != "flash")
        throw std::runtime_error("Choose either check or flash");
    if (options.action == "flash" && options.image.empty()) {
        fs::path bundled = executable_directory() / P2000T_FACTORY_IMAGE_FILENAME;
        if (fs::is_regular_file(bundled)) options.image = bundled;
        else throw std::runtime_error("flash requires a factory HEX image");
    }
    if (!(options.bitclock_us >= 1.0 && options.bitclock_us <= 1000.0))
        throw std::runtime_error("--bitclock-us must be between 1 and 1000");
    return options;
}

void execute_operation(Options options)
{
    std::optional<fs::path> work;
    try {
        options.avrdude = fs::absolute(find_avrdude(options.avrdude));
        require_file(options.avrdude, "AVRDUDE executable");
        options.config = find_config(options.avrdude, options.config);
        if (!options.config.empty()) {
            options.config = fs::absolute(options.config);
            require_file(options.config, "AVRDUDE configuration");
        }
        if (!options.image.empty()) {
            options.image = fs::absolute(options.image);
            require_file(options.image, "Factory image");
        }

        std::cout << "AVRDUDE: " << options.avrdude << '\n';
        if (!options.config.empty()) std::cout << "Config:   " << options.config << '\n';
        work = make_work_directory();
        check_connection(options, *work, options.probes, false);
        if (options.action == "flash") {
            install(options);
            check_connection(options, *work, 3, true);
            std::cout << "\nCartridge preparation and verification succeeded.\n"
                         "You may release the ISP contacts.\n";
        } else {
            std::cout << "\nISP connection check succeeded; nothing was written.\n"
                         "You may release the ISP contacts.\n";
        }
        std::error_code ec;
        fs::remove_all(*work, ec);
    } catch (...) {
        if (work) std::cerr << "Probe files retained in " << *work << '\n';
        throw;
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 1) {
        while (true) {
            try {
                Options options = interactive_options();
                if (options.action == "exit") return 0;
                execute_operation(std::move(options));
            } catch (const std::exception& error) {
                std::cerr << "ERROR: " << error.what() << '\n';
            }
            wait_for_enter("\nPress Enter to return to the menu...");
            std::cout << "\n\n";
        }
    }

    try {
        Options options = parse_options(argc, argv);
        execute_operation(std::move(options));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
