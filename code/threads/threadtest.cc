// threadtest.cc 
//	Simple test case for the threads assignment.
//
//	Create two threads, and have them context switch
//	back and forth between themselves by calling Thread::Yield, 
//	to illustratethe inner workings of the thread system.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "synch.h"

// testnum is set in main.cc
int testnum = 1;

// For test condition variable
Lock *mutex;
Condition *condc, *condp;
int buffer;

void ProducerCond(int id) {
	for (int i = 1; i <= 10; i++) {
		mutex->Acquire();
		while (buffer == 1) condp->Wait(mutex);
		buffer = 1;
		printf("[%d] Produce an item\n", i);
		condc->Signal(mutex);
		mutex->Release();
	}
}

void ConsumerCond(int id) {
	for (int i = 1; i <= 10; i++) {
		mutex->Acquire();
		while (buffer == 0) condc->Wait(mutex);
		buffer = 0;
		printf("[%d] Consume an item\n", i);
		condp->Signal(mutex);
		mutex->Release();
	}
}

// For test semaphore
Semaphore *slots, *items;

void ProducerSema(int id) {
	for (int i = 1; i <= 25; i++) {
		slots->P();
		mutex->Acquire();
		printf("[%d] Producer an item\n", i);
		mutex->Release();
		items->V();
	}
}

void ConsumerSema(int id) {
	for (int i = 1; i <= 25; i++) {
		items->P();
		mutex->Acquire();
		printf("[%d] Consume an item\n", i);
		mutex->Release();
		slots->V();
	}
}

//----------------------------------------------------------------------
// ThreadTest1
// 	Set up a ping-pong between two threads, by forking a thread 
//	to call SimpleThread, and then calling SimpleThread ourselves.
//----------------------------------------------------------------------

void
ThreadTest1()
{
    DEBUG('t', "Entering ThreadTest1");

    Thread *t1 = new Thread("forked thread");
    Thread *t2 = new Thread("forked thread");

	mutex = new Lock("mutex");
	condc = new Condition("condc");
	condp = new Condition("condp");

	slots = new Semaphore("slots", 5);
	items = new Semaphore("items", 0);

    t1->Fork(ProducerSema, t1->getThreadId());
	t2->Fork(ConsumerSema, t2->getThreadId());
}

//----------------------------------------------------------------------
// ThreadTest
// 	Invoke a test routine.
//----------------------------------------------------------------------

void
ThreadTest()
{
    switch (testnum) {
    case 1:
	ThreadTest1();
	break;
    default:
	printf("No test specified.\n");
	break;
    }
	printf("Test command ts: \n");
	Ts();
}

