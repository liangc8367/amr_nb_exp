//============================================================================
// Name        : amr-nb-framer.cpp
// Author      : 
// Version     :
// Copyright   : Your copyright notice
// Description : deframe amr-nb file, and replace certain percentage frames
//               with no-data or bad audio,
//               and send it to udp
//============================================================================

#include <iostream>
using namespace std;

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sched.h>
#include <string.h>
#include <errno.h>


class AutoFile
{
public:
    AutoFile(const char *name):m_name(name), m_fileHandle(NULL){};
    ~AutoFile();
    bool open(const char *mode);
    bool close();
    int write(unsigned char *buf, int size);
    int read ( void * ptr, size_t count);
    bool eof();

private:
    const char *    m_name;
    FILE *          m_fileHandle;
};

bool AutoFile::open(const char *mode)
{
    if(m_fileHandle){
        return false;
    }
    m_fileHandle = fopen(m_name, mode);
    if(!m_fileHandle){
        cout << "failed to open: " << m_name << ", mode=" << mode << endl;
    }
    return m_fileHandle;
}

bool AutoFile::close()
{
    if(m_fileHandle){
        fclose(m_fileHandle);
        return true;
    } else {
        return false;
    }
}

AutoFile::~AutoFile()
{
    close();
}

int AutoFile::write(unsigned char *buf, int size)
{
    if(!m_fileHandle){
        return -1;
    }
    fwrite(buf, sizeof(char), size, m_fileHandle);
    return size;
}

int AutoFile::read(void * ptr, size_t count)
{
    if(!m_fileHandle){
        return -1;
    }
    return fread(ptr, 1, count, m_fileHandle);
}

bool AutoFile::eof()
{
    if(m_fileHandle){
        return feof(m_fileHandle);
    }
    return true;
}

/// amr-nb file structure
typedef enum {
    AMR_475,
    AMR_515,
    AMR_590,
    AMR_670,
    AMR_740,
    AMR_795,
    AMR_102,
    AMR_122,
    AMR_SID, // 8
    AMR_9,
    AMR_10,
    AMR_11,
    AMR_12,
    AMR_13,
    AMR_14,
    AMR_NODATA,
} AMR_FRAME_TYPE;

#include <stdint.h>
#define ROUNDUP_TO_BYTES(x) (x+7)/8

const unsigned int AMR_FRAME_BITS[] = {
        95,     // 0
        103,    // 1
        118,    // 2
        134,    // 3
        148,    // 4
        159,    // 5
        204,    // 6
        244,    // 7
        39,      // 8
        0,
        0,
        0,
        0,
        0,
        0,
        0,      //15
};

const char *AMR_FILE_HEADER_SINGLE_CHANNEL = "#!AMR\n";
typedef struct {
    unsigned char p1:2,
                  q :1,
                  ft:4,
                  p2:1;
}AmrSpeechFrameHeader_t;

typedef struct {
    unsigned char p1:1,
                  mi:3,
                  sti:1,
                  s: 3;
}AmrSpeechFrameSIDByte4_t;

bool gbStolenFrames = false;

