// POSIX implementation of zodiac::platform::run_process (see platform/process.hpp).
//
// The child is started with posix_spawn (no shell, arguments verbatim). Standard output and standard error are duplicated
// onto one pipe that is read until EOF; standard input is /dev/null.
#include <platform/process.hpp>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {
struct Descriptor {
int value=-1;
Descriptor()=default;
Descriptor(const Descriptor&)=delete;
Descriptor& operator=(const Descriptor&)=delete;
~Descriptor() { reset(); }
void reset() noexcept
{
if(value>=0)close(value);
value=-1;
}
};

struct FileActions {
posix_spawn_file_actions_t actions;
FileActions() { posix_spawn_file_actions_init(&actions); }
FileActions(const FileActions&)=delete;
FileActions& operator=(const FileActions&)=delete;
~FileActions() { posix_spawn_file_actions_destroy(&actions); }
};

std::string errno_text(int code)
{
return std::strerror(code);
}
}

namespace zodiac::platform {
std::expected<ProcessResult,std::string> run_process(std::span<const std::string> argv)
{
if(argv.empty())return std::unexpected("no program to run");
std::vector<char*> arguments;
arguments.reserve(argv.size()+1);
for(const std::string& argument:argv)arguments.push_back(const_cast<char*>(argument.c_str()));
arguments.push_back(nullptr);

int pipe_fds[2];
if(pipe(pipe_fds)!=0)return std::unexpected("cannot create the output pipe: "+errno_text(errno));
Descriptor read_end;
Descriptor write_end;
read_end.value=pipe_fds[0];
write_end.value=pipe_fds[1];
// Neither end may leak into the child except through the explicit duplication below (dup2 clears the flag on the copy).
fcntl(read_end.value,F_SETFD,FD_CLOEXEC);
fcntl(write_end.value,F_SETFD,FD_CLOEXEC);

FileActions file_actions;
int status=posix_spawn_file_actions_addopen(&file_actions.actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
if(status==0)status=posix_spawn_file_actions_adddup2(&file_actions.actions,write_end.value,STDOUT_FILENO);
if(status==0)status=posix_spawn_file_actions_adddup2(&file_actions.actions,write_end.value,STDERR_FILENO);
if(status!=0)return std::unexpected("cannot prepare "+argv[0]+": "+errno_text(status));

pid_t child=0;
status=posix_spawn(&child,argv[0].c_str(),&file_actions.actions,nullptr,arguments.data(),environ);
if(status!=0)return std::unexpected("cannot start "+argv[0]+": "+errno_text(status));
// Only the child keeps the write end now, so reading ends when the child (and anything it spawned) closes it.
write_end.reset();

std::string output;
char buffer[4096];
for(;;){
const ssize_t count=read(read_end.value,buffer,sizeof(buffer));
if(count>0){
output.append(buffer,static_cast<std::size_t>(count));
continue;
}
if(count<0&&errno==EINTR)continue;
break;
}
int wait_status=0;
pid_t waited;
do waited=waitpid(child,&wait_status,0);
while(waited<0&&errno==EINTR);
if(waited<0)return std::unexpected("cannot wait for "+argv[0]+": "+errno_text(errno));
int code=-1;
if(WIFEXITED(wait_status))code=WEXITSTATUS(wait_status);
else if(WIFSIGNALED(wait_status))code=128+WTERMSIG(wait_status);
return ProcessResult{code,std::move(output)};
}
}
