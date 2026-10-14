// Executes public wait APIs from the built runtime, including the real callback queue.
#include <rex/thread.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <time.h>
using namespace rex::thread;
using namespace std::chrono_literals;
static std::atomic<unsigned> checks{0};
static void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::abort();}}
static double CpuMs(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1000.0+t.tv_nsec/1.0e6;}
static void Join(std::unique_ptr<Thread>& t){Check(bool(t),"created thread");Check(Wait(t.get(),false,2s)==WaitResult::kSuccess,"thread completion");}
int main(){
 Check(Thread::GetCurrentThread()!=nullptr,"main thread registration");
 for(unsigned iteration=0;iteration<120;++iteration){
  auto ready=Event::CreateAutoResetEvent(false),never=Event::CreateManualResetEvent(false);
  auto t=Thread::Create({},[&]{if(iteration%2)std::this_thread::yield();ready->Set();});
  auto result=WaitAny({never.get(),ready.get()},true,2s);Check(result.first==WaitResult::kSuccess&&result.second==1,"real event signal");Join(t);
 }
 for(unsigned iteration=0;iteration<120;++iteration){
  auto ready=Event::CreateManualResetEvent(false),never=Event::CreateManualResetEvent(false);
  std::atomic<unsigned> calls{0};std::atomic<bool> waiting{false};
  auto t=Thread::Create({},[&]{waiting=true;auto result=WaitAny({never.get(),ready.get()},true,2s);Check(result.first==WaitResult::kUserCallback,"callback interrupts actual wait");});
  Check(bool(t),"callback target created");for(;!waiting;)std::this_thread::yield();
  t->QueueUserCallback([&]{++calls;Check(Thread::GetCurrentThread()->system_id()==t->system_id(),"callback target affinity");});
  Join(t);Check(calls==1,"callback exactly once");
 }
 // Pending callback on a non-alertable wait remains queued until alertable entry.
 {
  auto go=Event::CreateAutoResetEvent(false),never=Event::CreateManualResetEvent(false);std::atomic<bool> waiting{false};std::atomic<unsigned> calls{0};
  auto t=Thread::Create({},[&]{waiting=true;Check(WaitAny({go.get(),never.get()},false,2s).first==WaitResult::kSuccess,"nonalertable event");Check(calls==0,"callback stayed pending");Check(WaitAny({go.get(),never.get()},true,0ms).first==WaitResult::kUserCallback,"pending callback before zero probe");});
  for(;!waiting;)std::this_thread::yield();t->QueueUserCallback([&]{++calls;});go->Set();Join(t);Check(calls==1,"pending callback delivered");
 }
 // An alertable nested wait restores the outer alertable state for a self-queued callback.
 {
  auto never=Event::CreateManualResetEvent(false),also_never=Event::CreateManualResetEvent(false);unsigned calls=0;
  auto t=Thread::Create({},[&]{auto* self=Thread::GetCurrentThread();self->QueueUserCallback([&]{Check(WaitAny({never.get(),also_never.get()},true,0ms).first==WaitResult::kTimeout,"nested zero wait");self->QueueUserCallback([&]{++calls;});});Check(WaitAny({never.get(),also_never.get()},true,2s).first==WaitResult::kUserCallback,"outer callback");Check(calls==1,"outer alertable restored");});Join(t);
 }
 // Actual timer queue notifies a multi-wait and permits cancellation and reuse.
 {
  auto timer=Timer::CreateSynchronizationTimer();auto never=Event::CreateManualResetEvent(false);
  for(unsigned n=0;n<12;++n){Check(timer->SetOnceAt(std::chrono::steady_clock::now()+2ms),"timer schedule");auto result=WaitAny({never.get(),timer.get()},true,2s);Check(result.first==WaitResult::kSuccess&&result.second==1,"timer multi-wake");Check(WaitAny({never.get(),timer.get()},true,0ms).first==WaitResult::kTimeout,"sync timer consumption");}
  Check(timer->SetOnceAt(std::chrono::steady_clock::now()+500ms),"timer reschedule");Check(timer->Cancel(),"timer cancel");Check(WaitAny({never.get(),timer.get()},true,5ms).first==WaitResult::kTimeout,"cancel does not signal");
 }
 // Create-suspended startup followed by cooperative shutdown.
 {
  Thread::CreationParameters p;p.create_suspended=true;std::atomic<bool> began{false};auto stop=Event::CreateManualResetEvent(false),never=Event::CreateManualResetEvent(false);
  auto t=Thread::Create(p,[&]{began=true;Check(WaitAny({stop.get(),never.get()},true,2s).first==WaitResult::kSuccess,"shutdown wake");});Check(bool(t),"suspended create");Check(!began,"not started early");Check(t->Resume(),"resume");stop->Set();Join(t);Check(began,"resumed task executed");
 }
 {
  auto a=Event::CreateManualResetEvent(false),b=Event::CreateManualResetEvent(false);double before=CpuMs();auto start=std::chrono::steady_clock::now();Check(WaitAny({a.get(),b.get()},true,200ms).first==WaitResult::kTimeout,"idle alertable deadline");double cpu=CpuMs()-before,elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();std::printf("IDLE elapsed_ms=%.6f running_cpu_ms=%.6f\n",elapsed,cpu);Check(cpu<20,"idle does not poll");
 }
 std::printf("PASS %u assertions\n",checks.load());
}
