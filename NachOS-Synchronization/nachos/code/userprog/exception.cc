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
#include "synchop.h"

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

extern void StartProcess (char*);

void
ForkStartFunction (int dummy)
{
   currentThread->Startup();
   machine->Run();
}

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


unsigned createSpace(unsigned size){

  int numPages = divRoundUp(size, PageSize);
  stats->numPageFaults = stats->numPageFaults + numPages;
  TranslationEntry * newPageTable;
  int numforVirtual = currentThread->space->GetNumPages();
  TranslationEntry *pageTable1 = currentThread->space->GetPageTable();

  newPageTable = new TranslationEntry[numPages+numforVirtual];
  for (int i = 0; i < numforVirtual; i++) {
    newPageTable[i].virtualPage = pageTable1[i].virtualPage;
    newPageTable[i].physicalPage = pageTable1[i].physicalPage;
    newPageTable[i].valid = pageTable1[i].valid;
    newPageTable[i].use = pageTable1[i].use;
    newPageTable[i].dirty = pageTable1[i].dirty;
    newPageTable[i].readOnly = pageTable1[i].readOnly;
    newPageTable[i].shared = pageTable1[i].shared;
  }

  for(int i = 0; i < numPages; i++){
    newPageTable[numforVirtual+i].virtualPage = i + numforVirtual;
    newPageTable[numforVirtual+i].physicalPage = i + numPagesAllocated;
    newPageTable[numforVirtual+i].valid = TRUE;
    newPageTable[numforVirtual+i].use = FALSE;
    newPageTable[numforVirtual+i].dirty = FALSE;
    newPageTable[numforVirtual+i].readOnly = FALSE;
    newPageTable[numforVirtual+i].shared = TRUE;
  }
  delete pageTable1;
  pageTable1 = NULL;

  currentThread->space->SetPageTable(newPageTable, numforVirtual + numPages);
  numPagesAllocated += numPages;
  currentThread->space->RestoreState();
  return numforVirtual*PageSize;
}

unsigned returnID(unsigned key){
  bool found = FALSE;
  int i;
  for(i=0; i<MAX_ALLOWABLE_SEMOPHORES; i++){
    if(semophoreTable[i].valid){
      if(semophoreTable[i].key == key){
        found = TRUE;
        return i;
      }
    }
  }
  for(i=0; i<MAX_ALLOWABLE_SEMOPHORES; i++){
    if(!semophoreTable[i].valid){
      found = TRUE;
      semophoreTable[i].valid = TRUE;
      semophoreTable[i].key = key;
      semophoreTable[i].semaphore = new Semaphore("user created", 1);
      return i;
    }
  }
  if(!found){
    return -1;
  }
}

unsigned returnCondID(unsigned key){
  bool found = FALSE;
  int i;
  for(i=0; i<MAX_ALLOWABLE_CONDITIONS; i++){
    if(conditionTable[i].valid){
      if(conditionTable[i].key == key){
        found = TRUE;
        return i;
      }
    }
  }
  for(i=0; i<MAX_ALLOWABLE_CONDITIONS; i++){
    if(!conditionTable[i].valid){
      found = TRUE;
      conditionTable[i].valid = TRUE;
      conditionTable[i].key = key;
      conditionTable[i].condition = new Condition("user created");
      return i;
    }
  }
  if(!found){
    return -1;
  }
}


