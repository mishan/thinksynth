/* $Id: KeyboardWindow.cpp,v 1.5 2004/04/01 07:44:49 misha Exp $ */

#include "config.h"
#include "think.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <gtkmm.h>
#include <gtkmm/messagedialog.h>

#include "thArg.h"
#include "thPlugin.h"
#include "thPluginManager.h"
#include "thNode.h"
#include "thMod.h"
#include "thMidiNote.h"
#include "thMidiChan.h"
#include "thSynth.h"

#include "KeyboardWindow.h"

/* conversion table for SHIFT chars */
static char	key_conv_in[] =  "~!@#$%^&*()_+|{}:\"<>?";
static char	key_conv_out[] = "`1234567890-=\\[];\',./";
/* list of keys */
static char	keylist_1[] = "1q2w3e4r5t6y7u8i9o0p-[=]\\";
static char	keylist_2[] = "azsxdcfvgbhnjmk,l.;/\'";
/* base keys */
static char	base_keys[] = "CDEFGAB";

static unsigned int	color0 = 0x00000000;	/* key border			*/
static unsigned int	color1 = 0x00FFFFFF;	/* white key			*/
static unsigned int	color2 = 0x00000000;	/* black key			*/
static unsigned int	color3 = 0x00C0FFFF;	/* A (440 Hz) key		*/
static unsigned int	color4 = 0x00D0D0D0;	/* white key / active		*/
static unsigned int	color5 = 0x00707070;	/* black key / active		*/
static unsigned int	color6 = 0x0090D0D0;	/* A (440 Hz) key / active	*/

static int	key_sizes[4][7] =
{
	/* keyboard size 0: 450x45 pixels */
	{
		26,		/* black key height			*/
		19,		/* total height - black key height	*/
		6,		/* white key total width		*/
		3,		/* black key width			*/
		4,		/* white key width 1 (C, F)		*/
		3,		/* white key width 2 (D, G, A)		*/
		4		/* white key width 3 (E, B)		*/
	},
	/* keyboard size 1: 600x60 pixels */
	{
		35,		/* black key height			*/
		25,		/* total height - black key height	*/
		8,		/* white key total width		*/
		5,		/* black key width			*/
		5,		/* white key width 1 (C, F)		*/
		3,		/* white key width 2 (D, G, A)		*/
		5		/* white key width 3 (E, B)		*/
	},
	/* keyboard size 2: 750x75 pixels */
	{
		43,		/* black key height			*/
		32,		/* total height - black key height	*/
		10,		/* white key total width		*/
		6,		/* black key width			*/
		7,		/* white key width 1 (C, F)		*/
		4,		/* white key width 2 (D, G, A)		*/
		6		/* white key width 3 (E, B)		*/
	},
	/* keyboard size 3: 1050x105 pixels */
	{
		61,		/* black key height			*/
		44,		/* total height - black key height	*/
		14,		/* white key total width		*/
		8,		/* black key width			*/
		10,		/* white key width 1 (C, F)		*/
		6,		/* white key width 2 (D, G, A)		*/
		9		/* white key width 3 (E, B)		*/
	}
};

