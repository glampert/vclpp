
// ================================================================================================
// -*- C++ -*-
// File: vclpp_main.cpp
// Author: Guilherme R. Lampert
// Created on: 30/11/15
// Brief: A custom C-like preprocessor for combined use with the VCL tool and the PS2DEV SDK.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
//
// ================================================================================================

//
// c++ -std=c++14 -O2 -Wall -Wextra -pedantic vclpp_main.cpp -o vclpp
//
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// ========================================================
// Helpers:
// ========================================================

struct Definition
{
    std::string name;
    std::string value;
};

struct MacroBlock
{
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> lines;
};

struct Directives
{
    std::vector<std::string> includes;
    std::vector<Definition>  defines;
    std::vector<MacroBlock>  macros;
};

static inline bool isBlank(const std::string & s)
{
    return s.find_first_not_of(" \n\r\t") == std::string::npos;
}

// ========================================================
// class Preprocessor:
// ========================================================

// Runs on a single file, building a list of includes, defines and macros.
class Preprocessor final
{
private:

    //
    // Miscellaneous:
    //

    // The file stream for the source (not the #includes).
    std::ifstream sourceFile;

    // Lines of code that didn't make into directives/macros (blank lines ignored).
    std::vector<std::string> codeLines;

    // Used for error reporting only.
    std::string currentFileName;
    int currentLineNum;
    bool isIncludeFile;

    void error(const std::string & message) const
    {
        std::cerr << "ERROR: " << currentFileName
                  << "(" << currentLineNum << "): "
                  << message << std::endl;

        throw std::runtime_error("Preprocessor error.");
    }

    //
    // Includes:
    //
    std::string readIncludeDirective(std::vector<std::string> & tokens) const
    {
        if (tokens[1].front() != '"' || tokens[1].back() != '"')
        {
            error("Include directive must be between double quotes and contain no spaces!");
        }
        return tokens[1].substr(1, tokens[1].length() - 2);
    }

    //
    // Defines:
    //
    Definition readDefineDirective(std::vector<std::string> & tokens) const
    {
        Definition def;
        def.name = std::move(tokens[1]);

        // [0] = #define
        // [1] = constant name
        // [2..N] = value string
        const auto numTokens = tokens.size();
        for (std::size_t t = 2; t < numTokens; ++t)
        {
            def.value += tokens[t];
            if (t != numTokens - 1)
            {
                def.value += " ";
            }
        }

        return def;
    }

    //
    // Function-like macros:
    //
    MacroBlock readMacroHeader(std::vector<std::string> & tokens) const
    {
        MacroBlock macro;
        macro.name = std::move(tokens[1]);

        // If the name is followed by a colon, no spaces in between, assume a parameter list.
        if (macro.name.back() == ':')
        {
            // Get rid of the ':'
            macro.name.pop_back();

            // [0] = #macro
            // [1] = macro name
            // [2..N] = comma separated parameter list
            const auto numTokens = tokens.size();
            for (std::size_t t = 2; t < numTokens; ++t)
            {
                auto && param = std::move(tokens[t]);

                // Just a lost comma from an editing error?
                if (param == ",")
                {
                    error("Lost comma in macro '" + macro.name + "' parameter list!");
                }

                if (param.back() == ',')
                {
                    param.pop_back();
                    if (t == numTokens - 1)
                    {
                        error("Extraneous comma after last macro parameter '" + param + "'!");
                    }

                    // A lost comma probably from editing out a previous parameter.
                    if (param.back() == ',')
                    {
                        param.pop_back();
                        error("Lost comma after macro parameter '" + param + "'!");
                    }
                }
                else
                {
                    if (t != numTokens - 1)
                    {
                        error("Missing comma after macro parameter '" + param + "'!");
                    }
                }
                macro.params.emplace_back(param);
            }
        }
        else
        {
            if (tokens.size() > 2 && tokens[2][0] != ';')
            {
                error("More text follows macro declaration. "
                      "Add a ':' right after the macro name to define a param list!");
            }
        }

        return macro;
    }

public:

