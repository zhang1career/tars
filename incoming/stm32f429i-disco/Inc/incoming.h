#ifndef INCOMING_H
#define INCOMING_H

#include "incoming_report.h"
#include "main.h"

void Incoming_ClockConfig(incoming_report_t *r);
void Incoming_RunTests(incoming_report_t *r);

void Incoming_SetPass(incoming_report_t *r, enum incoming_check bit);
void Incoming_SetFail(incoming_report_t *r, enum incoming_check bit);
void Incoming_SetWarn(incoming_report_t *r, enum incoming_check bit);
void Incoming_SetSkip(incoming_report_t *r, enum incoming_check bit);

#endif /* INCOMING_H */
