// translate.cc 
//	Routines to translate virtual addresses to physical addresses.
//	Software sets up a table of legal translations.  We look up
//	in the table on every memory reference to find the true physical
//	memory location.
//
// Two types of translation are supported here.
//
//	Linear page table -- the virtual page # is used as an index
//	into the table, to find the physical page #.
//
//	Translation lookaside buffer -- associative lookup in the table
//	to find an entry with the same virtual page #.  If found,
//	this entry is used for the translation.
//	If not, it traps to software with an exception. 
//
//	In practice, the TLB is much smaller than the amount of physical
//	memory (16 entries is common on a machine that has 1000's of
//	pages).  Thus, there must also be a backup translation scheme
//	(such as page tables), but the hardware doesn't need to know
//	anything at all about that.
//
//	Note that the contents of the TLB are specific to an address space.
//	If the address space changes, so does the contents of the TLB!
//
// DO NOT CHANGE -- part of the machine emulation
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "machine.h"
#include "addrspace.h"
#include "system.h"

#include <cstring>

// Routines for converting Words and Short Words to and from the
// simulated machine's format of little endian.  These end up
// being NOPs when the host machine is also little endian (DEC and Intel).

unsigned int
WordToHost(unsigned int word) {
#ifdef HOST_IS_BIG_ENDIAN
	 register unsigned long result;
	 result = (word >> 24) & 0x000000ff;
	 result |= (word >> 8) & 0x0000ff00;
	 result |= (word << 8) & 0x00ff0000;
	 result |= (word << 24) & 0xff000000;
	 return result;
#else 
	 return word;
#endif /* HOST_IS_BIG_ENDIAN */
}

unsigned short
ShortToHost(unsigned short shortword) {
#ifdef HOST_IS_BIG_ENDIAN
	 register unsigned short result;
	 result = (shortword << 8) & 0xff00;
	 result |= (shortword >> 8) & 0x00ff;
	 return result;
#else 
	 return shortword;
#endif /* HOST_IS_BIG_ENDIAN */
}

unsigned int
WordToMachine(unsigned int word) { return WordToHost(word); }

unsigned short
ShortToMachine(unsigned short shortword) { return ShortToHost(shortword); }


//----------------------------------------------------------------------
// Machine::ReadMem
//      Read "size" (1, 2, or 4) bytes of virtual memory at "addr" into 
//	the location pointed to by "value".
//
//   	Returns FALSE if the translation step from virtual to physical memory
//   	failed.
//
//	"addr" -- the virtual address to read from
//	"size" -- the number of bytes to read (1, 2, or 4)
//	"value" -- the place to write the result
//----------------------------------------------------------------------

bool
Machine::ReadMem(int addr, int size, int *value)
{
    int data;
    ExceptionType exception;
    int physicalAddress;
    
    DEBUG('a', "Reading VA 0x%x, size %d\n", addr, size);
    
    exception = Translate(addr, &physicalAddress, size, FALSE);
    if (exception != NoException) {
	machine->RaiseException(exception, addr);
	return FALSE;
    }
    switch (size) {
      case 1:
	data = machine->mainMemory[physicalAddress];
	*value = data;
	break;
	
      case 2:
	data = *(unsigned short *) &machine->mainMemory[physicalAddress];
	*value = ShortToHost(data);
	break;
	
      case 4:
	data = *(unsigned int *) &machine->mainMemory[physicalAddress];
	*value = WordToHost(data);
	break;

      default: ASSERT(FALSE);
    }
    
    DEBUG('a', "\tvalue read = %8.8x\n", *value);
    return (TRUE);
}

//----------------------------------------------------------------------
// Machine::WriteMem
//      Write "size" (1, 2, or 4) bytes of the contents of "value" into
//	virtual memory at location "addr".
//
//   	Returns FALSE if the translation step from virtual to physical memory
//   	failed.
//
//	"addr" -- the virtual address to write to
//	"size" -- the number of bytes to be written (1, 2, or 4)
//	"value" -- the data to be written
//----------------------------------------------------------------------

