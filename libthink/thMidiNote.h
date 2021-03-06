/*
 * Copyright (C) 2004-2014 Metaphonic Labs
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

#ifndef TH_MIDINOTE_H
#define TH_MIDINOTE_H 1

class thMidiNote {
public:
    thMidiNote (thSynthTree *tree, float note, float velocity);
    thMidiNote (thSynthTree *tree);
    ~thMidiNote ();
    
    thSynthTree *synthTree (void) { return &synthTree_; }
    int id (void) const { return noteid_; }

    void process (int length);

    void setArg (const string &name, float value);
    void setArg (const string &name, const float *value, int len);

private:
    thSynthTree synthTree_;
    int noteid_;
};

#endif /* TH_MIDINOTE_H */
