/*
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date  : 2/8/20
 */
#include "common.h"
#include "FIFOreqchannel.h"
#include "MQreqchannel.h"
#include "SHMreqchannel.h"
#include <sys/wait.h>
#include <vector>

using namespace std;


int main(int argc, char *argv[]){

    int opt;
    int p = 1;
    double t = -1;
    int e = 1;
    bool c = false;
    string fname = "";
    int bufferCapacity = MAX_MESSAGE;
    string ipcMethod = "f";
    int nchannels = 0; // 0 channels (excludes control channel)
    vector<RequestChannel*> channels;

    // extract command line args for single data point
    while((opt = getopt(argc, argv, "p:t:e:f:m:c:i:")) != -1)
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
                nchannels = atoi(optarg);
                break;
            case 'f':
                fname = optarg;
                break;
            case 'm':
                bufferCapacity = atoi(optarg);
                break;
            case 'i':
                ipcMethod = optarg;
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
        char *argv[] = {"./server", "-m", (char*)to_string(bufferCapacity).c_str(), "-i", (char *) ipcMethod.c_str(), NULL};
        execvp(argv[0], argv);
    }

    RequestChannel* chan = NULL;
    if(ipcMethod == "f")
        chan = new FIFORequestChannel ("control", RequestChannel::CLIENT_SIDE);
    else if (ipcMethod == "q")
        chan = new MQRequestChannel ("control", RequestChannel::CLIENT_SIDE, bufferCapacity);
    else if (ipcMethod == "m")
        chan = new SHMRequestChannel ("control", RequestChannel::CLIENT_SIDE, bufferCapacity);

    cout << "Client is connected to the server" << endl;

    char* buf = new char[bufferCapacity];
    datamsg* x = new datamsg (p, t, e);
    int nbytes;
    double reply;

    // create new channels if c flag is true
    if(c)
    {
        // loop to create each new channels
        for(int i = 0; i < nchannels; i++)
        {
            MESSAGE_TYPE m = NEWCHANNEL_MSG;
            char buf2 [30];
            chan->cwrite(&m, sizeof(MESSAGE_TYPE));
            chan->cread(buf2, 30);
            string newChanName = buf2;
            cout << "new channel name is " << newChanName << endl;

            RequestChannel* newChan = NULL;
            if(ipcMethod == "f")
                newChan = new FIFORequestChannel (newChanName, RequestChannel::CLIENT_SIDE);
            else if (ipcMethod == "q")
                newChan = new MQRequestChannel (newChanName, RequestChannel::CLIENT_SIDE, bufferCapacity);
            else if (ipcMethod == "m")
                newChan = new SHMRequestChannel (newChanName, RequestChannel::CLIENT_SIDE, bufferCapacity);

            channels.push_back(newChan);
        }
    }

    // testing a single data message at time t
    if(t != -1)
    {
	chan->cwrite (x, sizeof (datamsg));
	nbytes = chan->cread (buf, bufferCapacity);
	reply = *(double *) buf;
    cout << "For person " << p << ", at time " << t << ", the value of ecg " << e << " is " << reply << endl;
    }
    else if(t == -1 && fname == "") // request first 1k points via all channels specified save to x1.csv
    {
        struct timeval startTime;
        struct timeval endTime;
        gettimeofday(&startTime, NULL);
        ofstream csvFile;

        for(int i = 0; i < nchannels; i++)
        {
            cout << "starting transfer on channel " << i << endl;
            csvFile.open("x1.csv");
            x->seconds = 0; // resets time to start from beginning
            for(int j = 0; j < 1000; j++)
            {
                //cout << "channel " << i << "point " << j << endl;
                x->ecgno = 1;
                channels[i]->cwrite (x, sizeof (datamsg));
                nbytes = channels[i]->cread (buf, bufferCapacity);
                double ecg1 = *(double *) buf;
                x->ecgno = 2;
                channels[i]->cwrite (x, sizeof (datamsg));
                nbytes = channels[i]->cread (buf, bufferCapacity);
                double ecg2 = *(double *) buf;
                csvFile << x->seconds << "," << ecg1 << "," << ecg2 << "\n";
                x->seconds += .004;
            }
            cout << "done transferring points for channel " << i << endl;
            csvFile.close();
        }

        gettimeofday(&endTime, NULL);
        cout << "Finsihed collecting 1000 data points across " << nchannels << " channels in " << (double)((endTime.tv_sec * 1000000 + endTime.tv_usec) - (startTime.tv_sec * 1000000 + startTime.tv_usec)) / 1000000 << " seconds." << endl;

    }

    
    // file transfer
    if(fname != "" && nchannels == 0)
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
        chan->cwrite(buf3, sizeof(buf3));

        __int64_t filelen;
        chan->cread(&filelen, sizeof(__int64_t));
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
            
            memcpy (buf3, &fm, sizeof (filemsg));
            strcpy (buf3 + sizeof (filemsg), fname.c_str());
            chan->cwrite(buf3, sizeof(buf3));
            double bytes = 0;
            while(bytes != fm.length)
            {
                int currentBytes = chan->cread(recvbuf, fm.length-bytes);
                newFile.write(recvbuf, currentBytes);
                bytes += currentBytes;
            }
            remainingLen = remainingLen - fm.length;
        }
        newFile.close();
        gettimeofday(&endTime, NULL);
        assert(remainingLen == 0);
        cout << "Finsihed transferring file " << fname << " in " << (double)((endTime.tv_sec * 1000000 + endTime.tv_usec) - (startTime.tv_sec * 1000000 + startTime.tv_usec)) / 1000000 << " seconds." << endl;
        delete recvbuf;
    }
    else if (fname != "" && nchannels > 0) // multichannel transfer
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
        chan->cwrite(buf3, sizeof(buf3));
        __int64_t filelen;
        chan->cread(&filelen, sizeof(__int64_t));
        std::cout << "File length: " << filelen << endl;
        
        int remainingLen = (int) filelen;
        int lenPerFile = floor((double)filelen/nchannels); // split up file into channels
        std::cout << "len per channel " << lenPerFile << endl;

        for(int i = 0; i < nchannels; i++)
        {
            int requests = ceil((double)lenPerFile/bufferCapacity);
            char* recvbuf = new char[bufferCapacity];
            if(!recvbuf)
                perror("recvbuf not created properly");
            int remainingLenInChan = lenPerFile;

            for(int j = 0; j < requests; j++) // base off of lenPerFile
            {
                fm.offset = (i * lenPerFile) + (j * bufferCapacity);
                if(remainingLenInChan >= bufferCapacity)
                {
                    fm.length = bufferCapacity;
                }
                else
                {
                    fm.length = remainingLenInChan;
                }
                
                memcpy (buf3, &fm, sizeof (filemsg));
                strcpy (buf3 + sizeof (filemsg), fname.c_str());
                channels[i]->cwrite(buf3, sizeof(buf3));
                double bytes = 0;
                while(bytes != fm.length)
                {
                    int currentBytes = channels[i]->cread(recvbuf, fm.length-bytes);
                    newFile.write(recvbuf, currentBytes);
                    bytes += currentBytes;
                }
                remainingLen = remainingLen - fm.length;
                remainingLenInChan = remainingLenInChan - fm.length;
            }

            assert(remainingLenInChan == 0);
            delete recvbuf;
        }

        // take care of leftover len
        if(remainingLen != 0)
        {
            std::cout << "remaining len " << remainingLen << endl;
            fm.offset = filelen - remainingLen;
            fm.length = remainingLen;
            memcpy (buf3, &fm, sizeof (filemsg));
            strcpy (buf3 + sizeof (filemsg), fname.c_str());
            chan->cwrite(buf3, sizeof(buf3));
            double bytes = 0;
            char* recvbuf = new char[bufferCapacity];
            while(bytes != fm.length)
            {
                int currentBytes = chan->cread(recvbuf, fm.length-bytes);
                newFile.write(recvbuf, currentBytes);
                bytes += currentBytes;
            }
            remainingLen = remainingLen - fm.length;
            delete recvbuf;
        }

        
        newFile.close();
        gettimeofday(&endTime, NULL);
        assert(remainingLen == 0);
        std::cout << "Finsihed transferring file " << fname << " in " << (double)((endTime.tv_sec * 1000000 + endTime.tv_usec) - (startTime.tv_sec * 1000000 + startTime.tv_usec)) / 1000000 << " seconds." << endl;
    }

    
    // clean up new ipc channels
    MESSAGE_TYPE m = QUIT_MSG;
    for(int i = 0; i < nchannels; i++)
    {
        channels[i]->cwrite (&m, sizeof (MESSAGE_TYPE));
        delete channels[i];
    }


	// closing the channel    
    m = QUIT_MSG;
    chan->cwrite (&m, sizeof (MESSAGE_TYPE));
    delete chan;

    wait(NULL);

    delete x;
    delete buf;

}
