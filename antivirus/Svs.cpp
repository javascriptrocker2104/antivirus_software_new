#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include "sample.h"
#include <fstream>
#include <UserEnv.h>
#include <Wtsapi32.h>
#pragma comment(lib, "Wtsapi32.lib")

#pragma comment(lib, "advapi32.lib")

std::fstream Log;
#define SVCNAME TEXT("Svc")

SERVICE_STATUS gSvcStatus = { 0 };
SERVICE_STATUS_HANDLE gSvcStatusHandle = NULL;
HANDLE hServiceEvent = NULL;

VOID SvcInstall(void);
DWORD WINAPI ServiceControlHandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContent);
void WINAPI ServiceMain(DWORD dwArgc, LPSTR* lpArgv);

void ServiceReportStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint);
VOID SvcInit(DWORD, LPSTR*);
VOID SvcReportEvent(LPSTR);
BOOL CustomCreateProcess(DWORD wtsSession, DWORD& dwBytes);
char* GetUsernameFromSId(DWORD sId, DWORD& dwBytes);
void ServiceDelete();
void ServiceInstall();

int main(int argc, CHAR* argv[]) {
    Log.open("Log.txt", 9);
    if (!Log)
        Log.open("Log.txt", 1);
    Log.close();
    if (lstrcmpiA(argv[1], "install") == 0)
        ServiceInstall();
    else if (lstrcmpiA(argv[1], "delete") == 0)
        ServiceDelete();
    SERVICE_TABLE_ENTRY DispatchTable[] = {
        {(LPWSTR)SVCNAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
        {NULL, NULL}
    };
    StartServiceCtrlDispatcher(DispatchTable);
}
void ServiceInstall() {
    Log.open("Log.txt", 9);
    SC_HANDLE hSCManager = NULL;
    SC_HANDLE hService = NULL;
    DWORD dwModuleFileName = 0;
    TCHAR szPath[MAX_PATH];
    dwModuleFileName = GetModuleFileName(NULL, szPath, MAX_PATH);
    hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    hService = CreateService(
        hSCManager,
        SVCNAME,
        SVCNAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        szPath,
        NULL, NULL, NULL, NULL, NULL
    );

    Log << "Service successfully installed at ";
    Log.close();
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
}
void ServiceDelete() {
    Log.open("Log.txt", 9);
    SC_HANDLE hSCManager = NULL;
    SC_HANDLE hService = NULL;
    hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    hService = OpenService(hSCManager, SVCNAME, SERVICE_ALL_ACCESS);
    if (!DeleteService(hService))
        Log << GetLastError() << std::endl;
    Log << "Service have been successfully deleted";
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
}

VOID SvcInstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    TCHAR szUnquotedPath[MAX_PATH];

    if (!GetModuleFileName(NULL, szUnquotedPath, MAX_PATH))
    {
        printf("Cannot install service (%d)\n", GetLastError());
        return;
    }

    TCHAR szPath[MAX_PATH];
    StringCchPrintf(szPath, MAX_PATH, TEXT("\"%s\""));
    schSCManager = OpenSCManager(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        SC_MANAGER_ALL_ACCESS);  // full access rights 

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }


    schService = CreateService(
        schSCManager,              // SCM database 
        SVCNAME,                   // name of service 
        SVCNAME,                   // service name to display 
        SERVICE_ALL_ACCESS,        // desired access 
        SERVICE_WIN32_OWN_PROCESS, // service type 
        SERVICE_DEMAND_START,      // start type 
        SERVICE_ERROR_NORMAL,      // error control type 
        szPath,                    // path to service's binary 
        NULL,                      // no load ordering group 
        NULL,                      // no tag identifier 
        NULL,                      // no dependencies 
        NULL,                      // LocalSystem account 
        NULL);                     // no password 

    if (schService == NULL)
    {
        printf("CreateService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }
    else printf("Service installed successfully\n");

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

void WINAPI ServiceMain(DWORD dwArgc, LPSTR* lpArgv) {
    if (Log.is_open())
        Log.sync();
    Log.close();
    Log.open("Log.txt", 9);
    Log << "Service Starting";
    gSvcStatusHandle = RegisterServiceCtrlHandlerEx(SVCNAME, (LPHANDLER_FUNCTION_EX)ServiceControlHandlerEx, NULL);
    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SESSIONCHANGE;
    gSvcStatus.dwServiceSpecificExitCode = 0;
    ServiceReportStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    PWTS_SESSION_INFO wtsSessions;
    PROCESS_INFORMATION processInfo;
    DWORD sessionCount, dwBytes = NULL;
    for (auto i = 1u; i < dwArgc; ++i)
        Log << lpArgv[i] << std::endl;
    Log << "Service have been started";
    if (!WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &wtsSessions, &sessionCount)) {
        Log << GetLastError() << std::endl;
        Log.close();
        return;
    }
    for (auto i = 1; i < sessionCount; ++i)
        CustomCreateProcess(wtsSessions[i].SessionId, dwBytes);
    SvcInit(dwArgc, lpArgv);
    if (!Log.is_open())
        Log.open("Log.txt", 9);
    Log << "Service Initialization complete." << std::endl;
    while (gSvcStatus.dwCurrentState != SERVICE_STOPPED) {
        Log << "Service is running ";
        Log.sync();
        if (WaitForSingleObject(hServiceEvent, 60000) != WAIT_TIMEOUT)
            ServiceReportStatus(SERVICE_STOPPED, NO_ERROR, 0);
    }
    Log.close();
}

void SvcInit(DWORD dwArgc, LPSTR* lpArgv) {
    if (!Log.is_open())
        Log.open("Log.txt", 9);
    hServiceEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (hServiceEvent == NULL) {
        ServiceReportStatus(SERVICE_STOPPED, NO_ERROR, 0);
        return;
    }
    else
        ServiceReportStatus(SERVICE_RUNNING, NO_ERROR, 0);
}

void ServiceReportStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {
    static DWORD dwCheckPoint = 1;
    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;
    if (dwCurrentState == SERVICE_RUNNING || dwCurrentState == SERVICE_STOPPED)
        gSvcStatus.dwCheckPoint = 0;
    else
        gSvcStatus.dwCheckPoint = dwCheckPoint++;
    SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

DWORD WINAPI ServiceControlHandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContent) {
    DWORD errorCode = NO_ERROR, dwBytes = NULL;

    if (Log.is_open())
        Log.sync();
    Log.close();
    Log.open("Log.txt", 9);

    switch (dwControl)
    {
    case SERVICE_CONTROL_INTERROGATE:
        break;
    case SERVICE_CONTROL_SESSIONCHANGE: {
        WTSSESSION_NOTIFICATION* sessionNotification = static_cast<WTSSESSION_NOTIFICATION*>(lpEventData);
        char* pcUserName = GetUsernameFromSId(sessionNotification->dwSessionId, dwBytes);
        if (dwEventType == WTS_SESSION_LOGOFF) {
            Log << pcUserName << " is logging off" << std::endl;
            break;
        }
        else if (dwEventType == WTS_SESSION_LOGON) {
            Log << pcUserName << " is logging in" << std::endl;
            CustomCreateProcess(sessionNotification->dwSessionId, dwBytes);
        }
        delete[] pcUserName;
    }
        break;
    case SERVICE_CONTROL_STOP:
        Log << "Service have been Stopped";
        gSvcStatus.dwCurrentState = SERVICE_STOPPED;
        break;
    case SERVICE_CONTROL_SHUTDOWN:
        Log << "PC is going to SHUTDOWN stopping the Service";
        gSvcStatus.dwCurrentState = SERVICE_STOPPED;
        break;
    default:
        errorCode = ERROR_CALL_NOT_IMPLEMENTED;
        break;
    }

    Log.close();
    ServiceReportStatus(gSvcStatus.dwCurrentState, errorCode, 0);
    return errorCode;
}
BOOL CustomCreateProcess(DWORD wtsSession, DWORD& dwBytes) {
    if (!Log.is_open())
        Log.open("Log.txt", 9);
    HANDLE userToken;
    PROCESS_INFORMATION pi{};
    STARTUPINFO si{};
    WCHAR path[] =  L"C:\\Users\\Анна\\Downloads\\antivirus\\x64\\Debug\\UI.exe";
    WTSQueryUserToken(wtsSession, &userToken);
    CreateProcessAsUser(userToken, NULL, path, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    char* pcUserName = GetUsernameFromSId(wtsSession, dwBytes);
    Log << "Application pId " << pi.dwProcessId << " have been started by user " << pcUserName
        << " in session " << wtsSession;
    delete[] pcUserName;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}
char* GetUsernameFromSId(DWORD sId, DWORD& dwBytes) {
    char* pcUserName = new char[105];
    LPWSTR buff;
    WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, sId, WTSUserName, &buff, &dwBytes);
    WideCharToMultiByte(CP_UTF8, 0, buff, -1, pcUserName, 105, 0, 0);
    WTSFreeMemory(buff);
    return pcUserName;
}
VOID SvcReportEvent(LPSTR szFunction)
{
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];
    TCHAR Buffer[80];

    hEventSource = RegisterEventSource(NULL, SVCNAME);

    if (NULL != hEventSource)
    {
        StringCchPrintf(Buffer, 80, TEXT("%s failed with %d"), szFunction, GetLastError());

        lpszStrings[0] = SVCNAME;
        lpszStrings[1] = Buffer;

        ReportEvent(hEventSource,        // event log handle
            EVENTLOG_ERROR_TYPE, // event type
            0,                   // event category
            SVC_ERROR,           // event identifier
            NULL,                // no security identifier
            2,                   // size of lpszStrings array
            0,                   // no binary data
            lpszStrings,         // array of strings
            NULL);               // no binary data

        DeregisterEventSource(hEventSource);
    }
}