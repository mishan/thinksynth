/*
 * Copyright (C) 2004-2026 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef THCGENFILE_H
#define THCGENFILE_H

#include <map>
#include <string>
#include <vector>

#include "thLexer.h"

class thArg;
class thcPlugin;
class thcScheduler;
struct thcStage;

/* One token of .gen source, with its byte span. Public rather than a
 * loader detail because thcGenEdit splices files by replacing exact
 * token spans -- the same lexer feeds the reader and the writer, which
 * is what guarantees an edit never moves a byte it was not aimed at
 * (comments included; they live between the spans).
 *
 * Not thLexToken itself, though it is built from one. The shared lexer
 * is deliberately language-neutral: it hands back `-' and `5' as two
 * tokens because .dsp has arithmetic, and `@' and `density' as two
 * because .dsp has an operator there. .gen has neither, and its parser
 * is much clearer reading a NUMBER of -5 and a KNOB called density than
 * re-deciding that at every call site. tokenize() below is where the
 * one shape becomes the other. */
struct thcGenToken
{
    enum Kind { WORD, NUMBER, STRING, KNOB, PUNCT, MODSEP, END } kind;

    std::string text;    /* WORD/STRING body/KNOB name, PUNCT char      */
    double      num;
    int         line;
    size_t      off;     /* where the token starts in the source        */
    size_t      end;     /* one past where it stops (quotes included)   */
};

/* Loads a .gen file into a scheduler. See GEN_FORMAT.md for the language.
 *
 * A hand-rolled recursive-descent parser rather than the bison/flex
 * additions COMPOSITION_HANDOFF.md §5 originally sketched, and the
 * deviation is deliberate. thinklang.yy builds a thSynthTree as it goes
 * and folds `ms' into *samples* inside the grammar -- engine semantics a
 * .gen value must not inherit. And the loader has to resolve stage names
 * against thcPlugin and build thcScheduler chains, both of which live in
 * src/, which libthink cannot see; a shared grammar would have meant a
 * neutral AST in libthink plus a second walker here. Recursive descent
 * also says what is wrong by name and line far better than yacc does,
 * which for a file format people hand-write is most of the job.
 *
 * The *lexical* layer is shared, and that is the half worth sharing. It
 * began here as a faithful copy of thinklex.ll's rules -- cheap to write
 * and a standing invitation to drift the next time either language grew
 * a token. It is now the flex lexer itself, extracted into libthink as
 * thLexer.h and taught to hand back tokens with byte offsets. `#'
 * comments, `;', `=', `::', `{ }', quoted strings and numeric literals
 * are one implementation for both languages, and tokenize() below is a
 * short adapter rather than a second scanner.
 *
 * Everything is validated by name and line: a stage asking gen:: of a
 * plugin that exports no tick, a param the plugin never registered, a
 * knob referenced before it is declared, a bare number on a duration --
 * each error names the file, the line and the thing, and a file with any
 * error loads nothing (the scheduler is left empty, not half-built).
 */
class thcGenLoader
{
public:
    /* `plugins' is the loaded-module map, keyed by name ("eno_line"),
       and must outlive the chains built from it. */
    explicit thcGenLoader (const std::map<std::string, thcPlugin *> &plugins);

    /* Parses `path' and builds the piece into `sched'. The scheduler's
       existing chains and knobs are cleared first, success or fail.
       False on any error; errors() says what and where. */
    bool load (const std::string &path, thcScheduler *sched);

    const std::vector<std::string> &errors (void) const { return errors_; }

    /* Piece info, valid after a successful load. */
    const std::string &pieceName (void) const { return name_; }
    const std::string &pieceAuthor (void) const { return author_; }
    const std::string &pieceDescription (void) const { return description_; }

    bool     hasSeed (void) const { return hasSeed_; }
    unsigned seed (void) const { return seed_; }

    /* The one shared pitch parser the format promises: "F3 Ab3 C4" or
       "F3,Ab3,C4" to MIDI numbers. [A-G], optional # or b, octave;
       middle C is C4. On failure `bad' holds the offending token. */
    static bool parseNoteList (const std::string &text,
                               std::vector<int> &out, std::string &bad);

    /* The inverse, for anything writing note text: 60 -> "C4". Flats
       for the black keys, because that is how the shipped piece spells
       them. Empty for a value off the MIDI range. */
    static std::string noteName (int midi);

    /* The lexical layer in .gen's shape: `out' gets an END-terminated
       token stream with byte spans. False on a lexical error, with `err'
       and `errLine' saying what and where. thcGenEdit shares this, which
       is what keeps "the editor and the loader read the same language"
       true by construction; thLexString does the scanning under it,
       which is what keeps that language the same one .dsp is written in. */
    static bool tokenize (const std::string &text,
                          std::vector<thcGenToken> &out,
                          std::string &err, int &errLine);

private:
    typedef thcGenToken Token;

    bool lex (const std::string &path, std::vector<Token> &out);

    /* Recursive descent over the token stream. Each returns false after
       recording an error; the top level then skips to the next `;' at
       depth zero so one mistake does not cascade into fifty. */
    bool parseStatement (thcScheduler *sched);
    bool parseKnobStatement (thcScheduler *sched);
    bool parseScale (void);
    bool parseChain (thcScheduler *sched);
    bool parseStageBlock (thcScheduler *sched, size_t chain,
                          const std::string &chainName);
    bool parseSinkBlock (thcScheduler *sched, size_t chain);
    bool parseParam (thcScheduler *sched, thcStage *stage,
                     const std::string &stageName);

    const Token &peek (void) const;
    Token        take (void);
    bool         expectPunct (char c);
    void         skipStatement (void);

    void error (int line, const std::string &msg);

    const std::map<std::string, thcPlugin *> &plugins_;

    std::vector<Token> tokens_;
    size_t             pos_;
    std::string        path_;

    std::vector<std::string> errors_;

    std::map<std::string, std::vector<int> > scales_;

    std::string name_, author_, description_;
    bool        hasSeed_;
    unsigned    seed_;
};

#endif /* THCGENFILE_H */
