#include <iostream>
#include <thread>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include "Semaphore.h"
using namespace std;

#define numPhilo 5 // people
enum status {THINK, HUNGRY, EAT};

Semaphore* s[numPhilo];
Semaphore me (1); // for mutex
status pflag[numPhilo];

void think(int i) {
    sleep(rand() % 10 + 1);
}

void eat() {
    sleep(rand() % 10 + 1);
}

void test(int i) {
    if(pflag[i] == HUNGRY &&
    pflag[(i+4) % numPhilo] != EAT &&
    pflag[(i+1) % numPhilo] != EAT)
    {
        pflag[i] = EAT;
        cout << "philosopher " << i << " now eating" << endl;
        s[i]->V();
    }
}

void take_chopsticks(int i) {
    me.P();
    pflag[i] = HUNGRY;
    test(i);
    me.V();
    s[i]->P();
}

void drop_chopsticks(int i) {
    me.P();
    cout << "philosopher " << i << " now done eating" << endl;
    pflag[i] = THINK;
    test((i+4) % numPhilo);
    test((i+1) % numPhilo);
    me.V();
}

void philosopher(int i) {
    while(true)
    {
        think(i);
        take_chopsticks(i);
        eat();
        drop_chopsticks(i);
    }
}

int main() {
    for(int i = 0; i < numPhilo; i++)
    {
        s[i] = new Semaphore(0);
        pflag[i] = THINK;
    }

    vector<thread> philos;

    for(int i = 0; i < numPhilo; i++)
        philos.push_back(thread(philosopher, i));

    for(int i = 0; i < numPhilo; i++)
        philos[i].join();
    
}