void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);
    int memval, vaddr, printval, tempval, exp;
    unsigned printvalus;	// Used for printing in hex
    if (!initializedConsoleSemaphores) {
       readAvail = new Semaphore("read avail", 0);
       writeDone = new Semaphore("write done", 1);
       initializedConsoleSemaphores = true;
    }
    Console *console = new Console(NULL, NULL, ReadAvail, WriteDone, 0);
    int exitcode;		// Used in syscall_Exit
    unsigned i;
    char buffer[1024];		// Used in syscall_Exec
    int waitpid;		// Used in syscall_Join
    int whichChild;		// Used in syscall_Join
    NachOSThread *child;		// Used by syscall_Fork
    unsigned sleeptime;		// Used by syscall_Sleep

    if ((which == SyscallException) && (type == syscall_Halt)) {
	DEBUG('a', "Shutdown, initiated by user program.\n");
   	interrupt->Halt();
    }
    else if ((which == SyscallException) && (type == syscall_Exit)) {
       exitcode = machine->ReadRegister(4);
       printf("[pid %d]: Exit called. Code: %d\n", currentThread->GetPID(), exitcode);
       // We do not wait for the children to finish.
       // The children will continue to run.
       // We will worry about this when and if we implement signals.
       exitThreadArray[currentThread->GetPID()] = true;

       // Find out if all threads have called exit
       for (i=0; i<thread_index; i++) {
          if (!exitThreadArray[i]) break;
       }
       currentThread->Exit(i==thread_index, exitcode);
    }
    else if ((which == SyscallException) && (type == syscall_Exec)) {
       // Copy the executable name into kernel space
       vaddr = machine->ReadRegister(4);
       while(!machine->ReadMem(vaddr, 1, &memval));
       i = 0;
       while ((*(char*)&memval) != '\0') {
          buffer[i] = (*(char*)&memval);
          i++;
          vaddr++;
          while(!machine->ReadMem(vaddr, 1, &memval));
       }
       buffer[i] = (*(char*)&memval);
       StartProcess(buffer);
    }
    else if ((which == SyscallException) && (type == syscall_Join)) {
       waitpid = machine->ReadRegister(4);
       // Check if this is my child. If not, return -1.
       whichChild = currentThread->CheckIfChild (waitpid);
       if (whichChild == -1) {
          printf("[pid %d] Cannot join with non-existent child [pid %d].\n", currentThread->GetPID(), waitpid);
          machine->WriteRegister(2, -1);
          // Advance program counters.
          machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
          machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
          machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
       }
       else {
          exitcode = currentThread->JoinWithChild (whichChild);
          machine->WriteRegister(2, exitcode);
          // Advance program counters.
          machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
          machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
          machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
       }
    }
    else if ((which == SyscallException) && (type == syscall_Fork)) {
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
       
       child = new NachOSThread("Forked thread", GET_NICE_FROM_PARENT);
       child->space = new AddrSpace (currentThread->space);  // Duplicates the address space
       child->SaveUserState ();		     		      // Duplicate the register set
       child->ResetReturnValue ();			     // Sets the return register to zero
       child->ThreadStackAllocate (ForkStartFunction, 0);	// Make it ready for a later context switch
       child->Schedule ();
       machine->WriteRegister(2, child->GetPID());		// Return value for parent
    }
    else if ((which == SyscallException) && (type == syscall_Yield)) {
       currentThread->YieldCPU();
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
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
        writeDone->P() ;        // wait for previous write to finish
        console->PutChar(machine->ReadRegister(4));   // echo it!
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_PrintString)) {
       vaddr = machine->ReadRegister(4);
       while(!machine->ReadMem(vaddr, 1, &memval)){
        ;
       }


       while ((*(char*)&memval) != '\0') {
          writeDone->P() ;
          console->PutChar(*(char*)&memval);
          vaddr++;
          while(!machine->ReadMem(vaddr, 1, &memval));
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_GetReg)) {
       machine->WriteRegister(2, machine->ReadRegister(machine->ReadRegister(4))); // Return value
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_GetPA)) {
       vaddr = machine->ReadRegister(4);
       machine->WriteRegister(2, machine->GetPA(vaddr));  // Return value
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_GetPID)) {
       machine->WriteRegister(2, currentThread->GetPID());
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_GetPPID)) {
       machine->WriteRegister(2, currentThread->GetPPID());
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_Sleep)) {
       sleeptime = machine->ReadRegister(4);
       if (sleeptime == 0) {
          // emulate a yield
          currentThread->YieldCPU();
       }
       else {
          currentThread->SortedInsertInWaitQueue (sleeptime+stats->totalTicks);
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == syscall_Time)) {
       machine->WriteRegister(2, stats->totalTicks);
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
    }
    else if ((which == SyscallException) && (type == syscall_NumInstr)) {
       machine->WriteRegister(2, currentThread->GetInstructionCount());
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }

    // ############################ syscall_ShmAllocate ###########################

    else if ((which == SyscallException) && (type == syscall_ShmAllocate)) {
       
      unsigned a = (unsigned)machine->ReadRegister(4);
      machine->WriteRegister(2, createSpace(a));

      // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }

    // ############################ syscall_SemGet #########################
     else if ((which == SyscallException) && (type == syscall_SemGet)) {
      // printf("syscall_SemGet\n");
       unsigned key = (unsigned)machine->ReadRegister(4);
       machine->WriteRegister(2, returnID(key));
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }

    // ############################ syscall_SemOp #########################
    else if ((which == SyscallException) && (type == syscall_SemOp)) {
      // printf("syscall_SemOp\n");
      unsigned id = (unsigned)machine->ReadRegister(4);
      unsigned op = (unsigned)machine->ReadRegister(5);
      if(id >= MAX_ALLOWABLE_SEMOPHORES){
        machine->WriteRegister(2, -1);
      }else if(semophoreTable[id].valid){
        machine->WriteRegister(2, 0);
        Semaphore * semaphore = semophoreTable[id].semaphore;
        if(op==1){
          semaphore->V();
        }else if(op==-1){
          semaphore->P();
        }else{
          machine->WriteRegister(2, -1);
        }
      }else{
         machine->WriteRegister(2, -1);
      }
       
       // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } 




    // ############################ syscall_SemCTl #########################
    else if ((which == SyscallException) && (type == syscall_SemCtl)) {
        // printf("syscall_SemCtl\n");
        unsigned id = (unsigned)machine->ReadRegister(4);
        int command = machine->ReadRegister(5);
        int addr = (int)machine->ReadRegister(6);
        

       // time to check whether the system call completes nicely or not
       if(id >= MAX_ALLOWABLE_SEMOPHORES && !semophoreTable[id].valid){
        machine->WriteRegister(2, -1);
       }else { 
        Semaphore * semaphore = semophoreTable[id].semaphore;
        machine->WriteRegister(2, 0); 
        if(command == 0 && semophoreTable[id].valid){
          
          semophoreTable[id].valid = FALSE;
          delete semophoreTable[id].semaphore;
          semophoreTable[id].semaphore = NULL;

        } else if(command == 1 && semophoreTable[id].valid){
          
          while(!machine->WriteMem(addr, 4, semaphore->returnValue()));
          
        } else if(command == 2 && semophoreTable[id].valid){
          
          int value;
          while(!machine->ReadMem(addr, 4, &value));
          semaphore->setValue(value);
        
        }
       };
       // checking over

       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } 

    // ############################ syscall_CondGet #########################
    else if ((which == SyscallException) && (type == syscall_CondGet)) {
      // printf("syscall_CondGet\n");
       unsigned key = (unsigned)machine->ReadRegister(4);
       machine->WriteRegister(2, returnCondID(key));
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }

    // ############################ syscall_CondOp #########################
    else if ((which == SyscallException) && (type == syscall_CondOp)) {
      unsigned id = (unsigned)machine->ReadRegister(4);
      unsigned op = (unsigned)machine->ReadRegister(5);
      unsigned mutex = (unsigned)machine->ReadRegister(6);
      if(id >= MAX_ALLOWABLE_CONDITIONS){
        machine->WriteRegister(2, -1);
      }else{
        machine->WriteRegister(2, 0);
        Condition * condition = conditionTable[id].condition;
        
        if(op==COND_OP_WAIT){
          if(mutex>=MAX_ALLOWABLE_SEMOPHORES && !semophoreTable[mutex].valid){
            machine->WriteRegister(2, -1);
          }else{
            condition->Wait(semophoreTable[mutex].semaphore);  
          }
        }

        else if(op==COND_OP_SIGNAL){
          condition->Signal();
        }

        else if(op==COND_OP_BROADCAST){
          condition->Broadcast();
        }

        else{
          machine->WriteRegister(2, -1);
        }
      }
       
       // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }


    // ############################ syscall_CondRemove #########################
    else if ((which == SyscallException) && (type == syscall_CondRemove)) {
      unsigned id = (unsigned)machine->ReadRegister(4);
      
      if(id>=MAX_ALLOWABLE_CONDITIONS && conditionTable[id].valid==FALSE){
        machine->WriteRegister(2, -1);
      }else{
        conditionTable[id].valid = FALSE;
        delete conditionTable[id].condition;
        conditionTable[id].condition = NULL;
      }

      // Advance program counters.
      machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
      machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
      machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } 

    // ############################ PageFaultException ############################

     else if (which == PageFaultException) {
        // increment the number of page fault
        stats->numPageFaults++;

        // printf("pageFaults\n");
        int pid =currentThread->GetPID();
        currentThread->SortedInsertInWaitQueue (1000+stats->totalTicks);

        int vaddr = machine->ReadRegister(BadVAddrReg);
        NachOSThread * thread = threadArray[pid];
        char *filename = thread->space->filename;
        OpenFile *exec = fileSystem->Open(filename);
        int vpn = vaddr/PageSize;
        int offset = vaddr%PageSize;

        bzero(&machine->mainMemory[numPagesAllocated*PageSize], PageSize);
        exec->ReadAt(&(machine->mainMemory[numPagesAllocated * PageSize]), PageSize, vpn*PageSize+40); // code starts at 40, so offset
        TranslationEntry *pTable = thread->space->GetPageTable();
        pTable[vpn].virtualPage = vpn;
        pTable[vpn].physicalPage = numPagesAllocated;
        pTable[vpn].valid = TRUE;
        
        numPagesAllocated++;
        delete exec;
        // Advance program counters.
        // machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        // machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        // machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    } else {
	printf("Unexpected user mode exception %d %d\n", which, type);
	ASSERT(FALSE);
    }
}
