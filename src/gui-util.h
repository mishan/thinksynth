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

#endif /* GUI_UTIL_H */