KeyboardWindow::KeyboardWindow (thSynth *argsynth)
	: ctrlFrame ("Keyboard Control"), chanLbl("Channel")
{
	synth = argsynth;

	/* keyboard parameters */
	transpose = 0;
	key_ofs = 0;
	veloc0 = 32;
	veloc1 = 64;
	veloc2 = 96;
	veloc3 = 127;
	mouse_notnum = -1;
	mouse_veloc = 127;
	cur_size = 1;
	ctrl_on = 0;
	shift_on = 0;
	alt_on = 0;

//	set_default_size(img_width, img_height);
	set_title("thinksynth - Keyboard");

	img_height = key_sizes[cur_size][0] + key_sizes[cur_size][1];
	img_width = key_sizes[cur_size][2] * 75;

	drawArea.set_size_request(img_width, img_height);
	drawArea.add_events(Gdk::ALL_EVENTS_MASK);

	add(vbox);
	vbox.pack_start(ctrlFrame, Gtk::PACK_SHRINK, 5);
	vbox.pack_start(drawArea);
	ctrlFrame.add(ctrlTable);

	chanVal = new Gtk::Adjustment(0, 0, synth->GetChannelCount());
	chanBtn = new Gtk::SpinButton(*chanVal);

	ctrlTable.attach(chanLbl, 0, 1, 0, 1, Gtk::SHRINK, Gtk::SHRINK, 5, 0);
	ctrlTable.attach(*chanBtn, 1, 2, 0, 1, Gtk::SHRINK, Gtk::SHRINK);

	realize();
	gtk_widget_realize(GTK_WIDGET(drawArea.gobj()));	

	drawable = ((GtkWidget *)drawArea.gobj())->window;
	GTK_WIDGET_SET_FLAGS((GtkWidget *)drawArea.gobj(), GTK_CAN_FOCUS);

	kbgc = gdk_gc_new (drawable);
	gdk_gc_set_function (kbgc, GDK_COPY);
	gdk_gc_set_fill (kbgc, GDK_SOLID);
	gdk_rgb_gc_set_foreground (kbgc, 0x00000000);
	gdk_rgb_gc_set_background (kbgc, 0x00FFFFFF);

	/* disable key repeat */
//	gdk_key_repeat_disable ();

	/* clear previous key state */
	for (int i = 0; i < 128; i++)
	{
		prv_active_keys[i] = -2;
		active_keys[i] = 0;
	}

	drawArea.signal_expose_event().connect(
		SigC::slot(*this, &KeyboardWindow::exposeEvent));

	drawArea.signal_button_press_event().connect(
		SigC::slot(*this, &KeyboardWindow::clickEvent));

	drawArea.signal_key_press_event().connect(
		SigC::slot(*this, &KeyboardWindow::keyEvent));

	drawKeyboard (5);
}

KeyboardWindow::~KeyboardWindow (void)
{
	hide ();
}

/* convert key value to note number		*/
/* return value is -1 if key value is not valid	*/

int	KeyboardWindow::keyval_to_notnum (int key)
{
	char	*c;
	int	m, n, o;
	
	if ((key <= 0) || (key >= 256)) return -1;

	/* upper case -> lower case */
	if ((key >= 'A') && (key <= 'Z')) {
		key -= 'A'; key += 'a';
	}
	/* convert SHIFT characters */
	c = strchr (key_conv_in, key);
	if (c != NULL) key = key_conv_out[c - key_conv_in];
	/* find character in tables */
	c = strchr (keylist_1, key);
	if (c == NULL) {
		c = strchr (keylist_2, key);
		if (c == NULL) return -1;	/* not found */
		n = (int) (c - keylist_2);
	} else {
		n = 14 + (int) (c - keylist_1);
	}
	n += 13;
	/* key offset */
	n += (key_ofs << 1);
	m = n % 14;
	o = n / 14;	/* octave */
	/* check for invalid keys (E# and B#) */
	if ((m == 5) || (m == 13)) return -1;
	/* correct for missing black keys and transpose */
	if (m > 4) m--;
	n = 48 + transpose + 12 * o + m;
	if ((n < 0) || (n > 127)) n = -1;

	return n;
}


#define set_note_off(channel,notenum) { float *pbuf = new float[1]; *pbuf = 0; synth->SetNoteArg(channel, notenum, "trigger", pbuf, 1); }

bool KeyboardWindow::keyEvent (GdkEventKey *k)
{
	int keynum, press, release, notenum, veloc;

	keynum = k->keyval;
	press = (k->type == GDK_KEY_PRESS ? 1 : 0);
	release = 1 - press;

	switch (keynum)
	{
		case GDK_Control_L:
		case GDK_Control_R:
			ctrl_on = press; break;
		case GDK_Shift_L:		/* Shift */
		case GDK_Shift_R:
			shift_on = press; break;
		case GDK_Alt_L:			/* Alt */
		case GDK_Alt_R:
		case GDK_Meta_L:
		case GDK_Meta_R:
			alt_on = press; break;
		case GDK_F1:			/* function keys */
		case GDK_F2:
		case GDK_F3:
		case GDK_F4:
		case GDK_F5:
		case GDK_F6:
		case GDK_F7:
		case GDK_F8:
		case GDK_F9:
		case GDK_F10:
		case GDK_F11:
		case GDK_F12:
		case GDK_Left:
		case GDK_Right:
		case GDK_Up:
		case GDK_Down:
			break;
		default:
		{
			/* other keys */
			notenum = keyval_to_notnum (keynum);
			if (notenum >= 0) {	/* note event */
				if (press) {	/* note-on */
					if (alt_on) {	/* velocity */
						veloc = veloc0;
					} else if (ctrl_on) {
						veloc = veloc1;
					} else if (shift_on) {
						veloc = veloc2;
					} else {
						veloc = veloc3;
					}
					synth->AddNote((int)chanVal->get_value(), notenum, veloc);
					active_keys[notenum] = 1;
				} else {	/* note-off */
					set_note_off((int)chanVal->get_value(), notenum);
					active_keys[notenum] = 1;
				}
			}
			break;
		}

	}

	drawKeyboard(1);
	
	return true;
}