    const std::string & getCurrentFileName() const { return currentFileName;  }
    const std::vector<std::string> & getCodeLines() const { return codeLines; }

    Preprocessor(std::string filename, const bool isInclude)
        : currentFileName { std::move(filename) }
        , currentLineNum  { 0 }
        , isIncludeFile   { isInclude }
    {
        sourceFile.exceptions(0); // Don't throw on error. We use error().
        sourceFile.open(currentFileName);

        if (!sourceFile.is_open() || !sourceFile.good())
        {
            error("Unable to open file \"" + currentFileName + "\" for reading.");
        }
    }

    Directives parseDirectives()
    {
        // Temps:
        std::string line;
        std::vector<std::string> tokens;

        // #include directive filenames:
        std::vector<std::string> includes;

        // #define single-line constants:
        std::vector<Definition> defines;

        // #macro/#endmacro blocks:
        bool insideMacro = false;
        MacroBlock currentMacro;
        std::vector<MacroBlock> macros;

        // If the begin/end program sections are not found,
        // we warn, but allow preprocessing to continue.
        bool foundProgStart = false; // #vuprog
        bool foundProgEnd   = false; // #endvuprog

        //
        // Main processing loop:
        //
        while (std::getline(sourceFile, line))
        {
            ++currentLineNum;

            if (isBlank(line))
            {
                continue;
            }

            // If inside a macro, add the contents to it.
            if (insideMacro)
            {
                // Macro block closed.
                if (line == "#endmacro")
                {
                    macros.emplace_back(std::move(currentMacro));
                    insideMacro = false;
                }
                else
                {
                    if (line[0] == '#')
                    {
                        error("Preprocessor directive inside macro block: '" + line + "'");
                    }
                    currentMacro.lines.emplace_back(std::move(line));
                }
                continue;
            }

            // Not a define/macro and not resolving a macro block, ignore.
            if (line[0] != '#')
            {
                if (line[0] != ';') // Don't bother adding pure comment lines.
                {
                    codeLines.emplace_back(std::move(line));
                }
                continue;
            }

            // Split the line by whitespace:
            std::istringstream tokenizer{ std::move(line) };
            tokens.assign(std::istream_iterator<std::string>{ tokenizer },
                          std::istream_iterator<std::string>{});

            // Handle each preprocessor token:
            if (tokens[0] == "#include")
            {
                includes.emplace_back(readIncludeDirective(tokens));
            }
            else if (tokens[0] == "#define")
            {
                defines.emplace_back(readDefineDirective(tokens));
            }
            else if (tokens[0] == "#macro")
            {
                currentMacro = readMacroHeader(tokens);
                insideMacro  = true;
            }
            else if (tokens[0] == "#vuprog")
            {
                foundProgStart = true;
            }
            else if (tokens[0] == "#endvuprog")
            {
                foundProgEnd = true;
            }
            else
            {
                error("Unknown preprocessor directive '" + tokens[0] + "'!");
            }
        }

        if (insideMacro)
        {
            error("End of file reached while parsing a macro directive! "
                  "Last macro seen '" + currentMacro.name + "'.");
        }

        if (!isIncludeFile)
        {
            if (!foundProgStart)
            {
                std::cout << "WARNING: Program start directive '#vuprog' was not found!\n";
            }
            if (!foundProgEnd)
            {
                std::cout << "WARNING: Program end directive '#endvuprog' was not found!\n";
            }
        }

        return { std::move(includes), std::move(defines), std::move(macros) };
    }
}; // class Preprocessor

// ========================================================
// isDefName/isMacroName:
// ========================================================

