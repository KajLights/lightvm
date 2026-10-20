#include "render_executor.h"
#include <pthread.h>
#include <stdlib.h>

static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static RenderJob *assignment;
static bool started,stopping,complete;
static _Thread_local bool worker;

static void *run(void *unused){
    (void)unused;worker=true;
    pthread_mutex_lock(&mutex);
    for(;;){
        while(!assignment&&!stopping)pthread_cond_wait(&changed,&mutex);
        if(stopping)break;
        RenderJob *job=assignment;
        pthread_mutex_unlock(&mutex);
        render_job_execute(job);
        pthread_mutex_lock(&mutex);
        assignment=NULL;complete=true;pthread_cond_broadcast(&changed);
    }
    pthread_mutex_unlock(&mutex);return NULL;
}
void render_platform_init(unsigned workers){
    if(started||workers<2)return;
    stopping=complete=false;assignment=NULL;
    if(pthread_create(&thread,NULL,run,NULL))abort();
    started=true;
}
void render_platform_shutdown(void){
    if(!started)return;
    pthread_mutex_lock(&mutex);stopping=true;pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&mutex);pthread_join(thread,NULL);started=false;
}
unsigned render_platform_workers(void){return started?2:1;}
bool render_platform_is_worker(void){return worker;}
void render_platform_submit(RenderJob *job){
    pthread_mutex_lock(&mutex);
    if(assignment||!started)abort();
    complete=false;assignment=job;pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&mutex);
}
void render_platform_join(void){
    pthread_mutex_lock(&mutex);
    while(!complete)pthread_cond_wait(&changed,&mutex);
    complete=false;pthread_mutex_unlock(&mutex);
}