bool KeyboardWindow::clickEvent (GdkEventButton *b)
{	
	int	veloc;

	if(b->type == GDK_BUTTON_PRESS) {
		if (mouse_notnum >= 0) {	/* already active */
			set_note_off((int)chanVal->get_value(), mouse_notnum);
			active_keys[mouse_notnum] = 0;
		}
		
		/* get note number */
		mouse_notnum = get_coord ();
		
		if (mouse_notnum < 0) return false;
		
		switch (b->button) 
		{
			case 1:	veloc = veloc3; break;
			case 2:	veloc = veloc2; break;
			case 3:	veloc = veloc1; break;
			default:
				veloc = veloc0;
				break;
		}

		active_keys[mouse_notnum] = 1;
		synth->AddNote((int)chanVal->get_value(), mouse_notnum, veloc);
		
		mouse_veloc = veloc;	/* save velocity */
	}

	else if (b->type == GDK_BUTTON_RELEASE)
	{
		/* turn off if active */
		if (mouse_notnum >= 0) {
			set_note_off((int)chanVal->get_value(), mouse_notnum);
		}
		mouse_notnum = -1;
	}

	drawKeyboard(1);

	return true;
}

bool KeyboardWindow::exposeEvent (GdkEventExpose *e)
{
	drawKeyboard (5);

	return true;
}

void KeyboardWindow::drawKeyboard (int mode)
{	
	int		i, j, k, l, z, s0, s1, s2, s3, s4, s5, s6;
	unsigned int	c;

	s0 = key_sizes[cur_size][0];	/* black key height		*/
	s1 = key_sizes[cur_size][1];	/* total height - b. key height	*/
	s2 = key_sizes[cur_size][2];	/* white key total width	*/
	s3 = key_sizes[cur_size][3];	/* black key width		*/
	s4 = key_sizes[cur_size][4];	/* white key width 1 (C, F)	*/
	s5 = key_sizes[cur_size][5];	/* white key width 2 (D, G, A)	*/
	s6 = key_sizes[cur_size][6];	/* white key width 3 (E, B)	*/

	/* z = 4 if entire window should be redrawn */
	z = mode & 4;
	if (z) {
		/* key borders */
		gdk_rgb_gc_set_foreground (kbgc, color0);
		gdk_draw_line (drawable, kbgc, 0, img_height - 1,
					    img_width - 1, img_height - 1);
		i = 128; l = -1;
		do {
			l += s2;
			gdk_draw_line (drawable, kbgc, l, 0, l, img_height - 2);
		} while (--i);
	}
	j = s4;				/* black key x pos */
	l = 0;				/* white key x pos */
	i = k = 0;			/* key number (i: 0-127, k: 0-11) */
	/* draw keys */
	do {
		/* update only if state changed or redraw window was reqd */
		if ((active_keys[i] != prv_active_keys[i]) || z) {
			/* save new state */
			prv_active_keys[i] = active_keys[i];
			if (((k >= 5) && (k & 1)) || ((k < 5) && !(k & 1))) {
				/* white keys */
				if (i == 69) {	/* A (440 Hz) */
					c = (unsigned int)
					    (active_keys[i] ? color6 : color3);
				} else {
					c = (unsigned int)
					    (active_keys[i] ? color4 : color1);
				}
				/* set color */
				gdk_rgb_gc_set_foreground (kbgc, c);
				if ((k == 0) || (k == 5)) {
					/* C, F */
					gdk_draw_rectangle (drawable, kbgc, 1,
							    l, 0, s4, s0);
				} else if ((k == 4) || (k == 11)
					   || (i == 127)) {
					/* E, B */
					gdk_draw_rectangle (drawable, kbgc, 1,
							    l + s2 - s6 - 1, 0,
							    s6, s0);
				} else {
					/* D, G, A */
					gdk_draw_rectangle (drawable, kbgc, 1,
							    l + s2 - s6 - 1, 0,
							    s5, s0);
				}
				gdk_draw_rectangle (drawable, kbgc, 1,
						    l, s0, s2 - 1, s1 - 1);
			} else {
				/* black keys */
				c = (unsigned int)
				    (active_keys[i] ? color5 : color2);
				/* set color */
				gdk_rgb_gc_set_foreground (kbgc, c);
				gdk_draw_rectangle (drawable, kbgc, 1,
						    j, 0, s3, s0);
			}
		}
		/* new x coordinate */
		if (((k >= 5) && (k & 1)) || ((k < 5) && !(k & 1))) {
			/* white keys */
			l += s2;
			if ((k == 4) || (k == 11)) {
				/* skip E# and B# */
				j += s2;
			}
		} else {
			/* black keys */
			j += s2;
		}
		k = (k == 11 ? 0 : k + 1);
	} while (++i < 128);


}