static inline bool isDefName(const std::string & s, const long pos, const long len)
{
    //
    // A macro/define name referenced in the text/code must
    // be either surrounded by whitespace or punctuation.
    //
    // E.g.: func(FOO+42);
    // Where 'FOO' is a #define constant.
    //
    // This checking is necessary to avoid replacing
    // accidental things like in a "FOOBAR" string.
    //

    const long prevChar = pos - 1;
    const long postChar = pos + len;

    // At the left end
    if (prevChar <= 0)
    {
        return (postChar >= static_cast<long>(s.length())) ||
               (std::isspace(s[postChar]) || std::ispunct(s[postChar]));
    }
    // At the right end
    if (postChar >= static_cast<long>(s.length()))
    {
        return (prevChar <= 0) ||
               (std::isspace(s[prevChar]) || std::ispunct(s[prevChar]));
    }
    // In the middle
    return (std::isspace(s[prevChar]) || std::ispunct(s[prevChar])) &&
           (std::isspace(s[postChar]) || std::ispunct(s[postChar]));
}

static inline bool isMacroName(const std::string & s, const long pos, const long len)
{
    const long prevChar = pos - 1;
    const long postChar = pos + len;

    // At the left end
    if (prevChar <= 0)
    {
        // Must have a '{' to the right-hand side
        return postChar < static_cast<long>(s.length()) && s[postChar] == '{';
    }
    // At the right end
    if (postChar >= static_cast<long>(s.length()))
    {
        // Missing the '{'?
        return false;
    }
    // In the middle
    return (std::isspace(s[prevChar]) || std::ispunct(s[prevChar])) && (s[postChar] == '{');
}

// ========================================================
// doReplaceDefs():
// ========================================================

static inline void doReplaceDefs(std::string & line, const std::string & search, const std::string & replace)
{
    for (std::size_t pos = 0; ; pos += replace.length())
    {
        if ((pos = line.find(search, pos)) == std::string::npos)
        {
            break;
        }
        if (isDefName(line, pos, search.length()))
        {
            line.erase(pos, search.length());
            line.insert(pos, replace);
        }
    }
}

// ========================================================
// resolveDefines():
// ========================================================

static std::vector<std::string> resolveDefines(const std::vector<std::string> & codeLines,
                                               const std::vector<Directives>  & directives)
{
    std::vector<std::string> expandedDefs;

    // We have to test each line of the source with each #define
    // found inside the source file plus all of its #includes, so
    // you can imagine this triple looping is not very scalable.
    // Luckily our inputs are small, so this rudimentary text
    // replacement still performs reasonably well.
    for (auto line : codeLines)
    {
        for (const auto & dir : directives)
        {
            for (const auto & def : dir.defines)
            {
                doReplaceDefs(line, def.name, def.value);
            }
        }
        expandedDefs.emplace_back(std::move(line));
    }

    return expandedDefs;
}

// ========================================================
// doMacroExpansion():
// ========================================================

static void doMacroExpansion(std::string & line, const MacroBlock & macro)
{
    if (macro.lines.empty())
    {
        line.clear();
        return;
    }

    // Split the line by whitespace to get the macro params:
    std::istringstream tokenizer{ line };
    std::vector<std::string> params{ std::istream_iterator<std::string>{ tokenizer },
                                     std::istream_iterator<std::string>{} };

    // Do minimal parameter validation:
    if (!macro.params.empty())
    {
        if ((params.size() - 2) != macro.params.size())
        {
            std::cerr << "ERROR: Macro '" << macro.name << "' takes "
                      << macro.params.size() << " arguments, but "
                      << (params.size() - 2) << " were provided!" << std::endl;

            throw std::runtime_error("Not enough arguments in macro invocation.");
        }
    }
    else
    {
        if (params.size() > 2)
        {
            std::cerr << "ERROR: Macro '" << macro.name << "' takes no arguments, but "
                      << (params.size() - 2) << " were provided!" << std::endl;

            throw std::runtime_error("Too many arguments in macro invocation.");
        }
    }

    // Replace the parameters, if any:
    auto macroBody = macro.lines;
    if (!macro.params.empty())
    {
        auto stripCommas = [](std::string & s) -> std::string &
        {
            if (s.back()  == ',') { s.pop_back();    }
            if (s.front() == ',') { s = s.substr(1); }
            return s;
        };

        for (auto & l : macroBody)
        {
            for (std::size_t p = 0; p < macro.params.size(); ++p)
            {
                doReplaceDefs(l, macro.params[p], stripCommas(params[p + 1]));
            }
        }
    }

    // Expand the macro body into the out:
    line = "\n";
    for (auto && l : macroBody)
    {
        line += l;
        line += "\n";
    }
}

