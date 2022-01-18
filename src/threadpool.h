#pragma once
#include <iostream>
#include <pthread.h>

typedef struct signalinfo
{
  bool flag;
  pthread_cond_t awake_cond;
  pthread_mutex_t lock;
  int id;
  void (*fn_ptr)(void * arg);
  void *arg;
}signalinfo_t;

typedef struct threadinfo
{
  pthread_t handler;
  signalinfo_t info;
}threadinfo_t;

static void * thread_fn(void *arg)
{
    int status;

    for (;;)
    {
      status = pthread_mutex_lock(&( (*((signalinfo_t *)arg)).lock));
      while ((*((signalinfo_t *)arg)).flag == false)
      {
        status = pthread_cond_wait(&((*((signalinfo_t *)arg)).awake_cond), &((*((signalinfo_t *)arg)).lock));
      }
      status = pthread_mutex_unlock(&((*((signalinfo_t *)arg)).lock));
      (*((signalinfo_t *)arg)).fn_ptr( (*((signalinfo_t *)arg)).arg);
       status = pthread_mutex_lock(&( (*((signalinfo_t *)arg)).lock));
      (*((signalinfo_t *)arg)).flag = false;
      status = pthread_mutex_unlock(&((*((signalinfo_t *)arg)).lock));
     
    }

    return NULL;
}

class threadpool {
public:
  threadpool(int countarg) : threadcount(countarg)
  {
      threadpool_handler = (threadinfo_t *)malloc(threadcount * sizeof(threadinfo_t));
      pthread_attr_init(&common_thread_attr);
      for (int i = 0; i < threadcount; i++)
      {
          threadpool_handler[i].info.flag = false;
          threadpool_handler[i].info.awake_cond  = PTHREAD_COND_INITIALIZER;
          threadpool_handler[i].info.lock = PTHREAD_MUTEX_INITIALIZER;
          threadpool_handler[i].info.id = i;
          pthread_create(&(threadpool_handler[i].handler), &common_thread_attr, thread_fn, &(threadpool_handler[i].info));
      }
  }

  bool work(void fn_ptr(void * arg), void *arg)
  {
    bool ret_val = false;
    int status;

    for (int i = 0; i < threadcount; i++)
    {
      status = pthread_mutex_lock(&(threadpool_handler[i].info.lock));
      if (threadpool_handler[i].info.flag == false)
      {
        threadpool_handler[i].info.flag = true;
        threadpool_handler[i].info.fn_ptr = fn_ptr;
        threadpool_handler[i].info.arg = arg;
        ret_val = true;
        status = pthread_cond_broadcast(&(threadpool_handler[i].info.awake_cond));
      }
      status = pthread_mutex_unlock(&(threadpool_handler[i].info.lock));
      if (ret_val == true)
      {
          break;
      }
    }

    return ret_val;
  }

  ~threadpool()
  {
    int status;

    for (int i = 0; i < threadcount; i++)
    {
      status = pthread_cancel(threadpool_handler[i].handler);
      if (status == 0)
      {
        status = pthread_join(threadpool_handler[i].handler, NULL);
        if (status != 0)
        {
          std::cerr << "error in pthread_join() for thread " << threadpool_handler[i].info.id << std::endl;
        }
      }
      else
      {
        std::cerr << "error in pthread_cancel() for thread " << threadpool_handler[i].info.id << std::endl;
      }
    }
  }

private:
  int threadcount;
  threadinfo_t *threadpool_handler;
  pthread_attr_t common_thread_attr;
};