/* get mouse pointer coordinates, and convert to key number */
/* return -1 if pointer is not in keyboard area */
int	KeyboardWindow::get_coord (void)
{
	gint		x, y, m, n, o;
	GdkModifierType	mask;

	mask = GDK_MODIFIER_MASK;
	gdk_window_get_pointer (drawable, &x, &y, &mask);
	/* check for valid coordinates */
	if ((x < 0) || (x >= img_width) || (y < 0) || (y >= img_height))
		return -1;
	/* calculate key number */
	n = (x / key_sizes[cur_size][2]) << 1;
	m = n % 14;
	o = 12 * (n / 14) + m;
	if (y < key_sizes[cur_size][0]) {		/* black keys */
		y = x - ((n >> 1) * key_sizes[cur_size][2]);
		switch (m) {
		case 0:			/* C */
		case 6:			/* F */
			if (y >= key_sizes[cur_size][4]) o++;
			break;
		case 4:			/* E */
		case 12:		/* B */
			if (y < (key_sizes[cur_size][2]
				 - key_sizes[cur_size][6] - 1)) o--;
			break;
		default:		/* D, G, A */
			if (y >= key_sizes[cur_size][4]) {
				o++;
			} else if (y < (key_sizes[cur_size][2]
					- key_sizes[cur_size][6] - 1)) {
				o--;
			}
		}
	}
	if (m > 4) o--;		/* correct for missing E# */
	if (o > 127) o = 127;

	return (int) o;
}


#if 0

/* close window */

void	close_window (void)
{
	/* enable key repeat again */
//	gdk_key_repeat_restore ();
}



void	adjust_transpose (int n)
{
	transpose += n;
	if (transpose < -72) transpose = -72;
	if (transpose > 72) transpose = 72;
	fprintf (stderr, "transpose: %d\n", transpose);
}

/* add n to base key with range checking and print new value */

void	adjust_key_ofs (int n)
{
	key_ofs += n;
	if (key_ofs < 0) key_ofs = 0;
	if (key_ofs > 6) key_ofs = 6;
	fprintf (stderr, "base key: %c\n", base_keys[key_ofs]);
}

/* add n to channel with range checking and print new value */

void	adjust_channel (int n)
{
//	set_channel (channel + n);
//	fprintf (stderr, "channel: %d\n", channel);
}

/* add n to program with range checking and print new value */

void	adjust_program (int n)
{
//	send_program_change (midi_program + n);
//	fprintf (stderr, "program: %d\n", midi_program);
}

/* add n to velocity m with range checking and print new value	*/
/* m is 4 for off velocity					*/
int          off_vel = 64;
void	adjust_velocity (int m, int n)
{
	int	*veloc;

	switch (m) {
	case 0: veloc = &veloc0; break;
	case 1: veloc = &veloc1; break;
	case 2: veloc = &veloc2; break;
	case 3: veloc = &veloc3; break;
	default:
		veloc = &off_vel;
	}
	*veloc += n;
	if (*veloc < 1) *veloc = 1;
	if (*veloc > 127) *veloc = 127;
	if (m == 4) {
		fprintf (stderr, "note-off velocity: ");
	} else {
		fprintf (stderr, "velocity %d: ", m);
	}
	fprintf (stderr, "%d\n", *veloc);
}

/* function keys */

