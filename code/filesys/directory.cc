// directory.cc 
//	Routines to manage a directory of file names.
//
//	The directory is a table of fixed length entries; each
//	entry represents a single file, and contains the file name,
//	and the location of the file header on disk.  The fixed size
//	of each directory entry means that we have the restriction
//	of a fixed maximum size for file names.
//
//	The constructor initializes an empty directory of a certain size;
//	we use ReadFrom/WriteBack to fetch the contents of the directory
//	from disk, and to write back any modifications back to disk.
//
//	Also, this implementation has the restriction that the size
//	of the directory cannot expand.  In other words, once all the
//	entries in the directory are used, no more files can be created.
//	Fixing this is one of the parts to the assignment.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "utility.h"
#include "filehdr.h"
#include "directory.h"

//----------------------------------------------------------------------
// Directory::Directory
// 	Initialize a directory; initially, the directory is completely
//	empty.  If the disk is being formatted, an empty directory
//	is all we need, but otherwise, we need to call FetchFrom in order
//	to initialize it from disk.
//
//	"size" is the number of entries in the directory
//----------------------------------------------------------------------

Directory::Directory(int size)
{
    table = new DirectoryEntry[size];
    names = new char*[size];
    tableSize = size;
    for (int i = 0; i < tableSize; i++) {
        table[i].inUse = FALSE;
        names[i] = NULL;
    }
}

//----------------------------------------------------------------------
// Directory::~Directory
// 	De-allocate directory data structure.
//----------------------------------------------------------------------

Directory::~Directory()
{ 
    for (int i = 0; i < tableSize; i++) if (names[i] != NULL) {
        delete [] names[i];
        names[i] = NULL;
    }
    delete [] table;
    table = NULL;
} 

//----------------------------------------------------------------------
// Directory::FetchFrom
// 	Read the contents of the directory from disk.
//
//	"file" -- file containing the directory contents
//----------------------------------------------------------------------

void
Directory::FetchFrom(OpenFile *file)
{
    for (int i = 0; i < tableSize; i++) if (table[i].inUse) {
        table[i].inUse = FALSE;
        ASSERT(names[i] != NULL);
        delete [] names[i];
        names[i] = NULL;
    }

    int fileSize = file->GetHdr()->FileLength();
    char *buf = new char[fileSize];

    (void) file->ReadAt(buf, fileSize, 0);

    for (int i = 0, curEntry = 0; i < fileSize; i += table[curEntry].totalSize, curEntry++) {
        memcpy(&table[curEntry], &buf[i], sizeof(DirectoryEntry));
        if (names[curEntry] != NULL) {
            delete [] names[curEntry];
            names[curEntry] = NULL;
        }
        names[curEntry] = new char[table[curEntry].nameSize + 1];
        memcpy(names[curEntry], buf + i + sizeof(DirectoryEntry), table[curEntry].nameSize);
        names[curEntry][table[curEntry].nameSize] = '\0';
    }
    delete [] buf;
}

//----------------------------------------------------------------------
// Directory::WriteBack
// 	Write any modifications to the directory back to disk
//
//	"file" -- file to contain the new directory contents
//----------------------------------------------------------------------

void
Directory::WriteBack(OpenFile *file)
{
    int fileSize = 0;
    for (int i = 0; i < tableSize; i++) if (table[i].inUse) {
        fileSize += sizeof(table[i]);
        fileSize += table[i].nameSize;
    }
    char *buf = new char[fileSize], *ptr = buf;
    for (int i = 0; i < tableSize; i++) if (table[i].inUse) {
        memcpy(ptr, (char*)&table[i], sizeof(table[i]));
        ptr += sizeof(table[i]);
        memcpy(ptr, names[i], table[i].nameSize);
        ptr += table[i].nameSize;
    }
    file->GetHdr()->clear();
    (void) file->WriteAt(buf, fileSize, 0);
}

//----------------------------------------------------------------------
// Directory::FindIndex
// 	Look up file name in directory, and return its location in the table of
//	directory entries.  Return -1 if the name isn't in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::FindIndex(char *name)
{
    for (int i = 0; i < tableSize; i++) {
        if (table[i].inUse && !strcmp(names[i], name) ) {
            return i;
        }
    }
    return -1;		// name not in directory
}

//----------------------------------------------------------------------
// Directory::Find
// 	Look up file name in directory, and return the disk sector number
//	where the file's header is stored. Return -1 if the name isn't 
//	in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::Find(char *name)
{
    int i = FindIndex(name);

    if (i != -1)
	return table[i].sector;
    return -1;
}

//----------------------------------------------------------------------
// Directory::Add
// 	Add a file into the directory.  Return TRUE if successful;
//	return FALSE if the file name is already in the directory, or if
//	the directory is completely full, and has no more space for
//	additional file names.
//
//	"name" -- the name of the file being added
//	"newSector" -- the disk sector containing the added file's header
//----------------------------------------------------------------------

bool
Directory::Add(char *name, int newSector)
{ 
    if (FindIndex(name) != -1)
	return FALSE;

    for (int i = 0; i < tableSize; i++)
        if (!table[i].inUse) {
            // allocate space for file name
            int len = strlen(name);
            int nameSize = (len + 3) / 4 * 4;
            names[i] = new char[nameSize + 1];
            for (int j = 0; j < len; j++) {
                names[i][j] = name[j];
            }
            for (int j = len; j <= nameSize; j++) names[i][j] = '\0';
            table[i].totalSize = sizeof(DirectoryEntry) + nameSize;
            table[i].inUse = TRUE;
            table[i].sector = newSector;
            table[i].nameSize = nameSize;
        return TRUE;
	}
    return FALSE;	// no space.  Fix when we have extensible files.
}

//----------------------------------------------------------------------
// Directory::Remove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory. 
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------

bool
Directory::Remove(char *name)
{ 
    int i = FindIndex(name);

    if (i == -1)
	return FALSE; 		// name not in directory
    table[i].inUse = FALSE;
    delete [] names[i];
    names[i] = NULL;
    return TRUE;	
}

//----------------------------------------------------------------------
// Directory::List
// 	List all the file names in the directory. 
//----------------------------------------------------------------------

void
Directory::List()
{
   for (int i = 0; i < tableSize; i++)
	if (table[i].inUse) 
	    printf("%s\n", names[i]);
}

//----------------------------------------------------------------------
// Directory::Print
// 	List all the file names in the directory, their FileHeader locations,
//	and the contents of each file.  For debugging.
//----------------------------------------------------------------------

void
Directory::Print()
{ 
    FileHeader *hdr = new FileHeader;

    printf("Directory contents:\n");
    for (int i = 0; i < tableSize; i++)
	if (table[i].inUse) {
	    printf("Name: %s, Sector: %d\n", names[i], table[i].sector);
	    hdr->FetchFrom(table[i].sector);
	    hdr->Print();
	}
    printf("\n");
    delete hdr;
}
