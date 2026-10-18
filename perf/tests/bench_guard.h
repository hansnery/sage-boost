/* Performance samples must not compete with a running game/WorldBuilder.
 * Read-only process enumeration; never pause, terminate or alter a process. */
#ifndef PF_BENCH_GUARD_H
#define PF_BENCH_GUARD_H
#include <tlhelp32.h>

static int bench_no_game(void)
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32W process;
    BOOL have;
    DWORD error;
    if(snapshot==INVALID_HANDLE_VALUE) return 0;
    memset(&process,0,sizeof process);
    process.dwSize=sizeof process;
    have=Process32FirstW(snapshot,&process);
    while(have) {
        if(!_wcsicmp(process.szExeFile,L"game.dat") ||
           !_wcsicmp(process.szExeFile,L"lotrbfme2.exe") ||
           !_wcsicmp(process.szExeFile,L"lotrbfme2ep1.exe") ||
           !_wcsicmp(process.szExeFile,L"worldbuilder.exe")) {
            fprintf(stderr,"benchmark refused: game/WorldBuilder PID %lu is running\n",
                    (unsigned long)process.th32ProcessID);
            CloseHandle(snapshot);
            return 0;
        }
        have=Process32NextW(snapshot,&process);
    }
    error=GetLastError();
    CloseHandle(snapshot);
    return error==ERROR_NO_MORE_FILES;
}
#endif
