/* $Id: gthSignal.h,v 1.3 2004/04/06 19:03:55 misha Exp $ */

#ifndef SIGNAL_H
#define SIGNAL_H

typedef SigC::Signal3<void, int, float, float> sigNoteOn;
typedef SigC::Signal2<void, int, float> sigNoteOff;

/* additional arguments are usually bound to the callbacks of this signal */
typedef SigC::Signal0<void> sigReadyWrite;
typedef SigC::Signal0<int> sigMidiEvent;

extern sigNoteOn  m_sigNoteOn;
extern sigNoteOff m_sigNoteOff;

extern sigReadyWrite m_sigReadyWrite;
extern sigMidiEvent m_sigMidiEvent;

#endif /* SIGNAL_H */
