/*
 *
 * 简易转码服务（从rtsp转码为hls）
 * 1、读取配置文件，获取可以转码的rtsp流和标示名
 * 2、基于http请求
 *      如果有标示名，则开启转码服务
 *      无http请求，延时关闭转码服务
 */
#include <stdlib.h>
#include <sys/stat.h>
#include <event.h>
#include <evhttp.h>
#include <pthread.h>
#include <time.h>
#include "apr/apr_getopt.h"
#include "apr/apr_strings.h"
#include "apr/apr_file_io.h"
#include "apr/apr_file_info.h"
#include "trans.h"
#include "config.h"

#define DEFAULT_HTTP_ADDR      "0.0.0.0"
#define DEFAULT_HTTP_PORT      8080
#define DEFAULT_MIME_TYPE      "application/ocet-stream"
#define MAX_TRANS_SIZE         1024
#define MAX_IDLE_DURATION     20

typedef struct {
    unsigned int svr_port;   /* server port */
    int daemonize;           /* daemonize option: 0-off, 1-on */
} httpsvr_config;

/* main global memory pool*/
static apr_pool_t *g_mem_pool = NULL;
/* global httpsvr config */
static httpsvr_config *g_config = NULL;

/* 当前支持1024个配置视频 */
static trans_para transParams[MAX_TRANS_SIZE];
static int total_para_size;

/* 视频转码记录 */ 
typedef struct started_trans{
    char *hlsName; // 转换名
    pthread_t tid; // 转换线程ID
    time_t t; // 最近Http请求时间
} started_trans;

static started_trans startedTrans[MAX_TRANS_SIZE];
static int volatile start_trans_size =0;

static const int has_trans(char *hlsName){
    int i=0;
    while (i < start_trans_size)
    {
        if(strstr(startedTrans[i].hlsName,hlsName) != NULL){
            // 更新时间戳
            time_t tt;
            time(&tt);
            startedTrans[i].t = tt;

            return 1;
        }

        i++;
    }
    return 0;
}

static const int start_trans(char *hlsName){
    int i=0;
    while (i < total_para_size)
    {
        if(strstr(transParams[i].hlsName,hlsName) != NULL){
            
            pthread_t pt;
            time_t tt;

            char *out_filename = (char *) malloc(strlen(hlsName)*2 + 6);
            strcpy(out_filename, hlsName);
            strcat(out_filename, "/hls.m3u8");

            //删除遗留转码目录，需要递归删除文件，暂不做处理
            remove(out_filename);

            pthread_attr_t attr; 
            pthread_attr_init( &attr ); 
            pthread_attr_setdetachstate(&attr,1);
            pthread_create(&pt, &attr, trans, &transParams[i]);
            
            started_trans start = {};
            start.hlsName = (char*)apr_pstrdup(g_mem_pool, transParams[i].hlsName);
            //更新时间戳
            time(&tt);
            start.tid = pt;
            start.t = tt;
            startedTrans[start_trans_size++] = start;
            return 1; 

        }
        
        i++;
    }
    return 0;
}

/* 轮训查看转换是否需要关闭 */
static const void dispose_trans(){
    int i =0;
    time_t current;
    time(&current);

    fprintf(stdout,"检查当前转换线程[%d] \n",start_trans_size);
    
    while (i < start_trans_size)
    {
        int duration = current - startedTrans[i].t;
        fprintf(stdout,"转换名【%s】线程ID【%d】持续秒【%d】\n",startedTrans[i].hlsName,startedTrans[i].tid,duration);
        if(duration > MAX_IDLE_DURATION){
            //终止线程
            fprintf(stdout,"停止转换【%s】终止线程【%d】\n",startedTrans[i].hlsName,startedTrans[i].tid);
            pthread_cancel(startedTrans[i].tid);

            //覆盖处理
            startedTrans[i] = startedTrans[start_trans_size-1];
            start_trans_size--;
        }
        i++;
    }

    //递归调用
    sleep(5);
    dispose_trans();
}

/* code from mattows.c */
typedef struct EXT2MIME {
   const char *type;
   const char *ext[];
} ext2mime;
static const ext2mime ext_m3u8  = {"application/vnd.apple.mpegurl",{"m3u8",NULL}};
static const ext2mime ext_ts  = {"video/MP2T",{"ts",NULL}};

static const ext2mime* const extensions[] = {
   &ext_m3u8, &ext_ts, NULL
};

