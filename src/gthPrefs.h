#ifndef GTH_PREFS_H
#define GTH_PREFS_H

#define PREFS_FILE ".thinkrc"

class gthPrefs
{
public:
	gthPrefs (thSynth *argsynth);
	gthPrefs (thSynth *argsynth, const string &path);
	~gthPrefs (void);

	void Set (const string &key, string **vals);
	string **Get (const string &key);

	void Load (void);
	void Save (void);
private:
	thSynth *synth;
	map <string, string**> prefs;
	string prefsPath;
};

#endif /* GTH_PREFS_H */
