// tests/sm83_runner.cpp
// SingleStepTests/sm83 runner for SeaBoy CPU verification.
//
// Clone test data:
//   git clone --depth 1 https://github.com/SingleStepTests/sm83.git tests/sm83_data
//
// Run:
//   ./build/Release/sm83_runner.exe tests/sm83_data/v1
//
// Each .json file covers one opcode (1 000 randomised test cases).
// For each case the runner:
//   1. Loads the initial CPU state and RAM into a flat 64 KB MMU.
//   2. Steps the CPU once.
//   3. Compares every field in the "final" object.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "src/core/CPU.hpp"
#include "src/core/MMU.hpp"
#include "src/core/Registers.hpp"

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string hex8(uint8_t v)
{
    std::ostringstream ss;
    ss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<unsigned>(v);
    return ss.str();
}

static std::string hex16(uint16_t v)
{
    std::ostringstream ss;
    ss << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << v;
    return ss.str();
}

// ---------------------------------------------------------------------------
// Test state (mirrors the JSON schema)
// ---------------------------------------------------------------------------

struct State
{
    uint16_t pc = 0, sp = 0;
    uint8_t  a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, h = 0, l = 0;
    bool     ime     = false;
    bool     ei      = false; // IME-scheduled (EI delay) - optional field
    bool     has_ei  = false;
    uint8_t  ie      = 0;    // IE register value - initial state only
    bool     has_ie  = false;
    std::vector<std::pair<uint16_t, uint8_t>> ram;
};

static State parseState(const json& j)
{
    State s;
    s.pc = j.at("pc").get<uint16_t>();
    s.sp = j.at("sp").get<uint16_t>();
    s.a  = j.at("a").get<uint8_t>();
    s.b  = j.at("b").get<uint8_t>();
    s.c  = j.at("c").get<uint8_t>();
    s.d  = j.at("d").get<uint8_t>();
    s.e  = j.at("e").get<uint8_t>();
    s.f  = j.at("f").get<uint8_t>();
    s.h  = j.at("h").get<uint8_t>();
    s.l  = j.at("l").get<uint8_t>();
    s.ime = j.at("ime").get<int>() != 0;

    if (j.contains("ei"))
    {
        s.has_ei = true;
        s.ei     = j.at("ei").get<int>() != 0;
    }
    if (j.contains("ie"))
    {
        s.has_ie = true;
        s.ie     = j.at("ie").get<uint8_t>();
    }
    for (const auto& entry : j.at("ram"))
        s.ram.push_back({entry[0].get<uint16_t>(), entry[1].get<uint8_t>()});

    return s;
}

// ---------------------------------------------------------------------------
// Single test execution
// ---------------------------------------------------------------------------

struct TestResult
{
    bool        pass      = true;
    std::string firstFail;

    void fail(std::string msg)
    {
        pass = false;
        if (firstFail.empty())
            firstFail = std::move(msg);
    }
};

