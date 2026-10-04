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
        << "  --version                Prints vclpp's version and exits.\n"
        << "  -j, --vcl-boilerplate    Adds the standard VCL prologue/epilogue to the output.\n"
        << "  -x, --fixcexpr           Replaces constant integer expressions, like 1+2, by their values.\n"
        << "  -f, --flatten-subscripts Turns name[0] into name_0 and name[x] into namex.\n"
        << "  -I <dir>                 Adds a directory to look for #include files in.\n"
        << "  -D <name>[=<value>]      Defines a macro, as '#define name value' (the value defaults to 1).\n"
        << "  -Wundef                  Warns when an #if evaluates an identifier that is not a macro.\n"
        << "  -Werror                  Treats warnings as errors.\n"
        << "  -m, --masp               MASP mode: the input is written for masp, the GASP-compatible\n"
        << "                           preprocessor (.macro, .aif, .include, ...). Running vclpp under\n"
        << "                           the name masp or gasp turns it on too.\n"
        << "\n"
        << " In MASP mode, #include takes only a header's macros, the output goes to stdout unless\n"
        << " an output file is given, and these masp options apply:\n"
        << "  -o <file>                The output file.\n"
        << "  -c <char>                Makes <char> start comments as well as ';'.\n"
        << "  -D <name>[=<value>]      Sets the MASP variable <name>, read as \\&name.\n"
        << "  -P <char>                The directive prefix: only '.' is supported.\n"
        << "  -v                       Prints vclpp's version and exits.\n"
        << "  -p, -s, -u, -l, -d       Accepted and ignored.\n"
        << "\n"
        << "Created by Guilherme R. Lampert.\n";
}

void print_version()
{
    std::cout << "vclpp 2, with a MASP mode that stands in for masp 0.1.16\n";
}

// Whether to run in MASP mode: -m says so, and so does running vclpp under masp's or
// GASP's name (e.g. "masp", or "ee-gasp" with a toolchain prefix), as a drop-in for them.
bool wants_masp_mode(const int argc, const char * const argv[])
{
    const std::string program = std::filesystem::path{ argv[0] }.stem().string();
    for (const std::string name : { "masp", "gasp" })
    {
        const std::string prefixed = "-" + name;
        const bool has_prefix = (program.size() > prefixed.size() &&
                                 program.compare(program.size() - prefixed.size(), prefixed.size(), prefixed) == 0);
        if (program == name || has_prefix)
        {
            return true;
        }
    }

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "-m" || arg == "--masp")
        {
            return true;
        }
    }
    return false;
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
    // masp's options only exist in MASP mode, so that comes first.
    opts.masp = wants_masp_mode(argc, argv);

    std::vector<std::string> file_names;
    std::optional<std::string> output_option; // MASP mode's -o.

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];

        // An option's value: attached ("-Idir"), the next argument ("-I dir"), or for a long
        // option after '=' ("--output=file").
        const auto option_value = [&](const std::string_view option) -> std::optional<std::string>
        {
            if (arg.size() > option.size())
            {
                const std::string_view attached = arg.substr(option.size());
                const bool long_option = (option.size() > 2);
                return std::string{ long_option && attached.front() == '=' ? attached.substr(1) : attached };
            }
            if (i + 1 < argc)
            {
                return std::string{ argv[++i] };
            }
            return std::nullopt;
        };
        const auto is_option = [arg](const std::string_view short_name, const std::string_view long_name)
        {
            return arg.substr(0, short_name.size()) == short_name ||
                   (arg.substr(0, long_name.size()) == long_name &&
                    (arg.size() == long_name.size() || arg[long_name.size()] == '='));
        };

        if (arg == "-h" || arg == "--help")
        {
            print_help(argv[0]);
            return EXIT_SUCCESS;
        }
        else if (arg == "--version" || (opts.masp && arg == "-v"))
        {
            print_version();
            return EXIT_SUCCESS;
        }
        else if (arg == "-m" || arg == "--masp")
        {
            // Already seen.
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
        else if (opts.masp && is_option("-c", "--commentchar"))
        {
            const std::optional<std::string> chars = option_value(arg.substr(0, 2) == "-c" ? "-c" : "--commentchar");
            if (!chars.has_value() || chars->size() != 1)
            {
                return command_line_error("-c needs one comment character");
            }
            opts.masp_settings.comment_chars += *chars;
        }
        else if (opts.masp && is_option("-o", "--output"))
        {
            output_option = option_value(arg.substr(0, 2) == "-o" ? "-o" : "--output");
            if (!output_option.has_value() || output_option->empty())
            {
                return command_line_error("-o needs a file name");
            }
        }
        else if (opts.masp && is_option("-P", "--prefixchar"))
        {
            const std::optional<std::string> prefix = option_value(arg.substr(0, 2) == "-P" ? "-P" : "--prefixchar");
            if (prefix != std::optional<std::string>{ "." })
            {
                return command_line_error("-P: '.' is the only directive prefix vclpp supports");
            }
        }
        else if (opts.masp && (arg == "-p" || arg == "--print" || arg == "-s" || arg == "--copysource" ||
                               arg == "-u" || arg == "--unreasonable" || arg == "-l" || arg == "--line-numbers" ||
                               arg == "-d" || arg == "--debug"))
        {
            // masp's listing and debugging options: they do not change the code.
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

            // In MASP mode, as in masp, -D sets a variable rather than a macro.
            if (opts.masp)
            {
                if (!is_identifier(name))
                {
                    return command_line_error("cannot set '" + name + "' with -D");
                }
                opts.masp_settings.variables.emplace_back(std::move(name), std::move(value));
                continue;
            }

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
    if (output_option.has_value() && file_names.size() > 1)
    {
        return command_line_error("two output files: '" + *output_option + "' and '" + file_names[1] + "'");
    }

    opts.input_path = file_names[0];
    if (file_names.size() > 1 || output_option.has_value())
    {
        opts.output_path = (output_option.has_value() ? *output_option : file_names[1]);
    }
    else if (!opts.masp) // MASP mode writes to stdout, as masp does.
    {
        opts.output_path = std::filesystem::path{ file_names[0] }.replace_extension(".vsm").string();
    }
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

        if (opts.output_path.empty())
        {
            std::cout << text << std::flush;
            if (!std::cout)
            {
                diags.error("cannot write the output to stdout");
            }
            return EXIT_SUCCESS;
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
