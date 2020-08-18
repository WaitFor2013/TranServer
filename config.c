#include <stdio.h>
#include "trans.h"

int getKeyAndValue(char * keyAndValue,trans_para * param){
    char *p = keyAndValue;
    p = strstr(keyAndValue, "=");
    if(p == NULL){
        return 0;
    }

    param->hlsName = (char*)malloc(sizeof(char) * 1024);
    param->rtsp =(char*)malloc(sizeof(char) * 1024);
    int valueStart = 0,ki =0,vi=0;

    while (*(keyAndValue) != '\0')
    {
        char idxChar = *(keyAndValue++);

        if(idxChar == '=' && !valueStart){
            valueStart = 1;
            continue;
        }
        if(idxChar == ' ' || idxChar == '\n' || idxChar == '\r')
            continue;
        
        if(valueStart){
           param->rtsp[vi++] = idxChar;
        }else{
           param->hlsName[ki++] = idxChar;
        }
    }

    return 1;
}

//读取配置文件
int readCFG(const char *filename/*in*/,trans_para * trans_array){

    int ret = 0;
    FILE *pf = NULL;
    pf = fopen(filename, "r");

    if(pf == NULL)
        return 0;

    while(!feof(pf)){
        char line[1024] = {0};
        fgets(line, 1024, pf);

        trans_para para = {};
        if(getKeyAndValue(line,&para)){
            *(trans_array++) = para;
            ret++;
        }
    }

    if(pf != NULL)
        fclose(pf);
    
    return ret;
}