static const char *find_mime_type(char *buf)
{
   const ext2mime *e2m;
   int i,j;
   for(i=0; (e2m=extensions[i]); ++i)
      for(j=0; e2m->ext[j]; ++j)
         if(!strcasecmp(buf,e2m->ext[j]))
            return e2m->type;
   return DEFAULT_MIME_TYPE;
}

static void usage(void) {
    fprintf(stderr,
           "Usage: vs_httpd  [-p port] [-D]\n"
           "Options:\n"
           "  -p port         : define server port (default: 8080) \n"
           "  -D              : daemonize option 0-off,1-on (default: 0) \n"
        );
    exit(1);
}

static void httpsvr_config_init(void)
{
    g_config = apr_pcalloc(g_mem_pool, sizeof(httpsvr_config));
    g_config->svr_port = DEFAULT_HTTP_PORT;
    g_config->daemonize = 0;
}

static int exists(const char* path, char **complemented_path, int *filesize)
{
    struct stat sb;
    if(stat(path, &sb)<0){
        return 0;
    }
    if( S_ISREG(sb.st_mode)) {
        *complemented_path = apr_pstrdup(g_mem_pool, path);
         *filesize = (int)sb.st_size;
        return 1;
    }
    return 0;
}

void main_request_handler(void *args)
{
    //struct evhttp_request *r
    struct evhttp_request *r;
    r = (struct evhttp_request *)args;

    apr_status_t rv;
    struct evbuffer *evbuf;
    const char *path, *mimetype;
    char *complemented_path, *extbuf, *filebuf;
    int filesize = 0;

    /* check reqeust type. currently only suppoert GET */
    if (r->type != EVHTTP_REQ_GET) {
        fprintf(stdout, "only support GET request \n");
        evhttp_send_error(r, HTTP_BADREQUEST, "only support GET request");
        return;
    }

    const char * rq_url = evhttp_request_uri(r);
    
    fprintf(stdout, "req uri=%s\n", rq_url);

    char * path_check_m3u8 =strstr(rq_url, "hls.m3u8");
    char * path_check_ts =strstr(rq_url, "ts");

    if(path_check_m3u8 == NULL && path_check_ts == NULL){
        evhttp_send_error(r, HTTP_BADREQUEST, "request path is invalid ,must end with hls.m3u8 or ts");
        return;
    }
    
    char tempName[128];
    int startIdx=0,tIdx=0;

    while (*(rq_url) != '\0')
    {
        char idxChar = *(rq_url++);
        if(idxChar == '/' && !startIdx){
            startIdx =1;
            continue;
        }
        if(idxChar == '/' && startIdx){
            break;
        }
        if(startIdx){
            tempName[tIdx++] = idxChar;
        }
    }
    tempName[tIdx]='\0';

    if(!has_trans(tempName)){
        if(!start_trans(tempName)){
            evhttp_send_error(r, HTTP_BADREQUEST, "request path is invalid, must has trans config.");
            return;
        }
    }

    //去除查询字符串
    char pathWithOutQuery[128];
    const char * rq_temp = evhttp_request_uri(r);
    int zIdx=0;
    while (*(rq_temp) != '\0' && *(rq_temp) != '?')
    {
        char idxChar = *(rq_temp++);
        pathWithOutQuery[zIdx++] = idxChar;
    }
    pathWithOutQuery[zIdx] = '\0';

    path = apr_psprintf(g_mem_pool, "%s%s","./", pathWithOutQuery);

    int wait=0;
    while (NULL != path_check_m3u8 && !exists(path, &complemented_path, &filesize))
    {
        sleep(1);
        if(++wait > 12){
            break;
        }
    }

    /* file or dir existence check */
    if (!exists(path, &complemented_path, &filesize)) {
        evhttp_send_error(r, HTTP_NOTFOUND, "file not found");
        return;
    }
    /* file's extension check */
    mimetype = apr_pstrdup(g_mem_pool, DEFAULT_MIME_TYPE);
    extbuf = strrchr(complemented_path,'.');
    if (extbuf) {
        ++extbuf;
        mimetype = find_mime_type(extbuf);
    }

    /* file read */
    filebuf = apr_palloc(g_mem_pool, filesize + 1);
    apr_file_t *file = NULL;
    rv = apr_file_open(&file, complemented_path,
                       APR_READ|APR_BINARY, APR_OS_DEFAULT, g_mem_pool);
    if (rv != APR_SUCCESS) {
        evhttp_send_error(r, HTTP_SERVUNAVAIL, "failed to open file");
        return;
    }
    apr_size_t len = filesize;
    rv = apr_file_read(file, filebuf, &len);
    if (rv != APR_SUCCESS) {
        evhttp_send_error(r, HTTP_SERVUNAVAIL, "failed to read file");
        return;
    }
    apr_file_close(file);

    evbuf = evbuffer_new();
    if (!evbuf) {
        fprintf(stderr, "failed to create response buffer\n");
        evhttp_send_error(r, HTTP_SERVUNAVAIL, "failed to create response buffer");
        return;
    }
    evhttp_add_header(r->output_headers, "Access-Control-Allow-Origin","*");
    evhttp_add_header(r->output_headers, "Content-Type",mimetype);
    evhttp_add_header(r->output_headers, "Content-Length", apr_psprintf(g_mem_pool,"%d",filesize));
    evbuffer_add(evbuf, filebuf, len);
    evhttp_send_reply(r, HTTP_OK, "", evbuf);
    evbuffer_free(evbuf);
}

