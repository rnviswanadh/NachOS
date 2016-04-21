#include "syscall.h"
#define SIZE 100
int
main()
{
    int array[SIZE], i, sum=0;

    for (i=0; i<SIZE; i++) array[i] = i;
    for (i=0; i<SIZE; i++) sum += array[i];
    system_PrintInt(sum);
    // system_PrintInt(sum);
    system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    // system_PrintString("Total sum: \n");
    system_PrintInt(sum);

    system_PrintChar('\n');
    system_PrintString("Executed instruction count: ");
    system_PrintInt(system_GetNumInstr());
    system_PrintChar('\n');
    system_Exit(0);
    return 0;
}
