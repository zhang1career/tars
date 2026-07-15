#ifndef SHELL_HIST_H
#define SHELL_HIST_H

#include <stdint.h>

void ShellHist_Init(void);
void ShellHist_Push(const char *line);
void ShellHist_ResetBrowse(void);
int ShellHist_Prev(const char **line);
int ShellHist_Next(const char **line);
uint32_t ShellHist_Count(void);
const char *ShellHist_Entry(uint32_t age);

#endif /* SHELL_HIST_H */