void	fkeys_func (int key)
{
	if (alt_on) {
		switch (key) {
			/* velocity 0 */
			case GDK_Down:	adjust_velocity (0, -8); return;
			case GDK_Left:	adjust_velocity (0, -1); return;
			case GDK_Right:	adjust_velocity (0,  1); return;
			case GDK_Up:	adjust_velocity (0,  8); return;
		}
	} else if (ctrl_on) {
		switch (key) {
			/* velocity 1 */
			case GDK_Down:	adjust_velocity (1, -8); return;
			case GDK_Left:	adjust_velocity (1, -1); return;
			case GDK_Right:	adjust_velocity (1,  1); return;
			case GDK_Up:	adjust_velocity (1,  8); return;
		}
	} else if (shift_on) {
		switch (key) {
			/* note-off velocity */
			case GDK_F1: adjust_velocity (4, -8); return;
			case GDK_F2: adjust_velocity (4, -1); return;
			case GDK_F3: adjust_velocity (4,  1); return;
			case GDK_F4: adjust_velocity (4,  8); return;
			/* channel number */
			case GDK_F9:  adjust_channel (-1); return;
			case GDK_F10: adjust_channel (-1); return;
			case GDK_F11: adjust_channel ( 1); return;
			case GDK_F12: adjust_channel ( 1); return;
			/* velocity 2 */
			case GDK_Down:	adjust_velocity (2, -8); return;
			case GDK_Left:	adjust_velocity (2, -1); return;
			case GDK_Right:	adjust_velocity (2,  1); return;
			case GDK_Up:	adjust_velocity (2,  8); return;
		}
	} else {
		switch (key) {
			/* transpose */
			case GDK_F1:  adjust_transpose (-12); return;
			case GDK_F2:  adjust_transpose ( -1); return;
			case GDK_F3:  adjust_transpose (  1); return;
			case GDK_F4:  adjust_transpose ( 12); return;
			/* program */
			case GDK_F9:  adjust_program (-8); return;
			case GDK_F10: adjust_program (-1); return;
			case GDK_F11: adjust_program ( 1); return;
			case GDK_F12: adjust_program ( 8); return;
			/* velocity 3 */
			case GDK_Down:	adjust_velocity (3, -8); return;
			case GDK_Left:	adjust_velocity (3, -1); return;
			case GDK_Right:	adjust_velocity (3,  1); return;
			case GDK_Up:	adjust_velocity (3,  8); return;
		}
	}
	switch (key) {
	case GDK_F5:				/* print current settings */
		fprintf (stderr, "==== current settings: ====\n");
//		fprintf (stderr, "MIDI channel: %d\n", channel);
//		fprintf (stderr, "MIDI program: %d\n", midi_program);
		fprintf (stderr, "transpose: %d\n", transpose);
		fprintf (stderr, "base key: %c\n", base_keys[key_ofs]);
		fprintf (stderr, "velocity 0: %d\n", veloc0);
		fprintf (stderr, "velocity 1: %d\n", veloc1);
		fprintf (stderr, "velocity 2: %d\n", veloc2);
		fprintf (stderr, "velocity 3: %d\n", veloc3);
		fprintf (stderr, "note-off velocity: %d\n", off_vel);
		break;
	case GDK_F6:				/* all notes off */
		fprintf (stderr, "All notes off\n");
//		all_notes_off ();
		break;
	case GDK_F7:				/* base key - 1 */
		adjust_key_ofs (-1);
		break;
	case GDK_F8:				/* base key + 1 */
		adjust_key_ofs (1);
	}
}

/* convert key value to note number		*/
/* return value is -1 if key value is not valid	*/

int	keyval_to_notnum (int key)
{
	char	*c;
	int	m, n, o;

	if ((key <= 0) || (key >= 256)) return -1;

	/* upper case -> lower case */
	if ((key >= 'A') && (key <= 'Z')) {
		key -= 'A'; key += 'a';
	}
	/* convert SHIFT characters */
	c = strchr (key_conv_in, key);
	if (c != NULL) key = key_conv_out[c - key_conv_in];
	/* find character in tables */
	c = strchr (keylist_1, key);
	if (c == NULL) {
		c = strchr (keylist_2, key);
		if (c == NULL) return -1;	/* not found */
		n = (int) (c - keylist_2);
	} else {
		n = 14 + (int) (c - keylist_1);
	}
	n += 13;
	/* key offset */
	n += (key_ofs << 1);
	m = n % 14;
	o = n / 14;	/* octave */
	/* check for invalid keys (E# and B#) */
	if ((m == 5) || (m == 13)) return -1;
	/* correct for missing black keys and transpose */
	if (m > 4) m--;
	n = 48 + transpose + 12 * o + m;
	if ((n < 0) || (n > 127)) n = -1;

	return n;
}

