/*
 *
 */

#include "putty.h"

void exec_browser(char *url)
{
    ShellExecute(NULL, NULL, url, NULL, NULL, SW_SHOWNORMAL);
}