// ========================================================
// resolveMacos():
// ========================================================

static std::vector<std::string> resolveMacos(const std::vector<std::string> & codeLines,
                                             const std::vector<Directives>  & directives)
{
    std::vector<std::string> expandedMacros;

    // Same idea as in resolveDefines(): compare each line with each possible macro.
    // NOTE: For simplicity, assume at most one macro invocation per line!
    for (auto line : codeLines)
    {
        for (const auto & dir : directives)
        {
            for (const auto & mc : dir.macros)
            {
                const auto pos = line.find(mc.name);
                if (pos != std::string::npos && isMacroName(line, pos, mc.name.length()))
                {
                    doMacroExpansion(line, mc);
                    goto BREAKOUT; // Not related to the classic arcade game :P
                }
            }
        }
    BREAKOUT:
        expandedMacros.emplace_back(std::move(line));
    }

    return expandedMacros;
}

// ========================================================
// writeVcl[Prologue/Epilogue]:
// ========================================================

static void writeVclPrologue(std::ofstream & outFile)
{
    outFile << "\n";
    outFile << ".init_vf_all\n";
    outFile << ".init_vi_all\n";
    outFile << ".syntax new\n";
    outFile << ".vu\n";
    outFile << "\n";
    outFile << "--enter\n";
    outFile << "--endenter\n";
    outFile << "\n";
}

static void writeVclEpilogue(std::ofstream & outFile)
{
    outFile << "\n";
    outFile << "--exit\n";
    outFile << "--endexit\n";
    outFile << "\n";
}

// ========================================================
// stripComments():
// ========================================================

static void stripComments(std::string & s)
{
    // The only type of comment we handle is ';'
    const auto pos = s.find_first_of(';');
    if (pos == std::string::npos)
    {
        return; // No comments in this line.
    }
    // Remove everything after the comment start.
    s.erase(pos, s.length());
}

// ========================================================
// runPreprocessor():
// ========================================================

static void runPreprocessor(std::string srcFile, std::string destFile, const bool addVclJunk)
{
    // Source file is the root where substitutions take place.
    Preprocessor srcPP{ std::move(srcFile), false };
    auto srcDirectives = srcPP.parseDirectives();

    // Try to open the #included files:
    int includesFailedToOpen = 0;
    std::vector<std::unique_ptr<Preprocessor>> includePPs;

    for (auto && inc : srcDirectives.includes)
    {
        try
        {
            includePPs.emplace_back(std::make_unique<Preprocessor>(inc, true));
        }
        catch (...)
        {
            // We want to log all the #includes that fail to open.
            includesFailedToOpen++;
        }
    }

    if (includesFailedToOpen != 0)
    {
        throw std::runtime_error("Failed to open include file(s).");
    }

    // Now we run the same preprocessing for each of the #includes,
    // but if the included files have other includes we'll ignore them.
    // There's no support for recursive includes right now.
    std::vector<Directives> additionalDirectives;
    additionalDirectives.reserve(includePPs.size());

    for (auto && pp : includePPs)
    {
        auto && dir = pp->parseDirectives();
        if (!dir.includes.empty())
        {
            std::cerr << "ERROR: File " << pp->getCurrentFileName()
                      << ": Include directives are not allowed inside #included files!"
                      << std::endl;

            throw std::runtime_error("Recursive includes.");
        }
        additionalDirectives.emplace_back(dir);
    }

    // We can release this memory now.
    includePPs.clear();

    // Now that the list of dependencies is resolved and we
    // have all macros and defines, we can substitute in the
    // source file.
    const auto & srcCodeLines = srcPP.getCodeLines();

    // Merge 'em:
    additionalDirectives.emplace_back(std::move(srcDirectives));

    // #macro expansion:
    auto expandedMacros = resolveMacos(srcCodeLines, additionalDirectives);

    // #define expansion and we are done:
    auto finalProcessedText = resolveDefines(expandedMacros, additionalDirectives);

    //
    // Finally, write the output file:
    //
    std::ofstream outFile{ destFile };

    if (!outFile.is_open() || !outFile.good())
    {
        std::cerr << "Unable to open file \"" << destFile << "\" for writing." << std::endl;
        throw std::runtime_error("Can't open output file.");
    }

    if (addVclJunk)
    {
        writeVclPrologue(outFile);
    }

    for (auto && line : finalProcessedText)
    {
        stripComments(line);
        if (!isBlank(line))
        {
            outFile << line << "\n";
        }
    }

    if (addVclJunk)
    {
        writeVclEpilogue(outFile);
    }
}

