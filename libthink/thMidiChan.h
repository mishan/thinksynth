/* $Id: thMidiChan.h,v 1.20 2003/06/03 23:05:06 aaronl Exp $ */

#ifndef TH_MIDICHAN_H
#define TH_MIDICHAN_H 1

class thMidiChan {
	public:
		thMidiChan (thMod *mod, float amp, int windowlen);
		~thMidiChan (void);

		thMidiNote *AddNote (float note, float velocity);
		void DelNote (int note);

		void SetArg (thArg *arg);

		void Process (void);

		float *GetOutput (void) const { return output; }
		int GetChannels (void) const { return channels; }

	private:
		int GetLen(int);

		bool dirty;
		thMod *modnode;
		map<string, thArg*> args;
		map<int, thMidiNote*> notes;
		int channels, windowlength;
		float *output;
		int outputnamelen;
};

#endif /* TH_MIDICHAN_H */
