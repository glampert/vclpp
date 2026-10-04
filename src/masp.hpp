// ================================================================================================
// -*- C++ -*-
// File: masp.hpp
// Brief: The first stage of MASP mode: the macro language of masp and GASP, the assembler
//        preprocessors that classic VU code, like ps2gl's, was written for.
//
// This source code is released under the MIT license.
// See the accompanying LICENSE file for details.
// ================================================================================================

#pragma once

#include "tokenizer.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vclpp
{

class diagnostics;
class expression_evaluator;
class source_manager;

struct masp_options final
{
    std::string                                      comment_chars = ";"; // ';', and any given with -c.
    std::vector<std::pair<std::string, std::string>> variables;           // -D name=value
};

// Reads a program written for masp and carries out its directives: .macro, .aif,
// .arepeat, .awhile, .assigna/.assignc, .equ, .include and .end. It yields the lines
// that result for the engine, which runs the C preprocessor over them, as cpp would run
// over masp's output. Lines that start with '#' are C directives, passed on untouched.
//
// As in masp, macros expand into text, a line at a time: '\param' is replaced wherever it
// appears, even inside a word ('temp\@'), and the expanded lines are read again as input,
// so they can hold directives and invoke other macros.
class masp_reader final
{
public:
    masp_reader(std::uint32_t main_file, const masp_options & opts, source_manager & sources,
                diagnostics & diags, expression_evaluator & evaluator);

    masp_reader(const masp_reader &) = delete;
    masp_reader & operator = (const masp_reader &) = delete;

    // The next line of the program, as tokens. Returns false at its end.
    bool next_line(std::vector<pp_token> & line);

    // Hands a line back, for next_line() to return again.
    void unget_line(std::vector<pp_token> line);

private:
    // A line of source, with the lines continuing it ('+' in column 1) joined to it and
    // its comments removed.
    struct text_line final
    {
        std::string     text;
        source_location location; // Of its first line.
    };

    struct macro_def final
    {
        std::string                                      name;   // As written in its .macro.
        std::vector<std::pair<std::string, std::string>> params; // Each name with its default value.
        std::vector<text_line>                           body;
    };

    enum class frame_kind : std::uint8_t
    {
        file,
        macro, // An expansion.
        loop   // An .arepeat or .awhile body, replayed.
    };

    // Where lines are read from.
    struct frame final
    {
        frame_kind                                       kind = frame_kind::file;
        std::vector<text_line>                           lines;
        std::size_t                                      next = 0;
        std::size_t                                      conditional_depth = 0; // m_conditionals.size() on entry.
        std::shared_ptr<const expansion_origin>          origin;    // The expansion its lines come out of, if any.
        std::vector<std::pair<std::string, std::string>> arguments; // macro: each parameter with its value.
        std::string                                      counter;   // macro: the value of \@.
        std::uint64_t                                    repeats_left = 0; // loop: .arepeat passes after this one.
        std::optional<text_line>                         while_condition;  // loop: .awhile's, before each pass.
        std::uint64_t                                    passes = 0;
    };

    // An .aif and its .aelse.
    struct conditional final
    {
        source_location opened_at;
        bool            parent_active;
        bool            branch_taken;
        bool            active;
        bool            seen_else;
    };

    // A line taken apart: "label: word operand" or "label: .directive operand", or
    // "SYMBOL .equ operand" for the directives that name a symbol in column 1.
    struct statement final
    {
        std::string label;         // A name in column 1, with its colon, which masp lets it go without.
        std::size_t label_end = 0; // Where the label ends in the line, its colon included.
        std::string symbol;        // The column-1 name of .equ, .assigna and .assignc.
        std::string directive;     // A '.' word, lower-cased, without the '.'.
        std::string word;          // Otherwise the first word, which may name a macro.
        std::string operand;       // The rest of the line, after the directive or the word.
    };

    // Reading.
    std::vector<text_line> split_lines(std::uint32_t file_index) const;
    std::string strip_comments(std::string_view line, bool & in_block_comment) const;
    bool read_line(text_line & line);
    void end_frame();
    void pop_frame();
    std::vector<text_line> capture_body(std::string_view open, std::string_view close, const text_line & opener);

    // Lines.
    static statement parse_statement(const std::string & text);
    static std::string with_label(const statement & st, const std::string & text);
    bool process_line(const text_line & raw, std::vector<pp_token> & out);
    bool carry_out_directive(const statement & st, const text_line & raw);
    void assign_symbol(const statement & st, const text_line & raw);
    bool make_line(const std::string & text, const text_line & raw, std::vector<pp_token> & out);

    // Directives.
    void define_macro(const statement & st, const text_line & raw);
    void invoke_macro(const macro_def & m, const std::string & arguments, const text_line & raw);
    void exit_macro(const text_line & raw);
    void open_conditional(const statement & st, const text_line & raw);
    void else_conditional(const text_line & raw);
    void close_conditional(const text_line & raw);
    void repeat(const statement & st, const text_line & raw);
    void loop_while(const statement & st, const text_line & raw);
    void include_file(const statement & st, const text_line & raw);

    // Substitution and evaluation.
    static std::string substitute_arguments(const std::string & text, const frame & expansion);
    std::string substitute_symbols(const std::string & text, const text_line & raw) const;
    std::int64_t evaluate(const std::string & text, const text_line & raw, std::string_view context);
    bool evaluate_condition(const std::string & text, const text_line & raw, std::string_view context);

    // Helpers.
    bool is_active() const;
    std::size_t frame_conditional_depth() const;
    std::shared_ptr<const expansion_origin> current_origin() const;
    pp_token at(const text_line & raw) const;
    [[noreturn]] void error(const text_line & raw, const std::string & message) const;

    const masp_options &                         m_options;
    source_manager &                             m_sources;
    diagnostics &                                m_diags;
    expression_evaluator &                       m_evaluator;
    const std::uint32_t                          m_main_file;
    std::vector<frame>                           m_frames;
    std::vector<conditional>                     m_conditionals;
    std::unordered_map<std::string, macro_def>   m_macros;    // By lower-cased name: names ignore case.
    std::unordered_map<std::string, std::string> m_variables; // .assigna/.assignc, read as \&NAME.
    std::unordered_map<std::string, std::string> m_equates;   // .equ, by name.
    std::vector<std::vector<pp_token>>           m_put_back;
    std::uint64_t                                m_expansions    = 0;     // \@ of the next expansion.
    bool                                         m_blank_pending = false; // Set the next line off by a blank one.
    bool                                         m_ended         = false; // .end was reached.
};

} // namespace vclpp