static TestResult runTest(const json& tc)
{
    TestResult result;

    const State initial  = parseState(tc.at("initial"));
    const State expected = parseState(tc.at("final"));

    // ---- memory ----
    SeaBoy::MMU mmu;
    mmu.enableTestMode();

    // Write IE first so RAM entries can override 0xFFFF if needed
    if (initial.has_ie)
        mmu.testWrite(SeaBoy::ADDR_IE, initial.ie);

    for (const auto& [addr, val] : initial.ram)
        mmu.testWrite(addr, val);

    // ---- CPU ----
    SeaBoy::CPU cpu(mmu);
    // Set state directly - do NOT call reset()
    SeaBoy::Registers& r = cpu.regs();
    r.A  = initial.a;
    r.B  = initial.b;
    r.C  = initial.c;
    r.D  = initial.d;
    r.E  = initial.e;
    r.F  = initial.f;
    r.H  = initial.h;
    r.L  = initial.l;
    r.PC = initial.pc;
    r.SP = initial.sp;

    cpu.setIME(initial.ime);
    if (initial.has_ei)
        cpu.setIMEScheduled(initial.ei);

    // ---- run one step ----
    cpu.step();

    // ---- compare registers ----
    const SeaBoy::Registers& a = cpu.registers();

    if (a.A  != expected.a)  result.fail("A="  + hex8(a.A)  + " want " + hex8(expected.a));
    if (a.B  != expected.b)  result.fail("B="  + hex8(a.B)  + " want " + hex8(expected.b));
    if (a.C  != expected.c)  result.fail("C="  + hex8(a.C)  + " want " + hex8(expected.c));
    if (a.D  != expected.d)  result.fail("D="  + hex8(a.D)  + " want " + hex8(expected.d));
    if (a.E  != expected.e)  result.fail("E="  + hex8(a.E)  + " want " + hex8(expected.e));
    if (a.F  != expected.f)  result.fail("F="  + hex8(a.F)  + " want " + hex8(expected.f));
    if (a.H  != expected.h)  result.fail("H="  + hex8(a.H)  + " want " + hex8(expected.h));
    if (a.L  != expected.l)  result.fail("L="  + hex8(a.L)  + " want " + hex8(expected.l));
    if (a.PC != expected.pc) result.fail("PC=" + hex16(a.PC) + " want " + hex16(expected.pc));
    if (a.SP != expected.sp) result.fail("SP=" + hex16(a.SP) + " want " + hex16(expected.sp));

    // IME
    if (cpu.ime() != expected.ime)
        result.fail(std::string("IME=") + (cpu.ime() ? "1" : "0") +
                    " want " + (expected.ime ? "1" : "0"));

    // EI-scheduled flag (only checked if "ei" present in final state)
    if (expected.has_ei && cpu.imeScheduled() != expected.ei)
        result.fail(std::string("EI=") + (cpu.imeScheduled() ? "1" : "0") +
                    " want " + (expected.ei ? "1" : "0"));

    // ---- compare RAM ----
    for (const auto& [addr, val] : expected.ram)
    {
        uint8_t got = mmu.testRead(addr);
        if (got != val)
            result.fail("RAM[" + hex16(addr) + "]=" + hex8(got) + " want " + hex8(val));
    }

    return result;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: sm83_runner <test-dir>\n\n"
                     "  Clone test data first:\n"
                     "    git clone --depth 1 https://github.com/SingleStepTests/sm83.git tests/sm83_data\n\n"
                     "  Then run:\n"
                     "    sm83_runner tests/sm83_data/v1\n";
        return 1;
    }

    fs::path testDir(argv[1]);
    if (!fs::exists(testDir))
    {
        std::cerr << "Error: path not found: " << testDir << "\n";
        return 1;
    }

    // Collect all .json files (recurse to handle CB subdirectories, if any)
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(testDir))
    {
        if (entry.path().extension() == ".json")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    if (files.empty())
    {
        std::cerr << "No .json files found in: " << testDir << "\n";
        return 1;
    }

    int totalPass = 0, totalFail = 0, totalSkip = 0;

    for (const auto& filePath : files)
    {
        std::ifstream f(filePath);
        if (!f.is_open()) { ++totalSkip; continue; }

        json tests;
        try
        {
            tests = json::parse(f);
        }
        catch (const json::parse_error& e)
        {
            std::cerr << "Parse error in " << filePath.filename() << ": " << e.what() << "\n";
            ++totalSkip;
            continue;
        }

        int         filePass = 0, fileFail = 0;
        std::string firstFailName, firstFailReason;

        for (const auto& tc : tests)
        {
            try
            {
                TestResult res = runTest(tc);
                if (res.pass)
                {
                    ++filePass;
                }
                else
                {
                    ++fileFail;
                    if (firstFailName.empty())
                    {
                        firstFailName   = tc.at("name").get<std::string>();
                        firstFailReason = res.firstFail;
                    }
                }
            }
            catch (const std::exception& ex)
            {
                ++fileFail;
                if (firstFailName.empty())
                {
                    firstFailName   = tc.value("name", "?");
                    firstFailReason = std::string("exception: ") + ex.what();
                }
            }
        }

        totalPass += filePass;
        totalFail += fileFail;

        int total = filePass + fileFail;
        if (fileFail == 0)
        {
            std::cout << "PASS  " << filePath.filename().string()
                      << "  [" << filePass << "/" << total << "]\n";
        }
        else
        {
            std::cout << "FAIL  " << filePath.filename().string()
                      << "  [" << filePass << "/" << total << "]"
                      << "  first: \"" << firstFailName << "\" - " << firstFailReason << "\n";
        }
    }

    std::cout << "\n--- Summary ---\n"
              << "PASS: " << totalPass << "\n"
              << "FAIL: " << totalFail << "\n";
    if (totalSkip > 0)
        std::cout << "SKIP: " << totalSkip << "\n";
    std::cout << "Total: " << (totalPass + totalFail) << "\n";

    return totalFail > 0 ? 1 : 0;
}
