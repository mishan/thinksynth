/* $Id: gthSignal.h,v 1.6 2004/06/30 04:36:29 misha Exp $ */

#ifndef GTH_SIGNAL_H
#define GTH_SIGNAL_H

typedef SigC::Signal3<void, int, float, float> sigNoteOn;
typedef SigC::Signal2<void, int, float> sigNoteOff;

extern sigNoteOn  m_sigNoteOn;
extern sigNoteOff m_sigNoteOff;

//extern sigReadyWrite m_sigReadyWrite;
//extern sigMidiEvent m_sigMidiEvent;

#endif /* GTH_SIGNAL_H */
