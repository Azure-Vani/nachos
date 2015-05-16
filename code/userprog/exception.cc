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

#include <string>

#include "copyright.h"
#include "system.h"
#include "syscall.h"
#include "filesys.h"

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

char* getString(int addr) {
    std::string fileName;
    while (true) {
        int c = 0;
        while (!machine->ReadMem(addr, 1, &c));
        if (!c) break;
        fileName += (char) c;
        addr ++;
    }
    char *tmp = new char[fileName.length() + 1];
    strcpy(tmp, fileName.c_str());
    return tmp;
}

void
ExceptionHandler(ExceptionType which)
{
    // printf("%s\n", exceptionNames[which]);
    int type = machine->ReadRegister(2);
    int curInst = machine->ReadRegister(PCReg);

    if (which == SyscallException) {
        if (type == SC_Halt) {
            DEBUG('a', "Shutdown, initiated by user program.\n");
            interrupt->Halt();
        } else
        if (type == SC_Exit) {
            printf("Exit %d\n", machine->ReadRegister(4));
            currentThread->Finish();
        } else
        if (type == SC_Create) {
            int addr = machine->ReadRegister(4);
            char* name = getString(addr);
            printf("create %s\n", name);
            fileSystem->Create(name, 0, 0);
            //delete [] name;
        } else
        if (type == SC_Open) {
            int addr = machine->ReadRegister(4);
            char* name = getString(addr);
            printf("open %s\n", name);
            OpenFile *ofile = fileSystem->Open(name);
            // delete [] name;
            if (ofile == NULL) {
                machine->WriteRegister(2, -1);
                goto end;
            }
            int fd = -1;
            for (int i = 2; i < FdNumber; i++) if (currentThread->fds[i] == NULL) {
                fd = i;
                break;
            }
            if (fd == -1) {
                printf("Run out of fd table. \n");
                ASSERT(FALSE);
            }
            currentThread->fds[fd] = ofile;
            machine->WriteRegister(2, fd);
        } else 
        if (type == SC_Close) {
            int fd = machine->ReadRegister(4);
            printf("close %d\n", fd);
            ASSERT(currentThread->fds[fd] != NULL);
            // delete currentThread->fds[fd];
            currentThread->fds[fd] = NULL;
        } else
        if (type == SC_Write) {
            int addr = machine->ReadRegister(4);
            int size = machine->ReadRegister(5);
            int fd = machine->ReadRegister(6);
            printf("write addr %x, size %d, fd %d\n", addr, size, fd);
            char *buf = new char[size];
            for (int i = 0; i < size; i++) {
                machine->ReadMem(addr + i, 1, (int*)(buf + i));
            }
            currentThread->fds[fd]->Write(buf, size);
        } else 
        if (type == SC_Read) {
            int addr = machine->ReadRegister(4);
            int size = machine->ReadRegister(5);
            int fd = machine->ReadRegister(6);
            printf("read addr %x, size %d, fd %d\n", addr, size, fd);
            char *buf = new char[size];
            currentThread->fds[fd]->Read(buf, size);
            printf("read: ");
            for (int i = 0; i < size; i++) {
                printf("%x ", buf[i]);
                while (!machine->WriteMem(addr + i, 1, buf[i]));
            }
            puts("");
        } else {
            printf("Unkonwn system call %d\n", type);
            ASSERT(FALSE);
        }
    end:
        machine->WriteRegister(PCReg, curInst + 4);
    } else if (which == PageFaultException) {
        int addr = machine->ReadRegister(BadVAddrReg);
        machine->PageSwapping(addr);
    } else {
        printf("Unexpected user mode exception %d %d\n", which, type);
        ASSERT(FALSE);
    }

}