/// I don't want fancy oo design, I just want to manipulate the amr file
void processAmrData(AutoFile &ifile, int sockfd, struct sockaddr_in &servaddr, int szServaddr)
{
    unsigned char buf[200];
    int sz;

    sz = ifile.read(buf, 6);
    if(!sz){
        cout << "failed read header" << endl;
        return;
    }

    if(memcmp(buf, AMR_FILE_HEADER_SINGLE_CHANNEL, strlen(AMR_FILE_HEADER_SINGLE_CHANNEL))){
        cout << "not amr single channel file" << endl;
        return;
    }


    AmrSpeechFrameHeader_t header;
    cout << "size of header " << sizeof(header) << endl;
    int frameCount = 0;
    int stolenFrameCount = 0;
    while(!ifile.eof()){
        ifile.read((void *)&header, sizeof(header));
        if(header.ft != AMR_740 ){
            cout << "unsupported frame type:" << header.ft << " at frame @" << frameCount << endl;
            return;
        }
        if(!header.q){
            cout << "frame@" << frameCount << ": bad quality" << endl;
        }


        int szAudioBits = ROUNDUP_TO_BYTES(AMR_FRAME_BITS[AMR_740]);
        unsigned char audioBits[szAudioBits];
        ifile.read(audioBits, szAudioBits);

        uint8_t audioFrame[1000];
        int szAudioFrame = 0;

        // steal one in every 8 frame
        if( gbStolenFrames && !(frameCount % 8)  ){
#define WITH_NO_DATA 0
#if WITH_NO_DATA
            // replace frame at 0.5s with no-data
            header.ft = AMR_NODATA;
            header.q = 1; // good quality
            ofile.write((unsigned char *)&header, sizeof(header));
//            memset(audioBits, 0, szAudioBits);
//            ofile.write((unsigned char *)audioBits, szAudioBits);
#else
            // replace with SID_FIRST, comfort noise
            header.ft = AMR_SID;
            header.q = 1;
//            ofile.write((unsigned char *)&header, sizeof(header));
            memset(audioBits, 0, szAudioBits);
//            ofile.write((unsigned char *)audioBits, 4);
            AmrSpeechFrameSIDByte4_t byte4;
            byte4.s = 0;
            byte4.mi = 1; // mode#4, LSB first; 001
            byte4.sti = 0; // for first
            byte4.p1 = 0;
//            ofile.write((unsigned char *)&byte4, 1);

            memcpy(audioFrame, (uint8_t *)&header, sizeof(header));
            szAudioFrame = sizeof(header);
            memcpy(audioFrame + szAudioFrame, (uint8_t *)audioBits, 4);
            szAudioFrame += 4;
            memcpy(audioFrame + szAudioFrame, (uint8_t *)&byte4, 1);
            szAudioFrame += 1;


#endif
            ++stolenFrameCount;
        } else {
//            ofile.write((unsigned char *)&header, sizeof(header));
//            ofile.write((unsigned char *)audioBits, szAudioBits);
            memcpy(audioFrame, (uint8_t *)&header, sizeof(header));
            memcpy(audioFrame + sizeof(header), (uint8_t *)audioBits, szAudioBits);

            szAudioFrame = sizeof(header) + szAudioBits;
        }
        sendto(sockfd,audioFrame, szAudioFrame,0,
               (struct sockaddr *)&servaddr,szServaddr);


        ++frameCount;

        // sleep

        // show progress every 2s
        if(!(frameCount % (50 * 2))){
            cout << "txed: " << frameCount << endl;
        }

        //wait 20ms
        struct timespec ts_req, ts_remaining;
        ts_req.tv_sec = 0;
        ts_req.tv_nsec = 20 * 1000 * 1000ull; //20ms
        int err = nanosleep(&ts_req, &ts_remaining);
        if(err){
            cout << "! nanosleep failed: " << err << endl;
        }

    }

    cout << "total frames: " << frameCount << endl;
    cout << "stolen: " << stolenFrameCount << endl;
}

int main(int argc, char *argv[]) {
    if(argc!=4){
        cout << "client [ip] [port] [audio file]" << endl;
        return -1;
    }

    const char *str_fname = argv[3];
    const char *str_port = argv[2];
    const char *str_ip   = argv[1];

    short port = atoi(str_port);

    cout << "streaming " << str_fname << " to "
            << str_ip << ":" << port << endl;

    // change priority and policy
    int pri_min = sched_get_priority_min(SCHED_RR);
    int pri_max = sched_get_priority_max(SCHED_RR);
    int pri = (pri_min + pri_max)/2;
    struct sched_param sched_p;
    sched_p.__sched_priority = pri;
    int e = sched_setscheduler(0, SCHED_RR, &sched_p);
    cout << "setting RoundRobin scheduler, with priority " << pri;
    if(!e){
        cout << endl;
    }else{
        cout << "failed: " << strerror(errno) << endl;
    }

    AutoFile audioFile(str_fname);
    if(!audioFile.open("rb")){
        cout << "failed to open audio file: " << str_fname << endl;
        return -1;
    }

    // create client socket
    int sockfd,n;
    struct sockaddr_in servaddr,cliaddr;
    char sendline[1000];
    sockfd=socket(AF_INET,SOCK_DGRAM,0);

    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=inet_addr(str_ip);
    servaddr.sin_port=htons(port);

    processAmrData(audioFile, sockfd, servaddr, sizeof(servaddr));
    return 0;
}
