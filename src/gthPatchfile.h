#ifndef GTH_PATCHFILE_H
#define GTH_PATCHFILE_H

class gthPatchfile 
{
	gthPatchfile (thSynth *synth);
	~gthPatchfile (void);

	bool parse (char *filename);
private:
	thSynth *synth_;
};

#endif /* GTH_PATCHFILE_H */
