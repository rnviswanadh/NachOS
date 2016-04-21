// exception.cc 
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.  
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"
#include "console.h"
#include "synch.h"

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2. 
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions 
//	are in machine.h.
//----------------------------------------------------------------------
static Semaphore *readAvail;
static Semaphore *writeDone;
static void ReadAvail(int arg) { readAvail->V(); }
static void WriteDone(int arg) { writeDone->V(); }

static void ConvertIntToHex (unsigned v, Console *console)
{
   unsigned x;
   if (v == 0) return;
   ConvertIntToHex (v/16, console);
   x = v % 16;
   if (x < 10) {
      writeDone->P() ;
      console->PutChar('0'+x);
   }
   else {
      writeDone->P() ;
      console->PutChar('a'+x-10);
   }
}

void childfunction(int dummy){                // copied from down of __SWICH() in scheduler::Run
  #ifdef USER_PROGRAM
    if (currentThread->space != NULL) {         // if there is an address space
        currentThread->RestoreUserState();      // to restore, do it.
  currentThread->space->RestoreState();
    }
#endif
    machine->Run();  
}

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);
    int memval, vaddr, printval, tempval, exp;
    unsigned printvalus;        // Used for printing in hex
    if (!initializedConsoleSemaphores) {
       readAvail = new Semaphore("read avail", 0);
       writeDone = new Semaphore("write done", 1);
       initializedConsoleSemaphores = true;
    }
    Console *console = new Console(NULL, NULL, ReadAvail, WriteDone, 0);;

    if ((which == SyscallException) && (type == syscall_Halt)) {
	DEBUG('a', "Shutdown, initiated by user program.\n");
   	interrupt->Halt();
    }
    else if ((which == SyscallException) && (type == syscall_PrintInt)) {
       printval = machine->ReadRegister(4);
       if (printval == 0) {
	  writeDone->P() ;
          console->PutChar('0');
       }
       else {
          if (printval < 0) {
	     writeDone->P() ;
             console->PutChar('-');
             printval = -printval;
          }
          tempval = printval;
          exp=1;
          while (tempval != 0) {
             tempval = tempval/10;
             exp = exp*10;
          }
          exp = exp/10;
          while (exp > 0) {
	     writeDone->P() ;
             console->PutChar('0'+(printval/exp));
             printval = printval % exp;
             exp = exp/10;
          }
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_PrintChar)) {
	writeDone->P() ;
        console->PutChar(machine->ReadRegister(4));   // echo it!
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_PrintString)) {
       vaddr = machine->ReadRegister(4);
       machine->ReadMem(vaddr, 1, &memval);
       while ((*(char*)&memval) != '\0') {
	  writeDone->P() ;
          console->PutChar(*(char*)&memval);
          vaddr++;
          machine->ReadMem(vaddr, 1, &memval);
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_PrintIntHex)) {
       printvalus = (unsigned)machine->ReadRegister(4);
       writeDone->P() ;
       console->PutChar('0');
       writeDone->P() ;
       console->PutChar('x');
       if (printvalus == 0) {
          writeDone->P() ;
          console->PutChar('0');
       }
       else {
          ConvertIntToHex (printvalus, console);
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } else if ((which == SyscallException) && (type == syscall_GetReg)) {
       int reg = machine->ReadRegister(4);                                // taking input from register 2
       machine->WriteRegister(2, machine->ReadRegister(reg));             // returning the value stored in reg
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } else if ((which == SyscallException) && (type == syscall_GetPA)) {
       int virtAddr = machine->ReadRegister(4);                           // reading virtual address from register 2
       unsigned int vpn = (unsigned) virtAddr/PageSize;                   // virtual page number = (vitual address)/pagesize
       int physicalAddress;
        
       if(vpn > machine->pageTableSize){                                  // three conditions as mentioned in text file
          machine->WriteRegister(2, -1);
       } else if (!machine->pageTable[vpn].valid) {
          machine->WriteRegister(2, -1);
       } else if (machine->pageTable[vpn].physicalPage > NumPhysPages) {
          machine->WriteRegister(2, -1);
       } else {
          machine->Translate(virtAddr, &physicalAddress, 4, FALSE);
       }
       
       machine->WriteRegister(2, physicalAddress);                        // returning physical address

       // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } else if ((which == SyscallException) && (type == syscall_GetPID)) {
      machine->WriteRegister(2, currentThread->getpid());               // getpid is a function defined in threads.h
       
       // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } else if ((which == SyscallException) && (type == syscall_GetPPID)) {
      machine->WriteRegister(2, currentThread->getppid());              // getppid is a function defined in threads.h
       
       // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } else if ((which == SyscallException) && (type == syscall_Time)) {
      machine->WriteRegister(2, stats->totalTicks);                     // totals ticks of the object stats of class Statistics 
      // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } else if ((which == SyscallException) && (type == syscall_Yield)) {
      currentThread->YieldCPU();                                        // putting current thread into ready queue

      // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } else if ((which == SyscallException) && (type == syscall_NumInstr)) {
      machine->WriteRegister(2, currentThread->numInstr);                      
      // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } 
    else if ((which == SyscallException) && (type == syscall_Exec)) {
      int vaddrexec = machine->ReadRegister(4);                         // read file name virtual address
      
      char filename[50];                                                // similar code of print string but here we are storing the file name 
      int memorycharint;                                                // instead of printing it
      int j = 0;
      // int memval;
      machine->ReadMem(vaddrexec, 1, &memorycharint);             
      while ((*(char*)&memorycharint) != '\0') {
        memorycharint = (char)memorycharint;
        filename[j] = memorycharint;
        j++;
        vaddrexec++;
        machine->ReadMem(vaddrexec, 1, &memorycharint);
      }
      filename[j] = '\0';

      OpenFile*execExec = fileSystem->Open(filename);                   // opening the file with name stored in filename[50]
      
      AddrSpace *new_space;
      new_space = new AddrSpace(execExec);                              // created a new address space sent in the constructor AddrSpace(executable)
      currentThread->space = new_space;                                 
      delete execExec;                                                  // delete pointer of executable    
      new_space->InitRegisters(); 
      new_space->RestoreState();
      machine->Run();     // jump to the user progam
      ASSERT(FALSE);      // never returns
      
    }
    else if ((which == SyscallException) && (type == syscall_Sleep)) {
      
      
      int sleep_time = machine->ReadRegister(4);                         // reading sleep time
        if(sleep_time==0){
            currentThread->YieldCPU();                                   // yielding current thread when yielding
        } else{
            sleepList->SortedInsert((void *)currentThread, sleep_time+stats->totalTicks);   // inserting the current thread to sorted sleep list 
                                                                                            // to be waken after some time
            IntStatus initialLevel = interrupt->SetLevel(IntOff);
            currentThread->PutThreadToSleep();                                              // putting the current thread to sleep
            (void) interrupt->SetLevel(initialLevel);
        }
      
      // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
     }  

    else if ((which == SyscallException) && (type == syscall_Fork)) {
      // Advance program counters at the start, else the machine is halting after return of this systemcall                                        
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));                     
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);

      NachOSThread *child_thread;
      child_thread = new NachOSThread("child");         // creating new thread object named child 
      
      AddrSpace * space = new AddrSpace();              // creating new AddrSpace object to allocate space to the child
      child_thread->space = space;
      
      machine->WriteRegister(2,0);  

      child_thread->SaveUserState();                    // saving all machine registers to the child

      // child_thread->RestoreUserState();
      child_thread->userRegisters[2] = 0;               // making return value 2 by setting userRegisters[2] = 0
      // child_thread->setUserRegister2Return();
      child_thread->parentThread = currentThread;       // pointer to the parent thread in the child thread
      // printf("%d %d\n", child_thread->getpid(), currentThread->getpid());
      currentThread->insertChild(child_thread->getpid()); // this inserts the child thread to childList of parent thread

      child_thread->ThreadFork(childfunction, 0);         // calling threadFork function with childFunction as arguement
      machine->WriteRegister(2,child_thread->getpid());   // returning pid of child thread to the parent thread
      // printf("11111\n");

    }
     else if ((which == SyscallException) && (type == syscall_Join)) {
      
      
      
      int joined_pid = machine->ReadRegister(4);                  // the pid of child for which the parent has to wait
      // printf("11111 %d 11111\n", currentThread->getpid());
      int childStatus = currentThread->searchChild(joined_pid);   // searchig the child list of parent for the joined_pid
      // printf("%d ramakf\n", childStatus);
      // childStatus = -1;               
      if(childStatus>=0){
        machine->WriteRegister(2, childStatus);                   // child already exited
      }                                                                            // -1, -2, some number greater than 0
      else if(childStatus == -2){
        machine->WriteRegister(2, -1);                            // the thread with pid as joined_pid is not child of current thread
      }
      else if(childStatus == -1){                                 // child didnot exit, so the coming process
        currentThread->joinpid = joined_pid;
        // printf("in  joiun\n");
        IntStatus initialLevel = interrupt->SetLevel(IntOff);
        currentThread->PutThreadToSleep();
        (void) interrupt->SetLevel(initialLevel);
      }

      // printf("111111111\n");
      // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_Exit)) {
      int exit_code = machine->ReadRegister(4);
      // printf("exiting %s\n", currentThread->getName());
      threads_online--;
      if(threads_online==0){
        interrupt->Halt();
      }
      else{
        currentThread->updateparent(exit_code);
        currentThread->FinishThread();
      }
      // FinishThread and halt doesnot return. So, sense in increasing program counters
    }
     else {
	printf("Unexpected user mode exception %d %d\n", which, type);
	ASSERT(FALSE);
    }
}
