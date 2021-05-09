#ifndef BoundedBuffer_h
#define BoundedBuffer_h

#include <stdio.h>
#include <queue>
#include <string>
#include <pthread.h>
#include <condition_variable>

using namespace std;

class BoundedBuffer
{
private:
	int cap; // max number of bytes in the buffer
	queue<vector<char>> q;	/* the queue of items in the buffer. Note
	that each item a sequence of characters that is best represented by a vector<char> for 2 reasons:
	1. An STL std::string cannot keep binary/non-printables
	2. The other alternative is keeping a char* for the sequence and an integer length (i.e., the items can be of variable length).
	While this would work, it is clearly more tedious */

	// add necessary synchronization variables and data structures 
	int occupancy;
	condition_variable data_avail;
	condition_variable slot_avail;
	mutex mtx;


public:
	BoundedBuffer(int _cap):cap(_cap),occupancy(0){

	}
	~BoundedBuffer(){

	}

	void push(char* data, int len){
		unique_lock<mutex> l (mtx);
		slot_avail.wait (l, [this]{return occupancy + sizeof(datamsg) < cap;});
		vector<char> d (data, data+len);
		q.push(d);
		occupancy += len;
		l.unlock();
		data_avail.notify_one ();
	}

	int pop(char* buf, int bufcap){
		unique_lock<mutex> l (mtx);
		data_avail.wait (l, [this]{return q.size() > 0;});
		vector<char> data = q.front();
		q.pop();
		occupancy -= data.size();
		l.unlock();
		assert(data.size() <= bufcap);
		memcpy(buf, data.data(), data.size());
		slot_avail.notify_one ();
		return data.size();
	}
};

#endif /* BoundedBuffer_ */