// ========================================================
// removeFilenameExtension():
// ========================================================

static std::string removeFilenameExtension(const std::string & filename)
{
	const auto lastDot = filename.find_last_of('.');
	if (lastDot == std::string::npos)
	{
		return filename;
	}
	return filename.substr(0, lastDot);
}

// ========================================================
// printHelpText():
// ========================================================

static void printHelpText(const char * progName)
{
    std::cout << "\n"
        << "Usage:\n"
        << " $ " << progName << " <input-file> [output-file] [options]\n"
        << " Applies custom preprocessing to a source file prior to running VCL.\n"
        << " This preprocessor supports C-style #define constants and custom #macro directives.\n"
        << " If no output filename is provided the input name is used but the extension is replaced with '.vsm'\n"
        << " Options are:\n"
        << "  -h, --help     Prints this message and exits.\n"
        << "  -j, --vcljunk  Adds the standard VCL prologue/epilogue junk to the output.\n"
        << "\n"
        << "Created by Guilherme R. Lampert, " << __DATE__ << ".\n";
}

// ========================================================
// main() entry point:
// ========================================================

int main(int argc, const char * argv[])
{
    if (argc <= 1)
    {
        printHelpText(argv[0]);
        return EXIT_FAILURE;
    }

    if (argv[1][0] == '-')
    {
        if (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)
        {
            printHelpText(argv[0]);
            return EXIT_SUCCESS;
        }
    }

    const std::string inFileName{ argv[1] };

    // Check for a flag in the wrong place/empty string...
    if (inFileName.empty() || inFileName[0] == '-')
    {
        std::cerr << "Invalid filename \"" << inFileName << "\"!\n";
        return EXIT_FAILURE;
    }

    std::string outFileName;
    if (argc >= 3 && argv[2][0] != '-') // Output name provided?
    {
        outFileName = argv[2];
    }
    else // Use same name as input, changing the extension.
    {
        outFileName = removeFilenameExtension(inFileName) + ".vsm";
    }

    bool addVclJunk = false;
    if (argc >= 3)
    {
        if (argc == 3 &&
            (std::strcmp(argv[2], "-j") == 0 ||
             std::strcmp(argv[2], "--vcljunk") == 0))
        {
            addVclJunk = true;
        }
        else if (argc >= 4 &&
                 (std::strcmp(argv[3], "-j") == 0 ||
                  std::strcmp(argv[3], "--vcljunk") == 0))
        {
            addVclJunk = true;
        }
    }

    try
    {
        runPreprocessor(inFileName, outFileName, addVclJunk);
        return EXIT_SUCCESS;
    }
    catch (...)
    {
        std::cerr << "Terminating due to previous error(s)..." << std::endl;
        return EXIT_FAILURE;
    }
}
