/* $Id: gthSignal.h,v 1.5 2004/04/07 04:09:47 misha Exp $ */

#ifndef SIGNAL_H
#define SIGNAL_H

typedef SigC::Signal3<void, int, float, float> sigNoteOn;
typedef SigC::Signal2<void, int, float> sigNoteOff;

extern sigNoteOn  m_sigNoteOn;
extern sigNoteOff m_sigNoteOff;

//extern sigReadyWrite m_sigReadyWrite;
//extern sigMidiEvent m_sigMidiEvent;

#endif /* SIGNAL_H */
