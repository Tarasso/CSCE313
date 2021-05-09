/*
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date  : 2/8/20
 */
#include "common.h"
#include "FIFOreqchannel.h"
#include <sys/wait.h>

using namespace std;


int main(int argc, char *argv[]){

    int opt;
    int p = 1;
    double t = 0.0;
    int e = 1;
    bool c = false;
    string fname = "";
    int bufferCapacity = MAX_MESSAGE;

    // extract command line args for single data point
    while((opt = getopt(argc, argv, "p:t:e:f:m:c")) != -1)
    {
        switch (opt)
        {
            case 'p':
                p = atoi(optarg);
                break;
            case 't':
                t = atof(optarg);
                break;
            case 'e':
                e = atoi(optarg);
                break;
            case 'c':
                c = true;
                break;
            case 'f':
                fname = optarg;
                break;
            case 'm':
                bufferCapacity = atoi(optarg);
                break;
        }
    }

    pid_t serverPID = fork();
    if(serverPID < 0)
    {
        cout << "Error in creating server child process" << endl;
        exit(1);
    }
    else if (serverPID == 0)
    {
        char *argv[] = {"./server", "-m", (char*)to_string(bufferCapacity).c_str(), NULL};
        execvp(argv[0], argv);
    }

    FIFORequestChannel chan ("control", FIFORequestChannel::CLIENT_SIDE);

    char* buf = new char[bufferCapacity];
    datamsg* x = new datamsg (p, t, e);

    // testing a single data message
	chan.cwrite (x, sizeof (datamsg));
	int nbytes = chan.cread (buf, bufferCapacity);
	double reply = *(double *) buf;
    cout << "For person " << p << ", at time " << t << ", the value of ecg " << e << " is " << reply << endl;

    //collect 1000 data points and save them to x1.csv
    struct timeval startTime;
    struct timeval endTime;
    gettimeofday(&startTime, NULL);
    ofstream csvFile;
    csvFile.open("x1.csv");
    for(int i = 0; i < 1000; i++)
    {
        x->ecgno = 1;
        chan.cwrite (x, sizeof (datamsg));
	    nbytes = chan.cread (buf, bufferCapacity);
	    double ecg1 = *(double *) buf;
        x->ecgno = 2;
        chan.cwrite (x, sizeof (datamsg));
	    nbytes = chan.cread (buf, bufferCapacity);
	    double ecg2 = *(double *) buf;

        csvFile << x->seconds << "," << ecg1 << "," << ecg2 << "\n";

        x->seconds += .004;
    }
    csvFile.close();
    gettimeofday(&endTime, NULL);
    cout << "Finsihed collecting 1000 data points in " << (double)((endTime.tv_sec * 1000000 + endTime.tv_usec) - (startTime.tv_sec * 1000000 + startTime.tv_usec)) / 1000000 << " seconds." << endl;
	

    // create new channel if c flag is true
    if(c)
    {
        MESSAGE_TYPE m = NEWCHANNEL_MSG;
        char buf2 [30];
        chan.cwrite(&m, sizeof(MESSAGE_TYPE));
        chan.cread(buf2, 30);
        string newChanName = buf2;
        cout << "new channel name is " << newChanName << endl;
        FIFORequestChannel newChan (newChanName, FIFORequestChannel::CLIENT_SIDE);

        x->person = 1;
        x->seconds = 0;
        x->ecgno = 1;
	
	    newChan.cwrite (x, sizeof (datamsg));
	    nbytes = newChan.cread (buf, bufferCapacity);
	    reply = *(double *) buf;
        cout << "Using channel " << newChanName << ", for person " << x->person << ", at time " << x->seconds << ", the value of ecg " << x->ecgno << " is " << reply << endl;


        m = QUIT_MSG;
        newChan.cwrite (&m, sizeof (MESSAGE_TYPE));

    }


    // file transfer
    if(fname != "")
    {
        struct timeval startTime;
        struct timeval endTime;
        gettimeofday(&startTime, NULL);
        ofstream newFile;
        string path = "received/" + fname;
        newFile.open(path, ios::binary);
        filemsg fm (0,0);
        char buf3 [sizeof (filemsg) + fname.size()+1];
        memcpy (buf3, &fm, sizeof (filemsg));
        strcpy (buf3 + sizeof (filemsg), fname.c_str());
        chan.cwrite(buf3, sizeof(buf3));

        __int64_t filelen;
        chan.cread(&filelen, sizeof(__int64_t));
        cout << "File length: " << filelen << endl;
        
        int requests = ceil((double)filelen/bufferCapacity);
        int remainingLen = (int) filelen;
        char* recvbuf = new char[bufferCapacity];
        if(!recvbuf)
        {
            cout << "recvbuf not created properly" << endl;
            exit(1);
        }
            
        for(int i = 0; i < requests; i++)
        {
            fm.offset = i * bufferCapacity;
            if(remainingLen >= bufferCapacity)
            {
                fm.length = bufferCapacity;
            }
            else
            {
                fm.length = remainingLen;
            }
            
            cout << "transferring " << fm.length << "bytes" << endl;
            memcpy (buf3, &fm, sizeof (filemsg));
            strcpy (buf3 + sizeof (filemsg), fname.c_str());
            chan.cwrite(buf3, sizeof(buf3));
            double bytes = 0;
            while(bytes != fm.length)
            {
                int currentBytes = chan.cread(recvbuf, fm.length-bytes);
                newFile.write(recvbuf, currentBytes);
                bytes += currentBytes;
            }
            cout << "recvbuf contains " << bytes << " bytes" << endl;
            remainingLen = remainingLen - fm.length;
        }
        newFile.close();
        gettimeofday(&endTime, NULL);
        assert(remainingLen == 0);
        cout << "Finsihed transferring file " << fname << " in " << (double)((endTime.tv_sec * 1000000 + endTime.tv_usec) - (startTime.tv_sec * 1000000 + startTime.tv_usec)) / 1000000 << " seconds." << endl;
    }





	// closing the channel    
    MESSAGE_TYPE m = QUIT_MSG;
    chan.cwrite (&m, sizeof (MESSAGE_TYPE));

    // pid_t wpid;
    // int status = 0;
    // while((wpid = wait(&status)) > 0);
    wait(NULL);

    delete x;
}
