#include <iostream>
#include <thread>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include "Semaphore.h"
using namespace std;

#define CAP 10 // in pounds

int monkey_count[2] = {0,0};
Semaphore* m[2];
Semaphore remCap (CAP); // in pounds
Semaphore dirmtx (1); // direction mutex

void WaitUntilSafe(int dir, int weight){
    m[dir]->P(); // locks crit section
    monkey_count[dir]++;
    if(monkey_count[dir] == 1)
        dirmtx.P(); // locks in for caravan of monkeys in one direction
    m[dir]->V(); // release crit section
    for(int i = 0; i < weight; i++)
        remCap.P(); // decreasing rope capacily for each lb the monkey weighs
}

void DoneWithCrossing(int dir, int weight){
    m[dir]->P(); // locks crit section
    monkey_count[dir]--;
    if(monkey_count[dir] == 0)
        dirmtx.V(); // unlocks so that other direction can begin
    m[dir]->V(); // release crit section
    for(int i = 0; i < weight; i++)
        remCap.V(); // increase rope capacily for each lb the monkey weighs
}


void CrossRavine(int monkeyid, int dir, int weight){
    cout << "Monkey [id=" << monkeyid << ", wt=" << weight <<", dir=" << dir <<"] is ON the rope" << endl;
    sleep (rand()%5);
    cout << "Monkey [id=" << monkeyid << ", wt=" << weight <<", dir=" << dir <<"] is OFF the rope" << endl;
}


void monkey(int monkeyid, int dir, int weight) {
    WaitUntilSafe(dir, weight);         // define
    CrossRavine(monkeyid, dir, weight); // given, no work needed
    DoneWithCrossing(dir, weight);      // define

}


int main() {
    m[0] = new Semaphore(1);
    m[1] = new Semaphore(1);


    int ids[] = {0,1,2,3,4,5,6,7,8,9};
    int dir[] = {0,0,0,0,1,1,1,0,0,0};
    int wei[] = {7,5,2,2,6,6,6,2,2,2};

    vector<thread> monekys;

    for(int i = 0; i < 10; i++)
        monekys.push_back(thread(monkey, ids[i], dir[i], wei[i]));

    for(int i = 0; i < 10; i++)
        monekys[i].join();
    
}