/******************************************************************************
**
** Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
**
** This file is part of the extora which can be found at
** https://github.com/IvanPinezhaninov/extora/.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
** DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
** THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include <cstdlib>
#if defined(_WIN32)
#include <filesystem>
#endif // defined(_WIN32)
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#endif // defined(_WIN32)

#include <benchmark/benchmark.h>

#ifndef EXTORA_BENCHMARKS_ENABLE_ANALYTICS
#define EXTORA_BENCHMARKS_ENABLE_ANALYTICS 0
#endif // EXTORA_BENCHMARKS_ENABLE_ANALYTICS

#ifndef EXTORA_BENCHMARKS_PYTHON
#define EXTORA_BENCHMARKS_PYTHON ""
#endif // EXTORA_BENCHMARKS_PYTHON

#ifndef EXTORA_BENCHMARKS_ANALYZER
#define EXTORA_BENCHMARKS_ANALYZER ""
#endif // EXTORA_BENCHMARKS_ANALYZER

#ifndef EXTORA_BENCHMARKS_RESULT_JSON
#define EXTORA_BENCHMARKS_RESULT_JSON "extora_benchmarks.json"
#endif // EXTORA_BENCHMARKS_RESULT_JSON

namespace {

bool hasArgument(int argc, char** argv, std::string_view prefix)
{
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument{argv[i]};
    if (argument == prefix) return true;
    if (argument.size() > prefix.size() && argument.compare(0, prefix.size(), prefix) == 0 &&
        argument[prefix.size()] == '=')
      return true;
  }

  return false;
}

std::string argumentValue(const std::vector<std::string>& arguments, std::string_view name)
{
  for (std::size_t i = 1; i < arguments.size(); ++i) {
    const std::string& argument = arguments[i];
    if (argument.size() > name.size() && argument.compare(0, name.size(), name) == 0 && argument[name.size()] == '=')
      return argument.substr(name.size() + 1);
    if (argument == name && i + 1 < arguments.size()) return arguments[i + 1];
  }

  return {};
}

#if !defined(_WIN32)
std::string shellQuote(std::string_view value)
{
  std::string result;
  result.reserve(value.size() + 2);
  result += '\'';

  for (const char ch : value) {
    if (ch == '\'') {
      result += "'\\''";
    } else {
      result += ch;
    }
  }

  result += '\'';
  return result;
}
#endif // !defined(_WIN32)

int runAnalyzer(const std::string& resultJson, std::string_view resultFormat)
{
#if EXTORA_BENCHMARKS_ENABLE_ANALYTICS
  const std::string python = EXTORA_BENCHMARKS_PYTHON;
  const std::string analyzer = EXTORA_BENCHMARKS_ANALYZER;

  if (python.empty() || analyzer.empty() || resultJson.empty() || resultFormat != "json") return EXIT_SUCCESS;

  std::cout << '\n';

#if defined(_WIN32)
  const std::filesystem::path pythonPath{python};
  const std::filesystem::path analyzerPath{analyzer};
  const std::filesystem::path resultPath{resultJson};
  return static_cast<int>(::_wspawnl(_P_WAIT, pythonPath.c_str(), pythonPath.c_str(), analyzerPath.c_str(),
                                     resultPath.c_str(), static_cast<const wchar_t*>(nullptr)));
#else
  const std::string command = shellQuote(python) + " " + shellQuote(analyzer) + " " + shellQuote(resultJson);
  return std::system(command.c_str());
#endif // defined(_WIN32)
#else
  (void)resultJson;
  (void)resultFormat;
  return EXIT_SUCCESS;
#endif // EXTORA_BENCHMARKS_ENABLE_ANALYTICS
}

} // namespace

int main(int argc, char** argv)
{
  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc) + 2);

  for (int i = 0; i < argc; ++i)
    arguments.emplace_back(argv[i]);

#if EXTORA_BENCHMARKS_ENABLE_ANALYTICS
  const bool listTests = hasArgument(argc, argv, "--benchmark_list_tests");
  if (!listTests && !hasArgument(argc, argv, "--benchmark_out"))
    arguments.emplace_back(std::string{"--benchmark_out="} + EXTORA_BENCHMARKS_RESULT_JSON);

  if (!listTests && !hasArgument(argc, argv, "--benchmark_out_format"))
    arguments.emplace_back("--benchmark_out_format=json");
#endif // EXTORA_BENCHMARKS_ENABLE_ANALYTICS

  const std::string resultJson = argumentValue(arguments, "--benchmark_out");
  const std::string resultFormat = argumentValue(arguments, "--benchmark_out_format");

  std::vector<char*> benchmarkArgv;
  benchmarkArgv.reserve(arguments.size());

  for (std::string& argument : arguments)
    benchmarkArgv.push_back(argument.data());

  int benchmarkArgc = static_cast<int>(benchmarkArgv.size());

  benchmark::Initialize(&benchmarkArgc, benchmarkArgv.data());

  if (benchmark::ReportUnrecognizedArguments(benchmarkArgc, benchmarkArgv.data())) return EXIT_FAILURE;

  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();

  if (hasArgument(argc, argv, "--benchmark_list_tests")) return EXIT_SUCCESS;

  return runAnalyzer(resultJson, resultFormat);
}
