// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "noff.h"
#ifdef HOST_SPARC
#include <strings.h>
#endif

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void 
SwapHeader (NoffHeader *noffH)
{
	noffH->noffMagic = WordToHost(noffH->noffMagic);
	noffH->code.size = WordToHost(noffH->code.size);
	noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
	noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
	noffH->initData.size = WordToHost(noffH->initData.size);
	noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
	noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
	noffH->uninitData.size = WordToHost(noffH->uninitData.size);
	noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
	noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable)
{
    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) && 
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

    /*
    printf("(%d %d %d) (%d %d %d) (%d %d %d)\n", noffH.code.virtualAddr, noffH.code.inFileAddr, noffH.code.size, 
            noffH.initData.virtualAddr, noffH.initData.inFileAddr, noffH.initData.size, 
            noffH.uninitData.virtualAddr, noffH.uninitData.inFileAddr, noffH.uninitData.size);
            */

    int totalSize = noffH.code.size + noffH.initData.size + noffH.uninitData.size + UserStackSize;
    ASSERT(totalSize <= VirtualMemoryPerThread);

    if (machine->usedMemory >=  MemorySize) {
        printf("can not allocate main memory segment for new thread\n");
        ASSERT(FALSE);
    }
    usedMemory = machine->usedMemory;
    machine->usedMemory += PhysMemoryPerThread;

    if (machine->usedMockDisk >= AllocateMemory) {
        printf("run out of all allocated memory for nachos\n");
        ASSERT(FALSE);
    }
    usedStack = machine->usedMockDisk;
    machine->usedMockDisk += VirtualMemoryPerThread;

    pageTable = new TranslationEntry[PhysPagesPerThread];
    for (int i = 0; i < PhysPagesPerThread; i++) {
        pageTable[i].virtualPage = 0;
        pageTable[i].physicalPage = i;
        pageTable[i].valid = false;
        pageTable[i].use = FALSE;
        pageTable[i].dirty = FALSE;
        pageTable[i].readOnly = FALSE;
    }

    if (noffH.code.size > 0)
        executable->ReadAt(machine->mockDisk + usedStack + noffH.code.virtualAddr, noffH.code.size, noffH.code.inFileAddr);
    if (noffH.initData.size > 0)
        executable->ReadAt(machine->mockDisk + usedStack + noffH.initData.virtualAddr, noffH.initData.size, noffH.initData.inFileAddr);
    if (noffH.uninitData.size > 0)
        executable->ReadAt(machine->mockDisk + usedStack + noffH.uninitData.virtualAddr, noffH.uninitData.size, noffH.uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
   delete pageTable;
}

AddrSpace::AddrSpace (AddrSpace *oth) {
    *this = *oth;
    pageTable = new TranslationEntry[PhysPagesPerThread];
    memcpy(pageTable, oth->pageTable, sizeof (TranslationEntry[PhysPagesPerThread]));
}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);	

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, VirtualPagesPerThread * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState() {
    for (int i = 0; i < TLBSize; i++) {
        if (machine->tlb[i].valid) {
            *machine->getVaddrEntry(machine->tlb[i].virtualPage) = machine->tlb[i];
            machine->tlb[i].valid = false;
        }
    }
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() 
{
    machine->pageTable = pageTable;
    machine->noffH = noffH;
    machine->memoryOffset = usedMemory;
    machine->diskOffset = usedStack;
}
