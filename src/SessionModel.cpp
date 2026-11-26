/*
 * Copyright (C) 2004-2026 The thinksynth authors
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

#include <cmath>

#include "SessionModel.h"

bool
SessionModel::build (const std::string &filename, int playing, int cued,
                     thSession &out, std::string &why)
{
    thcGenEdit::Doc doc;
    std::vector<thcGenEdit::SectionText> sections;
    thcGenEdit::Ending ending;

    out = last_ = thSession();

    if (thcGenEdit::describe(filename, doc, why) != thcGenEdit::OK ||
        thcGenEdit::sections(filename, sections, ending, why) !=
            thcGenEdit::OK)
        return false;

    out.ending = ending.ends ? thSession::ENDS
               : ending.loopTo.empty() ? thSession::CYCLES
                                       : thSession::LOOPS;
    out.loopTo = ending.loopTo;
    out.cueKnob = ending.cues.empty() ? "" : ending.cues.front();

    for (size_t i = 0; i < sections.size(); i++)
    {
        thSession::Column c;

        c.name = sections[i].name;
        c.length = sections[i].length;
        c.hold = sections[i].hold;
        c.playing = (int)i == playing;
        c.cued = (int)i == cued;
        out.columns.push_back(c);
    }

    /* A row's cells, from what each section writes under `key'. */
    auto cells = [&sections] (const std::string &key)
    {
        std::vector<thSession::Cell> v;

        for (const auto &s : sections)
        {
            thSession::Cell cell;

            for (const auto &l : s.levels)
                if (l.first == key)
                {
                    cell.written = true;
                    cell.value = l.second;
                }

            v.push_back(cell);
        }

        return v;
    };

    for (const auto &ch : doc.chains)
    {
        thSession::Row r;

        r.name = ch.name;
        r.cells = cells(ch.name);

        for (const auto &s : ch.sinks)
        {
            r.notes = r.notes || (s.knob.empty() && s.chanarg.empty());
            r.cues = r.cues || (!s.knob.empty() && s.knob == out.cueKnob);
        }

        out.rows.push_back(r);
    }

    for (const auto &k : doc.knobs)
    {
        thSession::Row r;

        r.name = k.name;
        r.knob = true;
        r.cells = cells("@" + k.name);
        out.rows.push_back(r);
    }

    last_ = out;
    return true;
}

const thSession::Row *
SessionModel::row (const std::string &name) const
{
    for (const auto &r : last_.rows)
        if ((r.knob ? "@" + r.name : r.name) == name)
            return &r;

    return nullptr;
}

int
SessionModel::column (const std::string &name) const
{
    for (size_t i = 0; i < last_.columns.size(); i++)
        if (last_.columns[i].name == name)
            return (int)i;

    return -1;
}

bool
SessionModel::propose (const thSessionEdit &edit, std::string &why,
                       std::string &warn) const
{
    using K = thSessionEdit;
    const bool reorders = edit.kind == K::ADD || edit.kind == K::DUPLICATE ||
                          edit.kind == K::REMOVE || edit.kind == K::MOVE;

    warn.clear();

    if (reorders && !last_.cueKnob.empty())
    {
        why = "@" + last_.cueKnob + " picks this piece's sections by number, "
              "so adding, moving or taking one out would change what its "
              "values mean";
        return false;
    }

    const bool needsSection = !(edit.kind == K::END ||
                                ((edit.kind == K::ADD || edit.kind == K::LOOP)
                                 && edit.section.empty()));

    if (needsSection && column(edit.section) < 0)
    {
        why = "no section called " + edit.section;
        return false;
    }

    if (edit.kind == K::MOVE && !edit.after.empty() &&
        column(edit.after) < 0)
    {
        why = "no section called " + edit.after;
        return false;
    }

    if (edit.kind != K::LEVEL)
        return true;

    const thSession::Row *r = row(edit.row);

    if (r == nullptr)
    {
        why = "nothing called " + edit.row;
        return false;
    }

    if (!r->knob && !(edit.value >= 0))
    {
        why = "a level is 0 or more";
        return false;
    }

    if (r->cues && edit.value == 0)
        warn = "muting " + r->name + " in " + edit.section + " stops it "
               "cueing the sections through @" + last_.cueKnob;

    return true;
}

thcGenEdit::Result
SessionModel::apply (const std::string &filename,
                     const thSessionEdit &edit, std::string &why)
{
    using K = thSessionEdit;
    const thSession::Row *r = row(edit.row);

    switch (edit.kind)
    {
        case K::LEVEL:
            return thcGenEdit::setSectionLevel(filename, edit.section,
                edit.row,
                r != nullptr && r->knob && !edit.set ? NAN : edit.value, why);
        case K::ADD:
            return thcGenEdit::addSection(filename, edit.name,
                edit.length.empty() ? "1 bars" : edit.length, edit.section,
                {}, why);
        case K::DUPLICATE:
            return thcGenEdit::duplicateSection(filename, edit.section,
                                                edit.name, why);
        case K::REMOVE:
            return thcGenEdit::removeSection(filename, edit.section, why);
        case K::RENAME:
            return thcGenEdit::renameSection(filename, edit.section,
                                             edit.name, why);
        case K::LENGTH:
            return thcGenEdit::setSectionLength(filename, edit.section,
                                                edit.length, why);
        case K::HOLD:
            return thcGenEdit::setSectionHold(filename, edit.section,
                                              edit.set, why);
        case K::MOVE:
            return thcGenEdit::moveSection(filename, edit.section,
                                           edit.after, why);
        case K::END:
            return thcGenEdit::setSectionEnd(filename, edit.set, why);
        case K::LOOP:
            return edit.section.empty()
                ? thcGenEdit::setSectionEnd(filename, false, why)
                : thcGenEdit::setSectionLoop(filename, edit.section, why);
    }

    why = "not an edit the grid makes";
    return thcGenEdit::REFUSED;
}

thSessionEdit
SessionModel::toggle (const std::string &section, const std::string &chain)
{
    thSessionEdit e;
    const thSession::Row *r = row(chain);
    const int c = column(section);
    const double now = r != nullptr && c >= 0 && r->cells[c].written
        ? r->cells[c].value : 1;
    const auto key = std::make_pair(section, chain);

    e.kind = thSessionEdit::LEVEL;
    e.section = section;
    e.row = chain;

    if (now != 0)
    {
        remembered_[key] = now;
        e.value = 0;
    }
    else
    {
        const auto it = remembered_.find(key);

        e.value = it == remembered_.end() ? 1 : it->second;
    }

    return e;
}
