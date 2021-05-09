#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "FIFOreqchannel.h"

#include <thread>
#include <sys/wait.h>

using namespace std;


void patient_thread_function(int p, int n, BoundedBuffer* requestBuffer){
    datamsg d (p, 0.00, 1);
    for(int i = 0; i<n; i++)
    {
        requestBuffer->push((char*) &d, sizeof(datamsg));
        d.seconds += 0.004;
    }
}

void file_thread_function(string fname, BoundedBuffer* requestBuffer, FIFORequestChannel* chan, int mb) {
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

void worker_thread_function(BoundedBuffer* requestBuffer, FIFORequestChannel* wchan, BoundedBuffer* responseBuffer, int mb){
    char buf [1024];
    char recvbuf [mb];

    while(true)
    {
        requestBuffer->pop(buf, sizeof(buf));
        MESSAGE_TYPE* m = (MESSAGE_TYPE* ) buf;
        if(*m == QUIT_MSG)
        {
            wchan->cwrite(m, sizeof(MESSAGE_TYPE));
            delete wchan;
            break;
        }
        if(*m == DATA_MSG)
        {
            datamsg* d = (datamsg*) buf;
            wchan->cwrite(d, sizeof(datamsg));
            double ecg;
            wchan->cread(&ecg, sizeof(double));
            //cout << ecg << endl;
            Response r{d->person, ecg};
            responseBuffer->push((char*)&r, sizeof(r));
        }
        else if (*m == FILE_MSG)
        {
            // TODO: finish function
            filemsg* fm = (filemsg*) buf;
            string fname = (char*)(fm + 1);
            int sz = sizeof(filemsg) + fname.size() + 1;
            wchan->cwrite(buf, sz);
            wchan->cread(recvbuf, mb);

            string recvfname = "recv/" + fname;

            FILE* fp = fopen(recvfname.c_str(), "r+");
            fseek(fp, fm->offset, SEEK_SET);
            fwrite(recvbuf, 1, fm->length, fp);
            fclose(fp);
        }
    }
}

void histogram_thread_function (BoundedBuffer* responseBuffer, HistogramCollection* hc){
    char buf [1024];
    while(true)
    {
        responseBuffer->pop(buf,1024);
        Response* r = (Response*) buf; // this should be correct
        if(r->person == -1)
        {
            break;
        }
        hc->update(r->person, r->ecg);
    }
}


int main(int argc, char *argv[])
{
    int opt;
    int n = 100;    //default number of requests per "patient"
    int p = 10;     // number of patients [1,15]
    int w = 100;    //default number of worker threads
    int b = 1024; 	// default capacity of the request buffer, you should change this default
	int m = MAX_MESSAGE; 	// default capacity of the message buffer
    int h = 3;
    srand(time_t(NULL));

    string fname = "";
    
    while((opt = getopt(argc, argv, "n:p:w:b:m:f:h:")) != -1)
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
                h = atoi(optarg);
                break;
            
        }
    }

    int pid = fork();
    if (pid == 0){
        execl ("server", "server", "-m", (char*)to_string(m).c_str(), (char *)NULL);
    }
    
	FIFORequestChannel* chan = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE);
    BoundedBuffer requestBuffer(b);
    BoundedBuffer responseBuffer(b);
	HistogramCollection hc;

    thread patients [p];
    thread workers [w];
    thread hists [h];
    FIFORequestChannel* wchans [w];

    // create communication channels 1 for each worker
    for(int i = 0; i < w; i++)
    {
        MESSAGE_TYPE m = NEWCHANNEL_MSG;
        chan->cwrite(&m, sizeof(m));
        char newchanname [100];
        chan->cread(newchanname, sizeof(newchanname));
        wchans [i] = new FIFORequestChannel(newchanname, FIFORequestChannel::CLIENT_SIDE);
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

    if(fname == "") // data message
    {
        for(int i = 0; i < p; i++)
            patients[i] = thread(patient_thread_function, i+1, n, &requestBuffer);

        for(int i = 0; i < w; i++)
            workers[i] = thread(worker_thread_function, &requestBuffer, wchans[i], &responseBuffer, m);

        for(int i = 0; i < h; i++)
            hists[i] = thread(histogram_thread_function, &responseBuffer, &hc);

        cout << "created all data message threads" << endl;
    }
    else // file transfer
    {
        thread filethread(file_thread_function, fname, &requestBuffer, chan, m);

        for(int i = 0; i < w; i++)
            workers[i] = thread(worker_thread_function, &requestBuffer, wchans[i], &responseBuffer, m);

        cout << "created all file transfer threads" << endl;

        filethread.join();

        cout << "file thread finished" << endl;
    }
    
    
	/* Join all threads here */

    if(fname == "") // data message
    {
        for(int i = 0; i<p; i++)
            patients[i].join();
        
        cout << "patient threads finished" << endl;
    }

    for(int i = 0; i<w; i++)
    {
        MESSAGE_TYPE q = QUIT_MSG;
        requestBuffer.push((char*) &q, sizeof(MESSAGE_TYPE));
    }

    for(int i = 0; i<w; i++)
        workers[i].join();

    cout << "work threads finished" << endl;

    if(fname == "") // data message
    {
        // break condition for hist threads
        Response r {-1, 0}; 

        for(int i = 0; i<h; i++)
            responseBuffer.push((char*) &r, sizeof(r));

        for(int i = 0; i<h; i++)
            hists[i].join();
    }
    
    gettimeofday (&end, 0);

    // print the results
    cout << "about to print" << endl;
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    cout << "All Done!!!" << endl;
    wait(0);
    delete chan;
    
}
