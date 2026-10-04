// ================================================================================================
// -*- C++ -*-
// File: main.cpp
// Brief: vclpp, a C-like preprocessor for the PS2 Vector Unit assembly fed to VCL (openvcl).
//        The command line and the run.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#include "diagnostics.hpp"
#include "engine.hpp"
#include "expressions.hpp"
#include "macro_table.hpp"
#include "output_writer.hpp"
#include "source_manager.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

void print_help(const char * const program_name)
{
    std::cout << "\n"
        << "Usage:\n"
        << " $ " << program_name << " [options] <input-file> [output-file]\n"
        << " Preprocesses a Vector Unit assembly source file for VCL. Besides C's #include,\n"
        << " #define, #undef, #if/#ifdef/#ifndef/#elif/#else/#endif, #error, #warning and\n"
        << " #pragma once, it supports #macro blocks and #vuprog/#endvuprog programs.\n"
        << " With no output file, the output is the input file with its extension replaced by '.vsm'.\n"
        << " Options are:\n"
        << "  -h, --help               Prints this message and exits.\n"
        << "  -j, --vcl-boilerplate    Adds the standard VCL prologue/epilogue to the output.\n"
        << "  -x, --fixcexpr           Replaces constant integer expressions, like 1+2, by their values.\n"
        << "  -f, --flatten-subscripts Turns name[0] into name_0 and name[x] into namex.\n"
        << "  -I <dir>                 Adds a directory to look for #include files in.\n"
        << "  -D <name>[=<value>]      Defines a macro, as '#define name value' (the value defaults to 1).\n"
        << "  -Wundef                  Warns when an #if evaluates an identifier that is not a macro.\n"
        << "  -Werror                  Treats warnings as errors.\n"
        << "\n"
        << "Created by Guilherme R. Lampert.\n";
}

int command_line_error(const std::string & message)
{
    std::cerr << "vclpp: error: " << message << "\n"
              << "vclpp: note: run 'vclpp --help' for the options\n";
    return EXIT_FAILURE;
}

bool is_identifier(const std::string_view text)
{
    const auto is_start = [](const char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    const auto is_rest  = [&is_start](const char c) { return is_start(c) || (c >= '0' && c <= '9'); };

    if (text.empty() || !is_start(text.front()))
    {
        return false;
    }
    for (const char c : text.substr(1))
    {
        if (!is_rest(c))
        {
            return false;
        }
    }
    return true;
}

// Reads the command line into 'opts'. Options can come anywhere; the first file name
// is the input and the second, if any, the output. Returns an exit status to stop with
// right away, after printing the help or an error, or nothing to carry on.
std::optional<int> parse_command_line(const int argc, const char * const argv[], vclpp::options & opts)
{
    std::vector<std::string> file_names;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];

        // The value of -I or -D, either attached ("-Idir") or the next argument ("-I dir").
        const auto option_value = [&](const std::string_view option) -> std::optional<std::string>
        {
            if (arg.size() > option.size())
            {
                return std::string{ arg.substr(option.size()) };
            }
            if (i + 1 < argc)
            {
                return std::string{ argv[++i] };
            }
            return std::nullopt;
        };

        if (arg == "-h" || arg == "--help")
        {
            print_help(argv[0]);
            return EXIT_SUCCESS;
        }
        else if (arg == "-j" || arg == "--vcl-boilerplate")
        {
            opts.vcl_boilerplate = true;
        }
        else if (arg == "-x" || arg == "--fixcexpr")
        {
            opts.fold_constants = true;
        }
        else if (arg == "-f" || arg == "--flatten-subscripts")
        {
            opts.flatten_subscripts = true;
        }
        else if (arg == "-Wundef")
        {
            opts.warn_undef = true;
        }
        else if (arg == "-Werror")
        {
            opts.werror = true;
        }
        else if (arg.substr(0, 2) == "-I")
        {
            const std::optional<std::string> dir = option_value("-I");
            if (!dir.has_value() || dir->empty())
            {
                return command_line_error("-I needs a directory");
            }
            opts.include_dirs.push_back(*dir);
        }
        else if (arg.substr(0, 2) == "-D")
        {
            const std::optional<std::string> definition = option_value("-D");
            if (!definition.has_value())
            {
                return command_line_error("-D needs a macro name");
            }

            const std::size_t equals = definition->find('=');
            std::string name  = definition->substr(0, equals);
            std::string value = (equals == std::string::npos ? std::string{ "1" } : definition->substr(equals + 1));

            if (!is_identifier(name) || name == "defined" || vclpp::macro_table::is_builtin_name(name))
            {
                return command_line_error("cannot define '" + name + "' with -D");
            }
            opts.defines.emplace_back(std::move(name), std::move(value));
        }
        else if (arg.size() > 1 && arg.front() == '-')
        {
            return command_line_error("unknown option '" + std::string{ arg } + "'");
        }
        else
        {
            file_names.emplace_back(arg);
        }
    }

    if (file_names.empty())
    {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }
    if (file_names.size() > 2)
    {
        return command_line_error("unexpected file name '" + file_names[2] + "': only an input and an output are taken");
    }

    opts.input_path  = file_names[0];
    opts.output_path = (file_names.size() > 1 ? file_names[1]
                                              : std::filesystem::path{ file_names[0] }.replace_extension(".vsm").string());
    return std::nullopt;
}

} // namespace

int main(const int argc, const char * argv[])
{
    vclpp::options opts;
    if (const std::optional<int> status = parse_command_line(argc, argv, opts); status.has_value())
    {
        return *status;
    }

    vclpp::source_manager sources{ std::vector<std::filesystem::path>(opts.include_dirs.begin(), opts.include_dirs.end()) };
    vclpp::diagnostics diags{ sources, opts.werror };

    try
    {
        vclpp::engine engine{ opts, sources, diags };
        vclpp::preprocessed result = engine.run();

        vclpp::expression_evaluator evaluator{ diags };
        const vclpp::output_options output{ opts.vcl_boilerplate, opts.fold_constants, opts.flatten_subscripts };
        const std::string text = vclpp::write_output(std::move(result.tokens), result.program_name, output, evaluator);

        // Warnings as errors fail the run, once all of them have been reported.
        if (diags.failed())
        {
            return EXIT_FAILURE;
        }

        std::ofstream file{ opts.output_path, std::ios::binary };
        file << text;
        file.close();
        if (!file)
        {
            diags.error("cannot write output file '" + opts.output_path + "'");
        }
        return EXIT_SUCCESS;
    }
    catch (const vclpp::fatal_error &)
    {
        return EXIT_FAILURE; // Already reported.
    }
    catch (const std::exception & e)
    {
        std::cerr << "vclpp: error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
