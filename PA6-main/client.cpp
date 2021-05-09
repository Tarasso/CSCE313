#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "TCPreqchannel.h"

#include <thread>
#include <sys/wait.h>
#include <sys/epoll.h>

using namespace std;


void patient_thread_function(int p, int n, BoundedBuffer* requestBuffer){
    datamsg d (p, 0.00, 1);
    for(int i = 0; i<n; i++)
    {
        requestBuffer->push((char*) &d, sizeof(datamsg));
        d.seconds += 0.004;
    }
}

void file_thread_function(string fname, BoundedBuffer* requestBuffer, TCPRequestChannel* chan, int mb) {
    // create file and make it as long as the original length
    string recvfname = "recv/" + fname;
    char buf [1024];
    filemsg f (0,0);
    memcpy(buf, &f, sizeof(f));
    strcpy(buf + sizeof(f), fname.c_str());
    chan->cwrite(buf, sizeof(f) + fname.size() + 1);
    __int64_t filelength;
    chan->cread(&filelength, sizeof(filelength));

    FILE* fp = fopen(recvfname.c_str(), "w");
    fseek(fp, filelength, SEEK_SET);
    fclose(fp);

    // generate all file messages
    filemsg* fm = (filemsg*) buf;
    __int64_t remlen = filelength;

    while(remlen > 0)
    {
        // cout << "remlen: " << remlen << endl;
        fm->length = min(remlen, (__int64_t) mb);
        requestBuffer->push(buf, sizeof(filemsg) + fname.size() + 1);
        fm->offset += fm->length;
        remlen -= fm->length;
    }
    assert(remlen==0);
}

struct Response
{
    int person;
    double ecg;
};

void event_polling_thread (int n, int p, int w, int mb, TCPRequestChannel** wchans, BoundedBuffer* requestBuffer, HistogramCollection* hc) {
    char buf [1024];
    char recvbuf [mb];

    struct epoll_event ev;
    struct epoll_event events[w];

    // create an empty epoll list
    int epollfd = epoll_create1(0);
    if(epollfd == -1) {
        EXITONERROR("epoll_create1");
    }


    unordered_map<int, int> fd_to_index;
    vector<vector<char>> state (w);
    bool quit_recv = false;

    // priming + adding each rfd to the list
    int nsent = 0, nrecv = 0;
    for(int i = 0; i < w; i++)
    {
        if(!quit_recv)
        {
            int sz = requestBuffer->pop(buf, 1024);
            if(*(MESSAGE_TYPE*)buf == QUIT_MSG) {
                quit_recv = true;
            }
            wchans[i]->cwrite(buf, sz);
            state[i] = vector<char>(buf, buf+sz); // record the state [i]
            nsent++;
        }
        int rfd = wchans[i]->getfd();
        fcntl(rfd, F_SETFL, O_NONBLOCK);

        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = rfd;
        fd_to_index[rfd] = i;
        if(epoll_ctl(epollfd, EPOLL_CTL_ADD, rfd, &ev) == -1) {
            EXITONERROR("epoll_ctl: listen_sock");
        }

    }

    // nsent = w, nrecv = 0s

    while(true){
        if(quit_recv && nrecv == nsent)
            break;

        int nfds = epoll_wait(epollfd, events, w, -1);
        if(nfds == -1) {
            EXITONERROR("epoll_wait");
        }

        // loop for each available channel
        for(int i = 0; i < nfds; i++) {
            int rfd = events[i].data.fd;
            int index = fd_to_index[rfd];

            // process (recvbuf)
            int resp_sz = wchans[index]->cread(recvbuf, mb);
            nrecv++;
            vector<char> req = state[index];
            char* request = req.data();

            // processing the response
            MESSAGE_TYPE* m = (MESSAGE_TYPE*) request;
            if(*m == DATA_MSG) {
                hc->update(((datamsg*)request)->person, *(double*)recvbuf);
            }else if(*m == FILE_MSG) {
                filemsg* fm = (filemsg*) request;
                string fname = (char*)(fm + 1);
                string recvfname = "recv/" + fname;

                FILE* fp = fopen(recvfname.c_str(), "r+");
                fseek(fp, fm->offset, SEEK_SET);
                fwrite(recvbuf, 1, fm->length, fp);
                fclose(fp);
            }

            // reuse
            if(!quit_recv) {
                int req_sz = requestBuffer->pop(buf, sizeof(buf));
                if(*(MESSAGE_TYPE*)buf == QUIT_MSG) {
                    quit_recv = true;
                }
                wchans[index]->cwrite(buf, req_sz);
                state[index] = vector<char> (buf, buf+req_sz);
                nsent++;
            }
        }
    }
}


int main(int argc, char *argv[])
{
    int opt;
    int n = 15000;    //default number of requests per "patient"
    int p = 1;     // number of patients [1,15]
    int w = 200;    //default number of worker threads
    int b = 500; 	// default capacity of the request buffer, you should change this default
	int m = MAX_MESSAGE; 	// default capacity of the message buffer
    string host, port;

    srand(time_t(NULL));

    string fname = "";
    
    while((opt = getopt(argc, argv, "n:p:w:b:m:f:h:r:")) != -1)
    {
        switch (opt)
        {
            case 'n':
                n = atoi(optarg);
                break;
            case 'p':
                p = atoi(optarg);
                break;
            case 'w':
                w = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 'm':
                m = atoi(optarg);
                break;
            case 'f':
                fname = optarg;
                break;
            case 'h':
                host = optarg;
                break;
            case 'r':
                port = optarg;
                break;
            
        }
    }
    
    BoundedBuffer requestBuffer(b);
    BoundedBuffer responseBuffer(b);
	HistogramCollection hc;
    thread patients [p];
    TCPRequestChannel** wchans = new TCPRequestChannel* [w];
    thread filethread;
    thread evp;

    // create w worker channels
    for(int i = 0; i < w; i++)
    {
        wchans [i] = new TCPRequestChannel(host, port);
    }

    if(fname == "") // only set up hist for data messages
    {
        // create histogram for each patient 10 bins, range [-2,2] and add to collection
        for(int i =0; i < p; i++)
        {
            Histogram* hist = new Histogram(10,-2.0,2.0);
            hc.add(hist);
        }
    }
    
    struct timeval start, end;
    gettimeofday (&start, 0);

    /* Start all threads here */
    TCPRequestChannel* trans_file_chan;

    if(fname == "") // data message
    {
        for(int i = 0; i < p; i++)
            patients[i] = thread(patient_thread_function, i+1, n, &requestBuffer);
    }
    else // file transfer
    {
        trans_file_chan = new TCPRequestChannel(host, port);
        filethread = thread(file_thread_function, fname, &requestBuffer, trans_file_chan, m);
        cout << "created all file transfer threads" << endl;
    }

    // single worker thread
    evp = thread(event_polling_thread, n, p, w, m, wchans, &requestBuffer, &hc);
    
    
	/* Join all threads here */

    if(fname == "") // data message
    {
        for(int i = 0; i<p; i++)
            patients[i].join();
        cout << "patient threads finished" << endl;
    }
    else
    {
        filethread.join();
        cout << "file thread finished" << endl;
        delete trans_file_chan;
    }

    MESSAGE_TYPE q = QUIT_MSG;
    requestBuffer.push((char*) &q, sizeof(MESSAGE_TYPE));
    evp.join();
    cout << "worker thread finished" << endl;
    
    gettimeofday (&end, 0);

    // print the results
    hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

    for (int i = 0; i < w; ++i) {
        delete wchans[i];
    }
    delete[] wchans;

    cout << "All Done!!!" << endl;
}
