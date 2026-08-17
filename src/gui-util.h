#ifndef GUI_UTIL_H
#define GUI_UTIL_H

inline void trim_leadspc (char *line)
{
    /* delete the leading white spaces from the string 'line' */
    char *line_strt;
    char *ptr1, *ptr2;

    /* find the first non-space and point to it with 'line_strt' */
    for (line_strt = line; *line_strt != '\0'; line_strt++)
    if (*line_strt != ' ' && *line_strt != '\t')
        break;

    /* copy the string beginning at 'line_strt' into 'line' */
    for (ptr1 = line, ptr2 = line_strt; *ptr2 != '\0'; ptr1++, ptr2++)
    *ptr1 = *ptr2;
    *ptr1 = '\0';
}

/* One color per MIDI channel, hue-spaced by the golden angle so any two
 * channels are told apart at a glance. The piano roll predicted this
 * would move somewhere shared "if channel colors grow legs" -- the
 * composer canvas painting its sink boxes to match the roll's notes is
 * exactly those legs.
 *
 * The HSV->RGB conversion is spelled out because gtkmm-4 has nowhere to
 * borrow it from: Gtk::HSV went with GTK3 and Gdk::RGBA only parses
 * names. Saturation and value are fixed; this is the h-sector dance and
 * nothing more. */
inline void gthChannelColor (int chan, double &r, double &g, double &b)
{
    double h = (chan * 137.508) / 360.0;      /* golden-angle spacing    */

    h -= (int)h;
    if (h < 0)
        h += 1;

    const double s = 0.65, v = 0.85;

    double f = h * 6.0;
    int    sector = (int)f % 6;

    f -= (int)f;

    double p = v * (1 - s);
    double q = v * (1 - s * f);
    double t = v * (1 - s * (1 - f));

    switch (sector)
    {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

#endif /* GUI_UTIL_H */
