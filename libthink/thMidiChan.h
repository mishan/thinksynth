/* $Id: thMidiChan.h,v 1.17 2003/05/07 16:10:37 misha Exp $ */

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
		void ProcessHelper (thBSTree *note);
		int GetLen(int);

		thMod *modnode;
		thBSTree *args, *notes;
		int channels, windowlength;
		float *output;
		int outputnamelen;
};

#endif /* TH_MIDICHAN_H */