bool
Machine::WriteMem(int addr, int size, int value)
{
    ExceptionType exception;
    int physicalAddress;
     
    DEBUG('a', "Writing VA 0x%x, size %d, value 0x%x\n", addr, size, value);

    exception = Translate(addr, &physicalAddress, size, TRUE);
    if (exception != NoException) {
	machine->RaiseException(exception, addr);
	return FALSE;
    }
    switch (size) {
      case 1:
	machine->mainMemory[physicalAddress] = (unsigned char) (value & 0xff);
	break;

      case 2:
	*(unsigned short *) &machine->mainMemory[physicalAddress]
		= ShortToMachine((unsigned short) (value & 0xffff));
	break;
      
      case 4:
	*(unsigned int *) &machine->mainMemory[physicalAddress]
		= WordToMachine((unsigned int) value);
	break;
	
      default: ASSERT(FALSE);
    }
    
    return TRUE;
}

//----------------------------------------------------------------------
// Machine::Translate
// 	Translate a virtual address into a physical address, using 
//	either a page table or a TLB.  Check for alignment and all sorts 
//	of other errors, and if everything is ok, set the use/dirty bits in 
//	the translation table entry, and store the translated physical 
//	address in "physAddr".  If there was an error, returns the type
//	of the exception.
//
//	"virtAddr" -- the virtual address to translate
//	"physAddr" -- the place to store the physical address
//	"size" -- the amount of memory being read or written
// 	"writing" -- if TRUE, check the "read-only" bit in the TLB
//----------------------------------------------------------------------

TranslationEntry* Machine::LookupTlb(int vpn) {
    DEBUG('a', "look up tlb for virtual page %d\n", vpn);
    for (int i = 0; i < TLBSize; i++) {
        if (tlb[i].valid && tlb[i].virtualPage == vpn) {
            tlb[i].tlbLastUsed = machine->GetClock();
            return &tlb[i];
        }
    }
    return NULL;
}

int Machine::SelectTlbLru(void) {
    int w = 0;
    for (int i = 1; i < TLBSize; i++) {
        if (tlb[i].tlbLastUsed < tlb[w].tlbLastUsed)
            w = i;
    }
    return w;
}

int Machine::SelectTlbFifo(void) {
    int w = 0;
    for (int i = 1; i < TLBSize; i++) {
        if (tlb[i].tlbEnterTime < tlb[w].tlbEnterTime)
            w = i;
    }
    return w;
}

void Machine::EvictTlb(int index) {
    if (tlb[index].valid) {
        pageTable[tlb[index].virtualPage] = tlb[index];
    }
}

void Machine::SwapTlb(int index, TranslationEntry *entry) {
    tlb[index] = *entry;
    tlb[index].valid = true;
    tlb[index].tlbEnterTime = machine->GetClock(); 
    tlb[index].tlbLastUsed = machine->GetClock();
}

void Machine::UpdateTlb(int vpn, TranslationEntry* entry) {

    for (int i = 0; i < TLBSize; i++) {
        if (!tlb[i].valid) {
            EvictTlb(i);
            SwapTlb(i, entry);
            return;
        }
    }
    
    int which = SelectTlbFifo();
    EvictTlb(which);
    SwapTlb(which, entry);
}

ExceptionType Machine::LookupPageTable(int vpn, TranslationEntry *&entry) {
    if (vpn >= VirtualPagesPerThread) {
        return AddressErrorException;
    }
    if (!pageTable[vpn].valid) {
        return PageFaultException;
    }
    entry = &pageTable[vpn];
    return NoException;
}

