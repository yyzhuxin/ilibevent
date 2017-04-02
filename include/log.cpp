#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include "log.hpp"



static int ShiftFiles(const char* cpszLogBaseName, long lMaxLogSize, int iMaxLogNum)
{
	struct stat stStat;
	char szLogFileName[256];
	char szNewLogFileName[256];

	sprintf(szLogFileName, "%s.log", cpszLogBaseName);

	if(stat(szLogFileName, &stStat) < 0)
	{
		printf("[file %s] [function %s] [line %d] stat failed, errno = %d\n", __FILE__, __FUNCTION__, __LINE__, errno);
		return -1;
	}

	if (stStat.st_size < lMaxLogSize)
	{
		return 0;
	}

	sprintf(szLogFileName,"%s%d.log", cpszLogBaseName, iMaxLogNum - 1);
	if (access(szLogFileName, F_OK) == 0)
	{
		if (remove(szLogFileName) < 0 )
		{
			printf("[file %s] [function %s] [line %d] remove failed, errno = %d\n", __FILE__, __FUNCTION__, __LINE__, errno);
			return -1;
		}
	}

	for(int i = iMaxLogNum - 2; i >= 0; --i)
	{
		if (i == 0)
		{
			sprintf(szLogFileName, "%s.log", cpszLogBaseName);
		}
		else
		{
			sprintf(szLogFileName, "%s%d.log", cpszLogBaseName, i);
		}
		if (access(szLogFileName, F_OK) == 0)
		{
			sprintf(szNewLogFileName, "%s%d.log", cpszLogBaseName, i + 1);
			if (rename(szLogFileName, szNewLogFileName) < 0 )
			{
				printf("[file %s] [function %s] [line %d] rename failed, errno = %d\n", __FILE__, __FUNCTION__, __LINE__, errno);
				return -1;
			}
		}
	}
	return 0;
}

static void GetCurrentTimeStr(char* szTimeStr)
{
	timeval tval;
	gettimeofday(&tval, NULL);
	struct tm curr;
	curr = *localtime(&tval.tv_sec);

	sprintf(szTimeStr, 
		"%04d-%02d-%02d %02d:%02d:%02d.%06d", 
		curr.tm_year + 1900, 
		curr.tm_mon + 1, 
		curr.tm_mday,
		curr.tm_hour, 
		curr.tm_min, 
		curr.tm_sec,
		(int)tval.tv_usec);
}

static int VWriteLog(const char* cpszLogBaseName, long lMaxLogSize, int iMaxLogNum,const char* cpszFormat, va_list ap)
{
	FILE* pFile;
	char szLogFileName[256];

	sprintf(szLogFileName, "%s.log", cpszLogBaseName);
	if ((pFile = fopen(szLogFileName, "a+")) == NULL)
	{
		printf("[file %s] [function %s] [line %d] Fail to open log file %s\n", __FILE__, __FUNCTION__, __LINE__, szLogFileName);
		return -1;
	}

	char szTimeStr[256];
	GetCurrentTimeStr(szTimeStr);

	fprintf(pFile, "[%s] ", szTimeStr);
	vfprintf(pFile, cpszFormat, ap);
	fclose(pFile);

	return ShiftFiles(cpszLogBaseName, lMaxLogSize, iMaxLogNum);
}

static int WriteLog(const char* cpszLogBaseName, long lMaxLogSize, int iMaxLogNum, const char* cpszFormat, ...)
{
	int iRet;
	va_list ap;

	va_start(ap, cpszFormat);
	iRet = VWriteLog(cpszLogBaseName, lMaxLogSize, iMaxLogNum, cpszFormat, ap);
	va_end(ap);

	return iRet;
}

static char sszLogBaseName[256];
static long slMaxLogSize;
static int siMaxLogNum;
static int siLogInitialized = 0;

void LogInit(const char* cpszLogBaseName, long lMaxLogSize, int iMaxLogNum)
{
	memset(sszLogBaseName, 0, sizeof(sszLogBaseName));
	strncpy(sszLogBaseName, cpszLogBaseName, sizeof(sszLogBaseName) - 1);
	slMaxLogSize = lMaxLogSize;
	siMaxLogNum = iMaxLogNum;
	siLogInitialized = 1;
}

void Log(int iLevel, const char* cpszFormat, ...)
{
	char buf[1024 * 1024];
	int32_t len = 0;
	switch (iLevel)
	{
	case ERROR:
		len += snprintf(buf, sizeof(buf) - 1, "[ERROR]");
		break;
	case DEBUG:
		len += snprintf(buf, sizeof(buf) - 1, "[DEBUG]");
		break;
	default:
		len += snprintf(buf, sizeof(buf) - 1, "[ERROR]");
		break;
	}
	va_list ap;
	va_start(ap, cpszFormat);
	len += vsnprintf(buf + len, sizeof(buf) - len - 1, cpszFormat, ap);
	va_end(ap);
	if (len > 0 && buf[len - 1] != '\n')
	{
		strcat(buf, "\n");
	}
	WriteLog(sszLogBaseName, slMaxLogSize, siMaxLogNum, buf);
}

