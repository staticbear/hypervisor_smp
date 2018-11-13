#pragma once

void WakeupAP(BYTE ap_n, BYTE vector);
void IntInit4TLBShutdown(void);
bool IsBsp(void);
BYTE GetCpuN(void);
