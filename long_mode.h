#pragma once
void InitLongModeGdt(void);
void InitLongModeIdt(QWORD VectorAddr);
void InitLongModeTSS(void);
void InitLongModePages(void);
void InitMTRR(void);
void InitPAT(void);
void InitControlAndSegmenRegs(void);
void Delay(int ms_cnt);
void Speeker_Emit(void);
//void LPC_Disable(bool en);
