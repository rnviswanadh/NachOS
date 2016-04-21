syscall_GetReg
1. Take the input from register 4 to variable reg
2. Return the value stored returning the value stored in reg

syscall_GetPA
1. Read the virtual address from register 4 
2. Divide the virtual address by pageSize to get virtual Page Number
**** Write Here ****

syscall_GetPID
1. Return the pid of the current thread using function getpid() defined in thread.h

Data structures:
-> Defined a variable processid (in thread.h), which stores the pid of the thread created recently (i.e) the processid is incremented in constructor function of Thread Class
-> Defined variable pid (in thread.h) which stores the pid of the thread
Functions:
-> getpid() (in thread.h) returns the pid of the current process

syscall_GetPPID
1. Return the pid of the parent of the current thread using function getppid() defined in thread.h

Data Structures:
-> Defined variable ppid (in thread.h) which stores the ppid of the thread
-> The variable is set while calling fork syscall