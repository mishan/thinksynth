#ifndef THF_PREFS_H
#define THF_PREFS_H

class thfPrefs
{
public:
	thfPrefs (thSynth *argsynth);
	thfPrefs (thSynth *argsynth, const string &path);
	~thfPrefs (void);

	void Set (const string &key, string **vals);
	string **Get (const string &key);

	void Load (void);
	void Save (void);
private:
	thSynth *synth;
	map <string, string**> prefs;
	string prefsPath;
};

#endif /* THF_PREFS_H */
