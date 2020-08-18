
#ifndef TRANS_H
#define TRANS_H

typedef struct trans_para{
    char * rtsp;
    char * hlsName;
} trans_para;

int trans(void *arg);

#endif