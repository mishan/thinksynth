/* $Id: gthSignal.h,v 1.2 2004/04/06 04:07:36 misha Exp $ */

#ifndef SIGNAL_H
#define SIGNAL_H

typedef SigC::Signal3<void, int, float, float> sigNoteOn;
typedef SigC::Signal2<void, int, float> sigNoteOff;

extern sigNoteOn  m_sigNoteOn;
extern sigNoteOff m_sigNoteOff;

#endif /* SIGNAL_H */
