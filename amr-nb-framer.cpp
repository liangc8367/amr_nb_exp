//============================================================================
// Name        : amr-nb-framer.cpp
// Author      : 
// Version     :
// Copyright   : Your copyright notice
// Description : deframe amr-nb file, and replace certain percentage frames
//               with no-data or bad audio
//============================================================================

#include <iostream>
using namespace std;

#include <stdio.h>
#include <string.h>

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

/// I don't want fancy oo design, I just want to manipulate the amr file
void processAmrData(AutoFile &ifile, AutoFile &ofile)
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

    ofile.write(buf, 6);

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

        // steal one in every 8 frame
        if( !(frameCount % 8)  ){
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
            ofile.write((unsigned char *)&header, sizeof(header));
            memset(audioBits, 0, szAudioBits);
            ofile.write((unsigned char *)audioBits, 4);
            AmrSpeechFrameSIDByte4_t byte4;
            byte4.s = 0;
            byte4.mi = 1; // mode#4, LSB first; 001
            byte4.sti = 0; // for first
            byte4.p1 = 0;
            ofile.write((unsigned char *)&byte4, 1);
#endif
            ++stolenFrameCount;
        } else {
            ofile.write((unsigned char *)&header, sizeof(header));
            ofile.write((unsigned char *)audioBits, szAudioBits);
        }

        ++frameCount;
    }

    cout << "total frames: " << frameCount << endl;
    cout << "stolen: " << stolenFrameCount << endl;
}

int main(int argc, char *argv[]) {
	cout << "!!!Hello World!!!" << endl; // prints !!!Hello World!!!

	if(argc!=3){
	    cout << "invalid params" << endl;
	    return -1;
	}

	const char *ifname = argv[1];
	const char *ofname = argv[2];

	cout << "input file: " << ifname << endl;
	cout << "output file: " << ofname << endl;

	AutoFile ifile(ifname);
	AutoFile ofile(ofname);
	if(!ifile.open("rb") || !ofile.open("wb")){
	    cout << "failed to open i/o files" << endl;
	    return -1;
	}

	processAmrData(ifile, ofile);
	return 0;
}