void main_request_muti_thread(struct evhttp_request *r, void *args){
    pthread_t pt;
    pthread_attr_t attr; 
    pthread_attr_init( &attr ); 
    pthread_attr_setdetachstate(&attr,1);
    pthread_create(&pt, &attr, main_request_handler, r);
    fprintf(stdout,"开启线程处理请求，线程ID【%d】\n",pt);
}

int main(int argc, char **argv)
{

    total_para_size = readCFG("config.ini",&transParams);

    if(total_para_size < 1){
        fprintf(stderr,"抱歉，缺少配置信息，请在运行目录下，【config.ini】文件中配置转码明细\n");
        exit(1);
    }

    int ti = 0;
    fprintf(stdout,"[^_^:服务说明                        \n");
    fprintf(stdout,"[^_^:          -D        //后台运行   \n");
    fprintf(stdout,"[^_^:          -p 8080   //指定端口   \n");
    fprintf(stdout,"[^_^:当前目录下存放配置文件config.ini。     \n");
    fprintf(stdout,"[^_^:结束说明。\n\n");

    fprintf(stdout,"[拼命启动中                   \n");
    fprintf(stdout,"[读取配置文件config.ini              \n");
    while (ti < total_para_size)
    {
        printf("[%d:%s=%s。\n",ti,transParams[ti].hlsName,transParams[ti].rtsp);
        ti++;
    }
    fprintf(stdout,"[视频数:%d。                         \n",total_para_size);

    char ch;
    apr_getopt_t *opt;
    const char *optarg;
    struct evhttp *httpd;

    /* apr initialize */
    apr_initialize();
    apr_pool_create(&g_mem_pool, NULL);
    
    /* server config initialize */
    httpsvr_config_init();

    /* option parse and get */
    if ( apr_getopt_init(&opt, g_mem_pool, argc, (const char * const *)argv) != APR_SUCCESS) {
        fprintf(stderr, "failed to init apr_getopt \n");
        exit(1);
    }
    
    while (apr_getopt(opt, "p:Dh", &ch, &optarg) == APR_SUCCESS) {
        switch (ch) {
        case 'p':
            if (sscanf(optarg, "%u", &(g_config->svr_port)) != 1) {
                fprintf(stderr, "invalid -p option\n");
                exit(1);
            }
            break;
        case 'D':
            g_config->daemonize=1;
            break;
        case 'h':
        default:
            usage();
            break;
        }
    }

    /* daemonize */
    if ( g_config->daemonize ){
        pid_t daemon_pid =fork();
        if (daemon_pid < 0 ){
            fprintf(stderr, "daemonize failure\n");
            exit(1);
        }
        if (daemon_pid){
            /* scceeded in fork, then parent exit */
            exit(1);
        }
        /* move on to child */
        if (setsid() == -1)
            exit(1);
    }

    /* event driven http */
    event_init();
    httpd = evhttp_start(DEFAULT_HTTP_ADDR, g_config->svr_port);

    fprintf(stdout,"[TIMS视频转码服务成功启动，端口%d。\n",g_config->svr_port);

    pthread_t disposeId;
    pthread_attr_t attr; 
    pthread_attr_init( &attr ); 
    pthread_attr_setdetachstate(&attr,1);
    pthread_create(&disposeId, &attr, dispose_trans, NULL);

    evhttp_set_gencb(httpd, main_request_muti_thread, NULL);

    event_dispatch();
    evhttp_free(httpd);

    /* apr destruction */
    apr_pool_destroy(g_mem_pool);
    apr_terminate();
    
    return 0;
}