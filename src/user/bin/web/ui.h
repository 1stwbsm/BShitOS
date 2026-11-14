#ifndef WEB_UI_H
#define WEB_UI_H

#include "web.h"

/* Text columns, the same in both modes so one layout serves them. */
#define WEB_COLUMNS 100

extern struct web_page page;
extern int top; /* first visible line; shared so switching modes keeps your place */

void web_navigate(const char *url, int remember);
void web_back(void);
void web_go_home(void);

enum graphical_result { BACK_TO_TEXT, QUIT_BROWSER };
/* Runs the mouse-driven interface until the user leaves it. */
enum graphical_result web_graphical(void);

#endif