ExceptionType
Machine::Translate(int virtAddr, int* physAddr, int size, bool writing)
{
    int i;
    unsigned int vpn, offset;
    TranslationEntry *entry;
    unsigned int pageFrame;

    DEBUG('a', "\tTranslate 0x%x, %d:(%d,%d), %s: ", virtAddr, currentThread->getThreadId(), diskOffset, memoryOffset, writing ? "write" : "read");

    // check for alignment errors
    if (((size == 4) && (virtAddr & 0x3)) || ((size == 2) && (virtAddr & 0x1))){
        DEBUG('a', "alignment problem at %d, size %d!\n", virtAddr, size);
        return AddressErrorException;
    }

    // we must have either a TLB or a page table, but not both!
    ASSERT(tlb != NULL || pageTable != NULL);	

    // calculate the virtual page number, and offset within the page,
    // from the virtual address
    vpn = (unsigned) virtAddr / PageSize;
    offset = (unsigned) virtAddr % PageSize;

    if (!(entry = LookupTlb(vpn))) { // in case of TLB missing
        ExceptionType e = LookupPageTable(vpn, entry);
        if (e == NoException) {
            UpdateTlb(vpn, entry);
        } else 
            return e;
    }

    if (entry->readOnly && writing) {	// trying to write to a read-only page
        DEBUG('a', "%d mapped read-only at %d in TLB!\n", virtAddr, i);
        return ReadOnlyException;
    }
    pageFrame = entry->physicalPage;
    bitmap[pageFrame].lastUsed = machine->GetClock();

    // if the pageFrame is too big, there is something really wrong! 
    // An invalid translation was loaded into the page table or TLB. 
    if (pageFrame >= NumPhysPages) { 
        DEBUG('a', "*** frame %d > %d!\n", pageFrame, NumPhysPages);
        return BusErrorException;
    }

    entry->use = TRUE;		// set the use, dirty bits
    if (writing)
        entry->dirty = TRUE;
    *physAddr = pageFrame * PageSize + offset;
    ASSERT((*physAddr >= 0) && ((*physAddr + size) <= MemorySize));
    DEBUG('a', "phys addr = 0x%x\n", *physAddr);
    return NoException;
}

#define MEM_OFFSET(i) ((i) + memoryOffset)
#define PAGE_OFFSET(i) ((i) + memoryOffset / PageSize)
#define DISK_OFFSET(i) ((i) + diskOffset)

void Machine::WriteBack(int pn) {
    int physAddr = pn * PageSize;

    int vpn = bitmap[pn].pt->virtualPage;
    int virAddr = vpn * PageSize;

    DEBUG('a', "\twrite back phys page %d\n", pn);
    if (virAddr <= noffH.code.size + noffH.initData.size + noffH.uninitData.size) {
        memcpy(mockDisk + DISK_OFFSET(virAddr), mainMemory + physAddr, PageSize);
    } else {
        int offset = VirtualMemoryPerThread - virAddr;
        DEBUG('a', "\t[stack] write back stack offset: %d\n", offset);
        memcpy(mockDisk + DISK_OFFSET(VirtualMemoryPerThread) - offset, mainMemory + physAddr, PageSize);
    }
}

void Machine::SwapIn(int pn, int vpn) {
    int virAddr = vpn * PageSize;
    int physAddr = pn * PageSize;

    if (virAddr <= noffH.code.size + noffH.initData.size + noffH.uninitData.size) {
        memcpy(mainMemory + physAddr, mockDisk + DISK_OFFSET(virAddr), PageSize);
    } else {
        int offset = VirtualMemoryPerThread - virAddr;
        DEBUG('a', "\t[stack] swap in stack offset: %d\n", offset);
        memcpy(mainMemory + physAddr, mockDisk + DISK_OFFSET(VirtualMemoryPerThread) - offset, PageSize);
    }
}

void Machine::PageSwapping(int addr) {
    int pn = GetEvictPage();
    int physAddr = pn * PageSize;

    int vpn = addr / PageSize;

    DEBUG('a', "\tswap out phys page: %d, swap in virtual page: %d\n", pn, vpn);

    if (bitmap[pn].pt) {
        TranslationEntry *pt = bitmap[pn].pt;
        DEBUG('a', "\tassociated virtual page %d\n", bitmap[pn].pt->virtualPage);
        for (int i = 0; i < TLBSize; i++) {
            if (tlb[i].valid && tlb[i].virtualPage == pt->virtualPage) {
                *pt = tlb[i];
                break;
            }
        }
        if (bitmap[pn].pt->dirty) 
            WriteBack(pn);
        bitmap[pn].pt->valid = false;
    }

    TranslationEntry *entry = &pageTable[vpn];
    bitmap[pn].pt = entry;
    entry->virtualPage = vpn;
    entry->physicalPage = pn;
    entry->valid = true;
    entry->readOnly = false;
    entry->dirty = false;
    
    SwapIn(pn, vpn);
}

int Machine::GetEvictPage(void) {
    int w = 0;
    for (int i = 1; i < PhysPagesPerThread; i++) {
        if (bitmap[PAGE_OFFSET(i)].lastUsed < bitmap[PAGE_OFFSET(w)].lastUsed) {
            w = i;
        }
    }
    return PAGE_OFFSET(w);
}

#undef MEM_OFFSET
#undef DISK_OFFSET
