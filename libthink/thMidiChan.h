/* $Id: thMidiChan.h,v 1.19 2003/05/30 00:55:42 aaronl Exp $ */

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

		static void DestroyMap (map<string,thMidiChan*> themap)
		{
			DESTROYBODY(string,thMidiChan);
		}
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
