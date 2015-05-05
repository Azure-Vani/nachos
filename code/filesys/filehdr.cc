// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "system.h"
#include "filehdr.h"

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(BitMap *freeMap, int fileSize)
{   
    // used direct map entries
    int usedDirectEntries = numSectors < DirectEntries ? numSectors : DirectEntries;
    // used indirect map entries
    int usedIndirectEntries = numSectors <= DirectEntries ? 0 : divRoundUp(numSectors - DirectEntries, EntriesPerSector);
    // used entries in the last mapping indirect sector
    int usedEntry = usedIndirectEntries == 0 ? 0 : numSectors - DirectEntries - (usedIndirectEntries - 1) * EntriesPerSector;

    if (fileSize + numBytes > MaxFileSize) {
        printf("Too large file\n");
        ASSERT(FALSE);
    }

    int rawSectors = divRoundUp(fileSize, SectorSize);
    int backup = rawSectors;
    int totalSectors = rawSectors;

    if (rawSectors > DirectEntries - usedDirectEntries) {
        rawSectors -= DirectEntries - usedDirectEntries;
        if (usedIndirectEntries == 0 || rawSectors > EntriesPerSector - usedEntry) {
            rawSectors -= EntriesPerSector - usedEntry;
            totalSectors += divRoundUp(rawSectors, EntriesPerSector);
        }
    }

    rawSectors = backup;

    if (totalSectors > freeMap->NumClear()) return FALSE;

    int *buf = new int[EntriesPerSector];

    if (usedIndirectEntries == 0) {
        for (int i = usedDirectEntries; rawSectors && i < DirectEntries; i++) {
            dataSectors[i] = freeMap->Find();
   //         printf("$%d\n", dataSectors[i]);
            rawSectors--;
        }
    } else { 
        synchDisk->ReadSector(dataSectors[DirectEntries + usedIndirectEntries - 1], (char*)buf);
        for (int i = usedEntry; rawSectors && i < EntriesPerSector; i++) {
            buf[i] = freeMap->Find();
  //          printf("$%d\n", buf[i]);
            rawSectors--;
        }
        synchDisk->WriteSector(dataSectors[DirectEntries + usedIndirectEntries - 1], (char*)buf);
    }
    if (rawSectors > 0) {
        for (int i = DirectEntries + usedIndirectEntries; rawSectors && i < NumEntries; i++) {
            dataSectors[i] = freeMap->Find();
//            printf("#%d\n", dataSectors[i]);
            for (int j = 0; rawSectors && j < EntriesPerSector; j++) {
                buf[j] = freeMap->Find();
 //               printf("$%d\n", buf[j]);
                rawSectors--;
            }
            synchDisk->WriteSector(dataSectors[i], (char*)buf);
        }
    }

    rawSectors = backup;

    numSectors += rawSectors;

    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void 
FileHeader::Deallocate(BitMap *freeMap)
{
    int firstLevel= 0;
    if (numSectors <= DirectEntries) 
        firstLevel = numSectors;
    else
        firstLevel = DirectEntries + divRoundUp((numSectors - DirectEntries), EntriesPerSector);
    for (int i = 0; i < firstLevel; i++) {
        //printf(">>= %d\n", (int) dataSectors[i]);
        ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
        freeMap->Clear((int) dataSectors[i]);
    }
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector)
{
    synchDisk->ReadSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector)
{
    synchDisk->WriteSector(sector, (char *)this); 
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::ByteToSector(int offset)
{
    return(dataSectors[offset / SectorSize]);
}

// index is 0-base
int FileHeader::GetIthSector(int index, int *cache) {
    if (index < DirectEntries) {
        return dataSectors[index];
    } else {
        int *buf = new int[EntriesPerSector];
        int which = DirectEntries + divRoundUp((index - DirectEntries + 1), EntriesPerSector) - 1;
        synchDisk->ReadSector(dataSectors[which], (char*) buf);
        int ret = buf[index - DirectEntries + 1 - (which - DirectEntries) * EntriesPerSector - 1];
    //    for (int i = 0; i < NumEntries; i++) printf("%d,", dataSectors[i]); puts("");
     //   printf("%d %d %d\n", buf[0], buf[1], buf[2]);
        delete buf;
      //  printf("index %d %d (which %d)\n", index, ret, which);
        return ret;
    }
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void
FileHeader::Print()
{
    int i, j, k;
    char *data = new char[SectorSize];

    printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
    for (i = 0; i < numSectors; i++)
	printf("%d ", dataSectors[i]);
    printf("\nFile contents:\n");
    for (i = k = 0; i < numSectors; i++) {
	synchDisk->ReadSector(dataSectors[i], data);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
	    if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
		printf("%c", data[j]);
            else
		printf("\\%x", (unsigned char)data[j]);
	}
        printf("\n"); 
    }
    delete [] data;
}