/* read and process keyboard and mouse events		*/
/* return status is the sum of the following values:	*/
/*	1: some keys should be redrawn			*/
/*	2: close window and exit			*/
/*	4: redraw entire window				*/

int	get_events (void)
{	int	rval;
	int	keynum, press, release, notenum, veloc;

	rval = 0;

	while ((event = gdk_event_get ()) != NULL) {
		if ((event->type == GDK_DELETE)		/* close window */
		    || (event->type == GDK_DESTROY)) {
			rval |= 2; continue;
		}
		if ((event->type == GDK_KEY_PRESS)	/* key press or    */
		    || (event->type == GDK_KEY_RELEASE)) {	/* release */
			keynum = event->key.keyval;
			/* key state */
			press = (event->type == GDK_KEY_PRESS ? 1 : 0);
			release = 1 - press;
			switch (keynum) {
			case GDK_Control_L:		/* Control */
			case GDK_Control_R:
				ctrl_on = press; break;
			case GDK_Shift_L:		/* Shift */
			case GDK_Shift_R:
				shift_on = press; break;
			case GDK_Alt_L:			/* Alt */
			case GDK_Alt_R:
			case GDK_Meta_L:
			case GDK_Meta_R:
				alt_on = press; break;
			case GDK_F1:			/* function keys */
			case GDK_F2:
			case GDK_F3:
			case GDK_F4:
			case GDK_F5:
			case GDK_F6:
			case GDK_F7:
			case GDK_F8:
			case GDK_F9:
			case GDK_F10:
			case GDK_F11:
			case GDK_F12:
			case GDK_Left:
			case GDK_Right:
			case GDK_Up:
			case GDK_Down:
				if (press) fkeys_func (keynum);
				rval |= 1;
				break;
			default:			/* other keys */
				notenum = keyval_to_notnum (keynum);
				if (notenum >= 0) {	/* note event */
					if (press) {	/* note-on */
						if (alt_on) {	/* velocity */
							veloc = veloc0;
						} else if (ctrl_on) {
							veloc = veloc1;
						} else if (shift_on) {
							veloc = veloc2;
						} else {
							veloc = veloc3;
						}
//						send_note_on (notenum, veloc);
						active_keys[notenum] = 1;
						rval |= 1;
					} else {	/* note-off */
//						send_note_off (notenum);
						active_keys[notenum] = 1;
						rval |= 1;
					}
				}
			}
		} else if ((event->type == GDK_BUTTON_PRESS)	/* mouse */
			   || (event->type == GDK_2BUTTON_PRESS)
			   || (event->type == GDK_3BUTTON_PRESS)) {
			if (mouse_notnum >= 0) {	/* already active */
//				send_note_off (mouse_notnum);
				active_keys[mouse_notnum] = 0;
				rval |= 1;
			}
			/* get note number */
			mouse_notnum = get_coord ();
			if (mouse_notnum < 0) continue;
			/* velocity */
			switch (event->button.button) {
			case 1:	veloc = veloc3; break;
			case 2:	veloc = veloc2; break;
			case 3:	veloc = veloc1; break;
			default:
				veloc = veloc0;
			}
			active_keys[mouse_notnum] = 1;
//			send_note_on (mouse_notnum, veloc);
			rval |= 1;
			mouse_veloc = veloc;	/* save velocity */
		} else if (event->type == GDK_BUTTON_RELEASE) {
			/* turn off if active */
			if (mouse_notnum >= 0) {
//				send_note_off (mouse_notnum);
				active_keys[mouse_notnum] = 0;
				rval |= 1;
			}
			mouse_notnum = -1;
		} else {				/* other events */
			/* check for active mouse note */
			if (mouse_notnum >= 0) {
				notenum = get_coord ();
				if (notenum != mouse_notnum) {
//					send_note_off (mouse_notnum);
					active_keys[mouse_notnum] = 0;
					/* turn on new note */
					if (notenum >= 0) {
//						send_note_on (notenum,
//							      mouse_veloc);
						active_keys[notenum] = 1;
					}
					rval |= 1;
					mouse_notnum = notenum;
				}
			}
			/* update window if necessary */
			if ((event->type == GDK_CONFIGURE)
			    || (event->type == GDK_EXPOSE)
			    || (event->type == GDK_MAP)) {
				rval |= 4;
			}
		}
	}

	return rval;
}

#endif
